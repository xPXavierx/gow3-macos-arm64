// SPDX-License-Identifier: GPL-2.0-or-later
// Offline render descriptions + exact asset-code equivalence. No guest pointers
// may be followed here. All speculative results retain normal runtime validation.
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#endif
#include "common/hash.h"
#include "common/serdes.h"
#include "core/emulator_settings.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "video_core/cache_storage.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_pipeline_serialization.h"
#include "video_core/renderer_vulkan/vk_instance.h"

namespace Vulkan {
using Shader::HwStage;
using Shader::SwStage;

namespace {
struct AssetProgram { u64 offset{}; u32 length{}, stage{}; std::string digest, path; };
const std::unordered_map<u64, AssetProgram>& AssetPrograms() {
    static const auto index = [] {
        std::unordered_map<u64, AssetProgram> result;
        const char* path = std::getenv("GOW3_SHADER_ASSET_INDEX");
        if (!path) return result;
        std::ifstream stream{path};
        std::string hash;
        AssetProgram entry;
        while (result.size() < 200000 && stream >> hash >> entry.offset >> entry.length >> entry.stage >> entry.digest) {
            stream.get();
            std::getline(stream, entry.path);
            const std::filesystem::path relative{entry.path};
            if (entry.length < 8 || entry.length > 65536 || entry.length % 4 ||
                entry.path.size() > 4096 || relative.is_absolute() ||
                entry.path.find("..") != std::string::npos || entry.digest.size() != 64) continue;
            try { result.emplace(std::stoull(hash, nullptr, 16), entry); } catch (...) {}
        }
        return result;
    }();
    return index;
}

std::vector<u32> ReadAssetCode(u64 hash, const AssetProgram& entry) {
#if defined(__APPLE__)
    const char* game = std::getenv("GOW3_GAME_DIR");
    if (!game) return {};
    std::ifstream stream{std::filesystem::path{game} / entry.path, std::ios::binary};
    if (!stream) return {};
    stream.seekg(entry.offset);
    std::vector<u32> code(entry.length / 4);
    if (!stream.read(reinterpret_cast<char*>(code.data()), entry.length)) return {};
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest;
    CC_SHA256(code.data(), entry.length, digest.data());
    std::string actual;
    for (const auto value : digest) actual += fmt::format("{:02x}", value);
    if (actual != entry.digest || code[0] != 0xbeeb03ff) return {};
    const u64 footer_offset = (u64(code[1]) + 1) * 8;
    if (footer_offset < entry.length || footer_offset > 65536) return {};
    stream.seekg(entry.offset + footer_offset);
    std::array<u8, 28> footer;
    if (!stream.read(reinterpret_cast<char*>(footer.data()), footer.size()) ||
        std::memcmp(footer.data(), "OrbShdr", 7) != 0) return {};
    u64 actual_hash;
    std::memcpy(&actual_hash, footer.data() + 16, sizeof(actual_hash));
    if (actual_hash != hash) return {};
    return code;
#else
    return {};
#endif
}

struct MaterialStageRequest { std::filesystem::path path; u64 target{}, token{}; };
struct MaterialPipelineRequest { std::filesystem::path path; u64 vertex{}, fragment{}; };
struct MaterialHeader { u64 offset{}; size_t length{}; };
void CheckCatalogChecksum(std::vector<u8>& bytes) {
    u32 magic{}; if (bytes.size() < 40) throw std::runtime_error("Catalog description size");
    std::memcpy(&magic, bytes.data(), 4);
#if defined(__APPLE__)
    if (magic == 0x32525347 || magic == 0x32525047) {
        std::array<u8, CC_SHA256_DIGEST_LENGTH> digest;
        CC_SHA256(bytes.data(), bytes.size()-digest.size(), digest.data());
        if (!std::equal(digest.begin(),digest.end(),bytes.end()-digest.size())) throw std::runtime_error("Catalog SHA256");
        bytes.resize(bytes.size()-digest.size()); return;
    }
#endif
    u64 checksum{}; std::memcpy(&checksum,bytes.data()+bytes.size()-8,8); bytes.resize(bytes.size()-8);
    if (XXH3_64bits(bytes.data(),bytes.size())!=checksum) throw std::runtime_error("Catalog checksum");
}

std::vector<u8> ReadMaterialHeader(u64 raw, const std::unordered_map<u64, MaterialHeader>& headers) {
    const auto asset = AssetPrograms().find(raw);
    const auto header = headers.find(raw);
    const char* game = std::getenv("GOW3_GAME_DIR");
    if (!game || asset == AssetPrograms().end() || header == headers.end()) return {};
    const auto [offset, length] = header->second;
    if (offset < 16 || length < 48 || length > 4096 || offset+length != asset->second.offset) return {};
    std::ifstream stream{std::filesystem::path{game}/asset->second.path, std::ios::binary};
    stream.seekg(offset-8);
    u32 code_offset{};
    if (!stream.read(reinterpret_cast<char*>(&code_offset),4) || offset-16+code_offset != asset->second.offset) return {};
    stream.seekg(offset);
    std::vector<u8> bytes(length);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()),length)) return {};
    return bytes;
}
}

