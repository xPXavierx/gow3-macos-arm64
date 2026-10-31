// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the production SPIR-V emitter, specialization identity and cache reader.
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "common/serdes.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/specialization.h"
#include "video_core/renderer_vulkan/vk_pipeline_serialization.h"

int main(int argc, char** argv) {
    using namespace Shader;
    const std::filesystem::path dir = argc > 1 ? argv[1] : ".";
    const bool invalid_depth = argc > 2 && std::string(argv[2]) == "--invalid-depth";
    std::filesystem::create_directories(dir);
    // The catalogue upgrade uses one formerly zero padding byte, not a guessed
    // reinterpretation of any existing member. Keep its layout contract explicit.
#ifdef ARCH_ARM64
    static_assert(sizeof(RuntimeInfo) == 288 && sizeof(HwFragmentRuntimeInfo) == 216);
    static_assert(sizeof(Backend::Bindings) == 12);
    static_assert(offsetof(RuntimeInfo, hw) + offsetof(HwFragmentRuntimeInfo, depth_before_shader) == 286);
#endif
    unsigned cases = 0;
    for (bool early : {false, true}) for (bool discard : {false, true})
        for (bool depth : {false, true}) {
            if (invalid_depth ? !(early && depth && !discard) : (early && depth)) continue;
            Info info{};
            info.hw_stage = HwStage::Fragment; info.sw_stage = SwStage::Fragment;
            RuntimeInfo runtime{}; runtime.Initialize(info.hw_stage, info.sw_stage);
            runtime.hw.fs.depth_before_shader = early;
            runtime.hw.fs.color_buffers[0].num_format = AmdGpu::NumberFormat::Float;
            StageSpecialization spec; spec.info = &info; spec.runtime_info = runtime;
            auto opposite = spec; opposite.runtime_info.hw.fs.depth_before_shader = !early;
            assert(spec != opposite && opposite != spec);
            Serialization::Archive archive; spec.Serialize(archive);
            Serialization::Archive decoded{archive.TakeWritten(), true};
            StageSpecialization restored; restored.info = &info; restored.Deserialize(decoded);
            assert(restored == spec && spec == restored && restored != opposite);
            Common::ObjectPool<IR::Inst> pool; IR::Block block(pool); IR::IREmitter ir(block);
            for (u32 i = 0; i < 4; ++i) {
                info.stores.Set(IR::Attribute::RenderTarget0, i);
                ir.SetAttribute(IR::Attribute::RenderTarget0, ir.Imm32(1.f), i);
            }
            if (discard) { info.has_discard = true; ir.Discard(); }
            if (depth) {
                info.stores.Set(IR::Attribute::Depth, 0);
                ir.SetAttribute(IR::Attribute::Depth, ir.Imm32(.5f), 0);
            }
            ir.Epilogue(); IR::Program program(info);
            program.blocks.push_back(&block);
            program.syntax_list.push_back({.data = {.block = &block}, .type = IR::AbstractSyntaxNode::Type::Block});
            program.syntax_list.push_back({.type = IR::AbstractSyntaxNode::Type::Return});
            Profile profile{}; profile.supported_spirv = 0x00010600;
            Backend::Bindings bindings{}; std::vector<u32> code;
            // The invalid combination is exercised in a separate process: the
            // production assertion terminates a non-speculative compilation.
            code = Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
            assert(!invalid_depth);
            {
                bool emitted_early = false, emitted_depth = false;
                for (size_t at = 5; at < code.size();) {
                    const unsigned count = code[at] >> 16, op = code[at] & 0xffff;
                    assert(count && at + count <= code.size());
                    if (op == 16 && count >= 3) {
                        emitted_early |= code[at + 2] == 9; // EarlyFragmentTests
                        emitted_depth |= code[at + 2] == 12; // DepthReplacing
                    }
                    at += count;
                }
                assert(emitted_early == early && emitted_depth == depth);
                std::ofstream out(dir / (std::string(early ? "early" : "late") +
                    (discard ? "-discard" : "-keep") + (depth ? "-depth.spv" : ".spv")), std::ios::binary);
                out.write(reinterpret_cast<const char*>(code.data()), code.size() * sizeof(u32));
                assert(out.good());
            }
            ++cases;
        }
    // A stale header must be rejected before any old RuntimeInfo is decoded.
    for (bool old_meta : {false, true}) {
        Serialization::Archive ar; Serialization::Writer writer{ar};
#ifdef ARCH_ARM64
        writer.Write(u32(old_meta ? 0x41520010u : 0x41520011u));
#else
        writer.Write(u32(old_meta ? 10 : 11));
#endif
        writer.Write(u32(old_meta ? 33 : 32));
        Serialization::Archive stale{ar.TakeWritten(), true};
        Info info; std::optional<Gcn::FetchShaderData> fetch; StageSpecialization spec;
        size_t permutation{};
        assert(!Vulkan::LoadShaderMeta(stale, info, fetch, spec, permutation));
    }
    std::printf("PASS: %u early/late, discard/depth-export cases; specialization and serialized identity; old caches rejected; ABI=%016llx\n",
        cases, (unsigned long long)Vulkan::ShaderCatalogABI());
}