void PipelineCache::PrepareProgramAliases() {
    const char* toggle = std::getenv("GOW3_SHADER_EQUIVALENCE");
    if ((toggle && std::strcmp(toggle, "0") == 0) || EmulatorSettings.IsShaderCollect() ||
        EmulatorSettings.IsPatchShaders()) return;
    const auto& index = AssetPrograms();
    // Prefer the known program with most permutations, deterministically. The
    // full current specialization is still checked by GetProgram on every use.
    std::unordered_map<std::string, std::pair<u64, size_t>> selected;
    for (const auto& [hash, program] : program_cache) {
        const auto source = index.find(hash);
        const auto safe = alias_safe_modules.find(hash);
        if (source == index.end() || safe == alias_safe_modules.end() || safe->second.empty()) continue;
        auto& choice = selected[source->second.digest];
        if (safe->second.size() > choice.second ||
            (safe->second.size() == choice.second && hash < choice.first))
            choice = {hash, safe->second.size()};
    }
    std::unordered_map<std::string, std::shared_ptr<const std::vector<u32>>> sources;
    for (const auto& [digest, choice] : selected) {
        auto code = ReadAssetCode(choice.first, index.at(choice.first));
        if (!code.empty()) sources[digest] = std::make_shared<const std::vector<u32>>(std::move(code));
    }
    for (const auto& [hash, entry] : index) {
        if (program_cache.contains(hash)) continue;
        const auto source = sources.find(entry.digest);
        if (source == sources.end()) continue;
        const auto canonical = selected.at(entry.digest).first;
        const auto& info = program_cache.at(canonical)->info;
        if (u32(info.hw_stage) != entry.stage) continue;
        program_aliases.emplace(hash, ProgramAlias{canonical, info.hw_stage, source->second});
    }
    program_aliases_ready.store(true, std::memory_order_release);
    std::printf("Shader asset equivalence: %zu additional IDs in %zu code families; full byte comparison on use\n",
                program_aliases.size(), sources.size());
}

namespace {
// These are render descriptions, not SPIR-V, native GPU binaries or game code.
constexpr u32 RecipeMagic = 0x31525347; // GSR1
constexpr u32 RecipeVersion = 1;

Shader::Info NeutralDescription(const Shader::Info& source) {
    Shader::Info result;
    result.pgm_hash = source.pgm_hash;
    result.hw_stage = source.hw_stage;
    result.sw_stage = source.sw_stage;
    result.srt_info = source.srt_info;
    result.flattened_ud_buf.resize(source.flattened_ud_buf.size());
    const auto preserve = [&](const auto& fetch, const auto& sharp) {
        const auto words = std::bit_cast<std::array<u32, sizeof(sharp)/4>>(sharp);
        for (u32 i = 0; i < words.size(); ++i) {
            if ((fetch.load_mask >> i & 1) && fetch.offsets[i] != Shader::UNKNOWN_LOCATION) {
                if (fetch.offsets[i] >= result.flattened_ud_buf.size())
                    throw std::runtime_error("Recipe descriptor exceeds resource snapshot");
                result.flattened_ud_buf[fetch.offsets[i]] = words[i];
            }
        }
    };
    for (const auto& desc : source.buffers) {
        AmdGpu::Buffer sharp{};
        if (desc.sharp_fetch.Fetch(source.flattened_ud_buf.data(), &sharp)) {
            sharp.base_address = 1;
            preserve(desc.sharp_fetch, sharp);
        }
    }
    for (const auto& desc : source.images) {
        AmdGpu::Image sharp{};
        if (desc.sharp_fetch.Fetch(source.flattened_ud_buf.data(), &sharp)) {
            sharp.base_address = 1;
            preserve(desc.sharp_fetch, sharp);
        }
    }
    for (const auto& desc : source.samplers) {
        AmdGpu::Sampler sharp{};
        if (desc.sharp_fetch.Fetch(source.flattened_ud_buf.data(), &sharp))
            preserve(desc.sharp_fetch, sharp);
        if (desc.post_op == Shader::SharpFetchPostOp::DisableAnisoIfSingleLod &&
            desc.post_op_tsharp_dw3_off < result.flattened_ud_buf.size())
            result.flattened_ud_buf[desc.post_op_tsharp_dw3_off] =
                source.flattened_ud_buf[desc.post_op_tsharp_dw3_off];
    }
    for (const auto& desc : source.fmasks) {
        auto sharp = desc.GetSharp(source);
        sharp.base_address = 1;
        if (desc.sharp_idx + sizeof(sharp)/4 > result.flattened_ud_buf.size())
            throw std::runtime_error("Recipe fmask exceeds resource snapshot");
        std::memcpy(result.flattened_ud_buf.data() + desc.sharp_idx, &sharp, sizeof(sharp));
    }
    return result;
}
}

void PipelineCache::ExportShaderRecipe(const Shader::Info& info,
    const Shader::StageSpecialization& spec, size_t permutation, std::span<const u32> expected) {
    if (const char* path = std::getenv("GOW3_SHADER_CATALOG_EXPORT"))
        WriteShaderRecipe(info,spec,permutation,expected,path,false);
    const char* directory = std::getenv("GOW3_COMMUNITY_RECORD_DIR");
    if (!directory || !community_recorder || !community_recorder->HasCapacity() || expected.empty() ||
        (info.hw_stage != HwStage::Vertex && info.hw_stage != HwStage::Fragment)) return;
    const auto key=fmt::format("s_{:016x}_{}",info.pgm_hash,permutation);
    std::scoped_lock lock{community_record_mutex};
    if (community_recorded.contains(key)) return;
    try {
        auto owned=std::make_shared<Shader::Info>(info);
        owned->flattened_ud_buf=NeutralDescription(info).flattened_ud_buf;
        owned->user_data={}; owned->pgm_base=0; owned->compilation_flat_snapshot.reset();
        auto specialization=spec; specialization.info=owned.get();
        std::vector<u32> reference(expected.begin(),expected.end());
        if (community_recorder->Submit([this,owned,specialization,permutation,reference=std::move(reference),path=std::string(directory)] {
            WriteShaderRecipe(*owned,specialization,permutation,reference,path.c_str(),true);
        })) community_recorded.insert(key);
    } catch (const std::exception& failure) {
        std::printf("Community recording deferred: %s\n",failure.what());
    }
}
void PipelineCache::ExportPipelineRecipe(const GraphicsPipelineKey& key,
    const GraphicsPipeline::SerializationSupport& data) {
    if (const char* path = std::getenv("GOW3_SHADER_CATALOG_EXPORT")) WritePipelineRecipe(key,data,path,false);
    const char* directory=std::getenv("GOW3_COMMUNITY_RECORD_DIR");
    if (!directory || !community_recorder || !community_recorder->HasCapacity()) return;
    const auto identity=fmt::format("p_{:016x}",std::hash<GraphicsPipelineKey>{}(key));
    std::scoped_lock lock{community_record_mutex};
    if (community_recorded.contains(identity)) return;
    if (community_recorder->Submit([this,key,data,path=std::string(directory)] {
        try { WritePipelineRecipe(key,data,path.c_str(),true); }
        catch (const std::exception& failure) { std::printf("Community pipeline deferred: %s\n",failure.what()); }
    })) community_recorded.insert(identity);
}

void PipelineCache::WriteShaderRecipe(const Shader::Info& info,
    const Shader::StageSpecialization& spec, size_t permutation, std::span<const u32> expected,
    const char* directory, bool community) {
#if defined(__APPLE__) && defined(ARCH_ARM64)
    if (!directory) return;
    const auto path = std::filesystem::path{directory} /
        fmt::format("{:016x}_{}.recipe", info.pgm_hash, permutation);
    if (std::filesystem::exists(path)) return;
    Common::ScopedSpeculativeCompilation speculative;
    try {
        auto description = NeutralDescription(info);
        const auto rebuilt = RecompileCatalogShader(description, spec);
        if (rebuilt.size() != expected.size() ||
            !std::equal(rebuilt.begin(), rebuilt.end(), expected.begin())) {
            std::printf("Shader recipe not exported (nonidentical reconstruction): %016llx/%zu\n",
                        (unsigned long long)info.pgm_hash, permutation);
            return;
        }
        Serialization::Archive ar;
        Serialization::Writer writer{ar};
        writer.Write(community ? u32{0x32525347} : RecipeMagic); // GSR2 has no resource-walker program
        writer.Write(community ? u32{2} : RecipeVersion);
        writer.Write(u32(sizeof(Shader::RuntimeInfo)));
        writer.Write(u32(sizeof(GraphicsPipelineKey)));
        writer.Write(info.pgm_hash);
        writer.Write(info.hw_stage);
        writer.Write(info.sw_stage);
        writer.Write(permutation);
        writer.Write(description.flattened_ud_buf);
        std::vector<u8> walker(info.srt_info.walker_func_size);
        if (!walker.empty()) std::memcpy(walker.data(), Shader::SrtWalkerCode(info.srt_info.walker_func), walker.size());
        if (community) {
            writer.Write(u64(walker.size()));
            writer.Write(Shader::SrtLayoutFingerprint(info.srt_info.walker_func,walker.size()));
        } else writer.Write(walker);
        spec.Serialize(ar);
        auto bytes = ar.TakeWritten();
        if (community) {
            std::array<u8, CC_SHA256_DIGEST_LENGTH> digest;
            CC_SHA256(bytes.data(), bytes.size(), digest.data());
            bytes.insert(bytes.end(), digest.begin(), digest.end());
        } else {
            const u64 checksum = XXH3_64bits(bytes.data(), bytes.size());
            const auto* tail = reinterpret_cast<const u8*>(&checksum);
            bytes.insert(bytes.end(), tail, tail + sizeof(checksum));
        }
        const auto asset = AssetPrograms().find(info.pgm_hash);
        if (community && asset == AssetPrograms().end()) return;
        std::filesystem::create_directories(directory);
        const auto temporary=path.string()+".tmp";
        { std::ofstream output{temporary,std::ios::binary};
          output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
          if(!output) throw std::runtime_error("Contribution write"); }
        if (asset != AssetPrograms().end()) {
            std::ofstream fingerprints{std::filesystem::path{directory} / "binary-fingerprints.tsv", std::ios::app};
            fingerprints << path.filename().string() << '\t'
                         << fmt::format("{:016x}", XXH3_64bits(expected.data(), expected.size_bytes()))
                         << '\t' << asset->second.digest << '\n';
            if(!fingerprints) throw std::runtime_error("Contribution proof write");
        }
        std::filesystem::rename(temporary,path);
        const auto abi=std::filesystem::path{directory}/"compatibility.txt";
        if(!std::filesystem::exists(abi)) { std::ofstream output{abi}; output<<fmt::format("{:016x}\n",ShaderCatalogABI()); }
    } catch (const std::exception& error) {
        std::printf("Shader recipe export rejected: %s\n", error.what());
    }
#endif
}


void PipelineCache::WritePipelineRecipe(const GraphicsPipelineKey& key,
    const GraphicsPipeline::SerializationSupport& data, const char* directory, bool community) {
    if (!directory || !data.tcs.empty() || !data.tes.empty() ||
        data.multisampling.pNext || data.multisampling.pSampleMask) return;
    for (size_t i = 0; i < MaxShaderStages; ++i)
        if (i != u32(SwStage::Fragment) && i != u32(SwStage::Vertex) && key.stage_hashes[i]) return;
    Serialization::Archive ar;
    Serialization::Writer writer{ar};
    writer.Write(community ? u32{0x32525047} : u32{0x31525047}); // GPR2 public / GPR1 local
    writer.Write(community ? u32{2} : RecipeVersion);
    writer.Write(u32(sizeof(GraphicsPipelineKey)));
    key.Serialize(ar);
    writer.Write(data.vertex_attributes);
    writer.Write(data.vertex_bindings);
    writer.Write(data.divisors);
    writer.Write(data.multisampling.flags);
    writer.Write(data.multisampling.rasterizationSamples);
    writer.Write(data.multisampling.sampleShadingEnable);
    writer.Write(data.multisampling.minSampleShading);
    writer.Write(data.multisampling.alphaToCoverageEnable);
    writer.Write(data.multisampling.alphaToOneEnable);
    auto bytes = ar.TakeWritten();
#if defined(__APPLE__)
    if (community) {
        std::array<u8, CC_SHA256_DIGEST_LENGTH> digest;
        CC_SHA256(bytes.data(), bytes.size(), digest.data());
        bytes.insert(bytes.end(), digest.begin(), digest.end());
    } else
#endif
    {
        const u64 checksum = XXH3_64bits(bytes.data(), bytes.size());
        const auto* tail = reinterpret_cast<const u8*>(&checksum);
        bytes.insert(bytes.end(), tail, tail + 8);
    }
    std::filesystem::create_directories(directory);
    const auto abi = std::filesystem::path{directory} / "compatibility.txt";
    if (!std::filesystem::exists(abi)) {
        std::ofstream output{abi};
        output << fmt::format("{:016x}\n", ShaderCatalogABI());
    }
    const auto path = std::filesystem::path{directory} /
        fmt::format("g_{:#018x}.pso", std::hash<GraphicsPipelineKey>{}(key));
    std::ofstream output{path, std::ios::binary};
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void PipelineCache::ImportShaderCatalog(const std::function<void(u32, u32)>& progress) {
#if defined(__APPLE__) && defined(ARCH_ARM64)
    const char* directory = std::getenv("GOW3_SHADER_CATALOG");
    if (!directory || !std::getenv("GOW3_SHADER_ASSET_INDEX") ||
        !Storage::DataBase::Instance().ConcurrentReads()) return;
    u64 abi{};
    std::ifstream compatibility{std::filesystem::path{directory} / "compatibility.txt"};
    if (!(compatibility >> std::hex >> abi) || abi != ShaderCatalogABI()) {
        std::printf("Shader catalog: incompatible description ABI; using normal complete rendering\n");
        return;
    }
    Common::ScopedSpeculativeCompilation speculative;
    std::vector<MaterialStageRequest> recipes;
    std::vector<MaterialPipelineRequest> pipelines;
    std::unordered_map<u64, MaterialHeader> material_headers;
    std::error_code error;
    for (const auto& file : std::filesystem::directory_iterator{directory, error})
        if (file.path().extension() == ".recipe") recipes.push_back({file.path()});
    if (recipes.size() > 65536) return;
    std::ranges::sort(recipes, {}, &MaterialStageRequest::path);
    for (const auto& file : std::filesystem::directory_iterator{directory, error})
        if (file.path().extension() == ".pso") pipelines.push_back({file.path()});
    if (const char* plan = std::getenv("GOW3_MATERIAL_PLAN")) {
        const std::filesystem::path folder{plan};
        std::ifstream header_file{folder/"material-headers.tsv"};
        std::string raw, digest;
        MaterialHeader header;
        while (material_headers.size()<200000 && header_file>>raw>>header.offset>>header.length>>digest) {
            try { material_headers.emplace(std::stoull(raw,nullptr,16),header); } catch (...) {}
        }
        const auto safe_name = [](const std::string& name, const char* extension) {
            const std::filesystem::path path{name};
            return path.filename()==path && path.extension()==extension;
        };
        std::ifstream stage_file{folder/"material-stages.tsv"};
        std::string token, target, name;
        size_t count{};
        while (count++<8192 && stage_file>>token>>target>>name) {
            if (!safe_name(name,".recipe")) continue;
            try { recipes.push_back({std::filesystem::path{directory}/name,
                std::stoull(target,nullptr,16),std::stoull(token,nullptr,16)}); } catch (...) {}
        }
        std::ifstream pipeline_file{folder/"material-pipelines.tsv"};
        std::string vertex,fragment;
        count=0;
        while (count++<8192 && pipeline_file>>name>>vertex>>fragment) {
            if (!safe_name(name,".pso")) continue;
            try { pipelines.push_back({std::filesystem::path{directory}/name,
                std::stoull(vertex,nullptr,16),std::stoull(fragment,nullptr,16)}); } catch (...) {}
        }
    }
    Storage::DataBase::Instance().FinishPreload();
    const auto started = std::chrono::steady_clock::now();
    size_t compiled{}, reused{}, rejected{}, done{}, material_compiled{}, material_reused{}, material_rejected{};
    std::unordered_map<std::string, std::pair<u64, std::string>> fingerprints;
    {
        std::ifstream file{std::filesystem::path{directory} / "binary-fingerprints.tsv"};
        std::string name, hash, code_digest;
        while (file >> name >> hash >> code_digest) {
            try { fingerprints.emplace(name, std::pair{std::stoull(hash, nullptr, 16), code_digest}); }
            catch (const std::exception&) {}
        }
    }
    struct ExistingStage {
        Shader::Info info;
        Shader::StageSpecialization spec;
        size_t permutation{};
        u64 binary_hash{};
    };
    std::unordered_map<u64, std::vector<std::unique_ptr<ExistingStage>>> existing_stages;
    Storage::DataBase::Instance().ForEachBlob(Storage::BlobType::ShaderMeta,
        [&](std::vector<u8>&& bytes) {
            try {
                auto stage = std::make_unique<ExistingStage>();
                Serialization::Archive ar{std::move(bytes),true};
                std::optional<Shader::Gcn::FetchShaderData> fetch;
                stage->spec.info = &stage->info;
                if (!LoadShaderMeta(ar, stage->info, fetch, stage->spec, stage->permutation) ||
                    stage->permutation > 1024) return;
                std::vector<u32> binary;
                Storage::DataBase::Instance().Load(Storage::BlobType::ShaderBinary,
                    fmt::format("{:#018x}_{}", stage->info.pgm_hash, stage->permutation), binary);
                if (!binary.empty()) {
                    stage->binary_hash = XXH3_64bits(binary.data(), binary.size()*sizeof(u32));
                    existing_stages[stage->info.pgm_hash].push_back(std::move(stage));
                }
            } catch (const std::exception& failure) {
                std::printf("Shader catalog: unusable existing stage: %s\n", failure.what());
            }
        });
    // Recipe indices belong to their source recording. They never overwrite an
    // unrelated variant already at that index in the player's persistent cache.
    std::unordered_map<u64, u64> stage_remap;
    for (const auto& request : recipes) {
        const auto& path = request.path;
        try {
            const auto length = std::filesystem::file_size(path);
            if (length < 48 || length > 1024 * 1024) throw std::runtime_error("Recipe size");
            std::vector<u8> bytes(length);
            std::ifstream input{path, std::ios::binary};
            if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) throw std::runtime_error("Recipe read");
            CheckCatalogChecksum(bytes);
            Serialization::Archive ar{std::move(bytes),true};
            Serialization::Reader reader{ar};
            u32 magic, version, runtime_size, key_size;
            reader.Read(magic); reader.Read(version); reader.Read(runtime_size); reader.Read(key_size);
            const bool community = magic == 0x32525347 && version == 2;
            if ((!community && (magic != RecipeMagic || version != RecipeVersion)) ||
                runtime_size != sizeof(Shader::RuntimeInfo) || key_size != sizeof(GraphicsPipelineKey))
                throw std::runtime_error("Recipe ABI");
            Shader::Info description;
            size_t permutation;
            reader.Read(description.pgm_hash); reader.Read(description.hw_stage);
            reader.Read(description.sw_stage); reader.Read(permutation);
            if (permutation > 1024 || (description.hw_stage != HwStage::Vertex && description.hw_stage != HwStage::Fragment) ||
                (description.sw_stage != SwStage::Vertex && description.sw_stage != SwStage::Fragment))
                throw std::runtime_error("Recipe stage");
            reader.Read(description.flattened_ud_buf);
            if (description.flattened_ud_buf.size() < 16 || description.flattened_ud_buf.size() > 65536)
                throw std::runtime_error("Recipe resources");
            std::vector<u8> walker;
            u64 layout_hash{}, walker_size{};
            if (community) { reader.Read(walker_size); reader.Read(layout_hash); }
            else { reader.Read(walker); walker_size=walker.size(); }
            if (walker_size > 65536 || (community && !layout_hash)) throw std::runtime_error("Recipe layout");
            description.srt_info.flattened_bufsize_dw = description.flattened_ud_buf.size();
            description.srt_info.walker_func_size = walker_size;
            if (!walker.empty()) description.srt_info.walker_func = Shader::RegisterWalkerCode(walker.data(), walker.size());
            if (community) description.compilation_flat_snapshot = Shader::Info::CompilationSnapshot{
                description.flattened_ud_buf, description.srt_info, layout_hash};
            Shader::StageSpecialization spec;
            spec.info = &description;
            spec.Deserialize(ar);
            if (spec.buffers.size()>128 || spec.images.size()>128 || spec.samplers.size()>128 ||
                spec.fmasks.size()>128 || spec.vs_attribs.size()>32 ||
                (spec.fetch_shader_data && spec.fetch_shader_data->attributes.size()>32))
                throw std::runtime_error("Recipe resource count");
            if (spec.runtime_info.hw_stage != description.hw_stage ||
                spec.runtime_info.sw_stage != description.sw_stage) throw std::runtime_error("Recipe runtime stage");
            if (!ar.IsEoS()) throw std::runtime_error("Recipe trailing bytes");
            if (request.target) {
                const auto source = AssetPrograms().find(description.pgm_hash);
                const auto target = AssetPrograms().find(request.target);
                const auto reference = ReadMaterialHeader(description.pgm_hash,material_headers);
                if (source==AssetPrograms().end() || target==AssetPrograms().end() ||
                    source->second.stage!=target->second.stage || reference.empty() ||
                    reference!=ReadMaterialHeader(request.target,material_headers))
                    throw std::runtime_error("Material shader ABI header mismatch");
                description.pgm_hash=request.target;
            }
            auto& current = existing_stages[description.pgm_hash];
            const auto fingerprint = fingerprints.find(path.filename().string());
            const auto asset = AssetPrograms().find(description.pgm_hash);
            if (asset == AssetPrograms().end() || (!request.target && fingerprint != fingerprints.end() &&
                asset->second.digest != fingerprint->second.second))
                throw std::runtime_error("Recipe source code differs from indexed dump");
            u64 expected_hash = request.target || fingerprint == fingerprints.end() ? 0 : fingerprint->second.first;
            const auto find_existing = [&](u64 binary_hash) {
                return std::ranges::find_if(current, [&](const auto& stage) {
                    const auto& previous = stage->spec;
                    const auto& layout = stage->info.srt_info;
                    const auto& expected_layout = description.srt_info;
                    return stage->binary_hash == binary_hash &&
                        layout.flattened_bufsize_dw == expected_layout.flattened_bufsize_dw &&
                        layout.walker_func_size == expected_layout.walker_func_size &&
                        (layout_hash ? Shader::SrtLayoutFingerprint(layout.walker_func,layout.walker_func_size)==layout_hash :
                         (!layout.walker_func_size || std::memcmp(Shader::SrtWalkerCode(layout.walker_func),
                            Shader::SrtWalkerCode(expected_layout.walker_func), layout.walker_func_size) == 0)) &&
                        previous.buffers.size() == spec.buffers.size() &&
                        previous.images.size() == spec.images.size() &&
                        previous.samplers.size() == spec.samplers.size() &&
                        previous.bitset == spec.bitset && previous == spec && spec == previous;
                });
            };
            auto same = expected_hash ? find_existing(expected_hash) : current.end();
            Shader::Info rebuilt;
            std::vector<u32> binary;
            if (same == current.end()) {
                // Another GPU profile can legitimately emit different SPIR-V.
                // Reconstruct it locally before deciding which cached variant
                // is equivalent. Matching descriptor types alone is not proof.
                binary = RecompileCatalogShader(description, spec, &rebuilt);
                if (binary.empty()) throw std::runtime_error("Recipe does not match source/profile");
                expected_hash = XXH3_64bits(binary.data(), binary.size()*sizeof(u32));
                same = find_existing(expected_hash);
            }
            const u64 original_hash = request.target ? request.token : HashCombine(description.pgm_hash, permutation);
            if (same != current.end()) {
                stage_remap[original_hash] = HashCombine(description.pgm_hash, (*same)->permutation);
                alias_safe_modules[description.pgm_hash].insert((*same)->permutation);
                if(request.target) material_modules[description.pgm_hash].insert((*same)->permutation);
                ++reused; if(request.target) ++material_reused;
                if (progress) progress(++done,recipes.size());
                continue;
            }
            size_t local_permutation = 0;
            for (const auto& stage : current) local_permutation = std::max(local_permutation, stage->permutation + 1);
            if (local_permutation > 1024) throw std::runtime_error("Too many shader permutations");
            spec.info = &rebuilt;
            RegisterShaderBinary(std::move(binary), description.pgm_hash, local_permutation);
            RegisterShaderMeta(rebuilt, spec.fetch_shader_data, spec,
                               HashCombine(description.pgm_hash, local_permutation), local_permutation);
            stage_remap[original_hash] = HashCombine(description.pgm_hash, local_permutation);
            auto stage = std::make_unique<ExistingStage>();
            stage->info = std::move(rebuilt);
            stage->spec = std::move(spec);
            stage->spec.info = &stage->info;
            stage->permutation = local_permutation;
            stage->binary_hash = expected_hash;
            alias_safe_modules[description.pgm_hash].insert(local_permutation);
            if(request.target) material_modules[description.pgm_hash].insert(local_permutation);
            current.push_back(std::move(stage));
            ++compiled;
            if(request.target) ++material_compiled;
        } catch (const std::exception& failure) {
            ++rejected;
            if(request.target) ++material_rejected;
            std::printf("Shader catalog recipe rejected: %s %s\n", path.filename().c_str(), failure.what());
        }
        if (progress) progress(++done, recipes.size());
    }
    Storage::DataBase::Instance().WaitForWrites(); // startup only, before live draws exist
    size_t prepared_modules{};
    for (auto& [hash, stages] : existing_stages) {
        for (auto& stage : stages) {
            std::vector<u8> bytes;
            Storage::DataBase::Instance().Load(Storage::BlobType::ShaderMeta,
                fmt::format("{:#018x}", HashCombine(hash, stage->permutation)), bytes);
            if (bytes.empty()) continue;
            Serialization::Archive ar{std::move(bytes),true};
            if (LoadPipelineStage(ar, u32(stage->info.sw_stage))) ++prepared_modules;
        }
    }
    sel.infos.fill(nullptr); sel.modules.fill(nullptr); sel.fetch_shader.reset();
    std::printf("Shader catalog modules: %zu available before play\n", prepared_modules);
    // Descriptions whose stages were unavailable are safely rejected by the
    // regular loader. Never mark an incomplete draw as successfully prepared.
    size_t pipeline_descriptions{}, incomplete{};
    size_t material_pipelines{};
    for (const auto& request : pipelines) {
        const auto& path = request.path;
        const auto length = std::filesystem::file_size(path,error);
        if (error || length > 65536 || length < 32) continue;
        try {
            std::vector<u8> bytes(length);
            std::ifstream input{path, std::ios::binary};
            if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) continue;
            CheckCatalogChecksum(bytes);
            Serialization::Archive ar{std::move(bytes),true};
            Serialization::Reader reader{ar};
            u32 magic, version, key_size;
            reader.Read(magic); reader.Read(version); reader.Read(key_size);
            if (!((magic == 0x31525047 && version == RecipeVersion) || (magic == 0x32525047 && version == 2)) || key_size != sizeof(GraphicsPipelineKey))
                throw std::runtime_error("PSO version");
            GraphicsPipelineKey key;
            key.Deserialize(ar);
            if (key.num_color_attachments>8 || key.mrt_mask>255 || key.motion_vectors>1 ||
                !std::has_single_bit(unsigned(key.num_samples)) || key.num_samples>16 ||
                !std::has_single_bit(unsigned(key.depth_samples)) || key.depth_samples>16 ||
                key.patch_control_points>32 || unsigned(key.polygon_mode)>2)
                throw std::runtime_error("PSO numeric state");
            GraphicsPipeline::SerializationSupport data{};
            reader.Read(data.vertex_attributes); reader.Read(data.vertex_bindings); reader.Read(data.divisors);
            reader.Read(data.multisampling.flags);
            reader.Read(data.multisampling.rasterizationSamples);
            reader.Read(data.multisampling.sampleShadingEnable);
            reader.Read(data.multisampling.minSampleShading);
            reader.Read(data.multisampling.alphaToCoverageEnable);
            reader.Read(data.multisampling.alphaToOneEnable);
            if (!ar.IsEoS()) throw std::runtime_error("PSO trailing bytes");
            if (request.vertex) {
                key.stage_hashes[u32(SwStage::Vertex)]=request.vertex;
                key.stage_hashes[u32(SwStage::Fragment)]=request.fragment;
            }
            bool complete = true;
            for (size_t stage = 0; stage < MaxShaderStages; ++stage) {
                auto& hash = key.stage_hashes[stage];
                if (!hash) continue;
                if (stage != u32(SwStage::Vertex) && stage != u32(SwStage::Fragment)) complete = false;
                const auto remapped = stage_remap.find(hash);
                if (remapped == stage_remap.end()) complete = false;
                else hash = remapped->second;
            }
            if (!complete) { ++incomplete; continue; }
            const auto name = fmt::format("g_{:#018x}", std::hash<GraphicsPipelineKey>{}(key));
            std::vector<u8> existing;
            Storage::DataBase::Instance().Load(Storage::BlobType::PipelineKey, name, existing);
            if (existing.empty()) RegisterPipelineData(key, std::hash<GraphicsPipelineKey>{}(key), data);
            ++pipeline_descriptions;
            if(request.vertex) ++material_pipelines;
        } catch (const std::exception& failure) {
            std::printf("Shader catalog PSO rejected: %s %s\n", path.filename().c_str(), failure.what());
        }
    }
    std::printf("Shader catalog PSO descriptions: %zu ready, %zu incomplete (deferred to required rendering)\n",
                pipeline_descriptions, incomplete);
    std::printf("Material preparation: %zu stages rebuilt, %zu reused, %zu rejected; %zu pipeline descriptions ready\n",
        material_compiled,material_reused,material_rejected,material_pipelines);
    Storage::DataBase::Instance().WaitForWrites();
    std::printf("Shader catalog: %zu rebuilt, %zu reused, %zu rejected in %.3f s before play\n",
                compiled, reused, rejected,
                std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
#endif
}

// Experimental offline reconstruction. Only exact matching specializations are
// published. The source is read from the player's dump, not a bundled binary.
std::vector<u32> PipelineCache::RecompileCatalogShader(
    const Shader::Info& recorded, const Shader::StageSpecialization& specialization,
    Shader::Info* rebuilt) {
#if defined(__APPLE__) && defined(ARCH_ARM64)
    const char* index_path = std::getenv("GOW3_SHADER_ASSET_INDEX");
    const char* game_path = std::getenv("GOW3_GAME_DIR");
    if (!index_path || !game_path ||
        (recorded.hw_stage != HwStage::Vertex && recorded.hw_stage != HwStage::Fragment)) return {};
    Common::ScopedSpeculativeCompilation speculative;
    try {
        const auto& index = AssetPrograms();
        const auto found = index.find(recorded.pgm_hash);
        if (found == index.end()) return {};
        auto code = ReadAssetCode(recorded.pgm_hash, found->second);
        if (code.empty()) return {};
        const u64 hash = recorded.pgm_hash;

        std::array<u32, 16> userdata{};
        if (recorded.flattened_ud_buf.size() < userdata.size()) return {};
        std::copy_n(recorded.flattened_ud_buf.begin(), userdata.size(), userdata.begin());
        const Shader::ShaderParams params{userdata, code, hash};
        Shader::Info info{recorded.hw_stage, recorded.sw_stage, params};
        info.compilation_flat_snapshot = Shader::Info::CompilationSnapshot{
            recorded.flattened_ud_buf, recorded.srt_info,
            recorded.compilation_flat_snapshot ? recorded.compilation_flat_snapshot->layout_fingerprint : 0};
        Shader::Gcn::FetchShaderSnapshot fetch;
        fetch.info = &info;
        fetch.fetch = specialization.fetch_shader_data;
        if (fetch.fetch) {
            if (fetch.fetch->attributes.size() != specialization.vs_attribs.size()) return {};
            for (const auto& attrib : specialization.vs_attribs) {
                AmdGpu::Buffer buffer{};
                buffer.base_address = 1;
                buffer.num_records = 1;
                buffer.stride = 16;
                buffer.data_format = u32(AmdGpu::DataFormat::Format32_32_32_32);
                buffer.num_format = u32(attrib.num_class == AmdGpu::NumberClass::Uint
                    ? AmdGpu::NumberFormat::Uint : attrib.num_class == AmdGpu::NumberClass::Sint
                    ? AmdGpu::NumberFormat::Sint : AmdGpu::NumberFormat::Float);
                buffer.dst_sel_x = u32(attrib.dst_select.r);
                buffer.dst_sel_y = u32(attrib.dst_select.g);
                buffer.dst_sel_z = u32(attrib.dst_select.b);
                buffer.dst_sel_w = u32(attrib.dst_select.a);
                fetch.buffers.push_back(buffer);
            }
        }
        Shader::Gcn::ScopedFetchShaderSnapshot fetch_scope{fetch};
        Shader::Pools offline_pools;
        auto runtime = specialization.runtime_info;
        auto binding = specialization.start;
        const auto ir = Shader::TranslateProgram(code, offline_pools, info, runtime, profile);
        const Shader::StageSpecialization compiled{info, runtime, profile, specialization.start};
        if (compiled.buffers.size() != specialization.buffers.size() ||
            compiled.images.size() != specialization.images.size() ||
            compiled.samplers.size() != specialization.samplers.size() ||
            compiled.fmasks.size() != specialization.fmasks.size() ||
            compiled.bitset != specialization.bitset ||
            compiled.vs_attribs.size() != specialization.vs_attribs.size() ||
            !(compiled == specialization) || !(specialization == compiled)) return {};
        auto binary = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime, ir, binding);
        if (std::getenv("GOW3_SHADER_TRACE"))
            std::printf("Shader asset reconstruction: %016llx %zu words\n",
                        (unsigned long long)hash, binary.size());
        if (rebuilt) {
            *rebuilt = std::move(info);
            rebuilt->compilation_flat_snapshot.reset();
            rebuilt->user_data = {};
            rebuilt->pgm_base = 0;
        }
        return binary;
    } catch (const std::exception& error) {
        std::printf("Shader asset reconstruction rejected: %016llx %s\n",
                    (unsigned long long)recorded.pgm_hash, error.what());
    }
#endif
    return {};
}


} // namespace Vulkan
