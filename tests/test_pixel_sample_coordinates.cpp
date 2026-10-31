// SPDX-License-Identifier: GPL-2.0-or-later
// Run the production image-sample lowering: derived dimension queries must
// retain guest pixel coordinates even when only the sampler requests them.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "common/assert.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/passes/resource_pass.h"
#include "shader_recompiler/profile.h"
namespace Vulkan { u64 ShaderCatalogABI(); }

int main() {
    using namespace Shader;
    for (bool msaa : {false, true}) for (bool sampler_pixels : {false, true})
        for (bool instruction_pixels : {false, true}) {
        Info info{};
        info.flattened_ud_buf.resize(16);
        AmdGpu::Sampler sharp{};
        sharp.force_unnormalized.Assign(sampler_pixels);
        AmdGpu::Image image = AmdGpu::Image::Null(false);
        image.type = u64(msaa ? AmdGpu::ImageType::Color2DMsaa : AmdGpu::ImageType::Color2D);
        Common::ObjectPool<IR::Inst> pool;
        IR::Block block(pool);
        IR::IREmitter ir(block);
        auto coords = ir.CompositeConstruct(ir.Imm32(123.5f), ir.Imm32(87.5f),
                                           ir.Imm32(0.f), ir.Imm32(0.f));
        IR::TextureInstInfo flags{};
        flags.is_unnormalized.Assign(instruction_pixels);
        const auto sample = ir.ImageSampleRaw(ir.Imm32(0u), ir.Imm32(0u), coords, coords,
                                              coords, ir.Imm32(0.f), flags);
        Optimization::ResourceDiscovery usage{.user = sample.Inst()};
        usage.sharps[0].num_dwords = 8;
        usage.sharps[1].num_dwords = 4;
        std::array<u32,8> image_words;
        std::array<u32,4> sampler_words;
        std::memcpy(image_words.data(), &image, sizeof(image));
        std::memcpy(sampler_words.data(), &sharp, sizeof(sharp));
        for (unsigned i=0;i<8;++i) usage.sharps[0].dwords[i] = ir.Imm32(image_words[i]);
        for (unsigned i=0;i<4;++i) usage.sharps[1].dwords[i] = ir.Imm32(sampler_words[i]);
        Optimization::ResourcePatchingPass(info, {usage}, Profile{});
        const bool pixels = sampler_pixels || instruction_pixels;
        assert(info.images[0].needs_native == msaa);
        assert(info.images[0].needs_exact_size == (pixels || msaa));
        unsigned queries = 0;
        for (const auto& instruction : block) {
            queries += instruction.GetOpcode() == IR::Opcode::ImageQueryDimensions;
        }
        assert(queries == unsigned(msaa ? !pixels : pixels));
    }
    // Integer loads and size queries keep exact logical views without inserting
    // SRT reads/buffer bindings. This is what made the previous prototype reject
    // existing community recipes when it changed their resource layouts.
    for (bool query : {false,true}) {
        Info info{};info.flattened_ud_buf.resize(16);
        auto image=AmdGpu::Image::Null(false);image.base_address=1;
        image.width=1919;image.height=1079;
        Common::ObjectPool<IR::Inst> pool;IR::Block block(pool);IR::IREmitter ir(block);
        const auto coords=ir.CompositeConstruct(ir.Imm32(123u),ir.Imm32(87u),ir.Imm32(0u),ir.Imm32(0u));
        const auto operation=query ? ir.ImageQueryDimension(ir.Imm32(0u),ir.Imm32(0u),ir.Imm1(false),{})
                                   : ir.ImageRead(ir.Imm32(0u),coords,{},{},{});
        Optimization::ResourceDiscovery usage{.user=operation.Inst()};
        usage.sharps[0].num_dwords=8;
        std::array<u32,8> words;std::memcpy(words.data(),&image,sizeof(image));
        for(u32 i=0;i<8;++i) usage.sharps[0].dwords[i]=ir.Imm32(words[i]);
        Optimization::ResourcePatchingPass(info,{usage},Profile{});
        assert(info.images.size()==1 && !info.images[0].needs_native && info.images[0].needs_exact_size);
        assert(info.buffers.empty());
    }
    // A speculative resource snapshot may be incomplete. Rejection must reach
    // the worker's recovery handler rather than terminate through noexcept.
    bool rejected = false;
    try {
        Common::ScopedSpeculativeCompilation speculative;
        auto image = AmdGpu::Image::Null(false);
        image.data_format = u64(AmdGpu::DataFormat::Format32);
        image.num_format = u64(AmdGpu::NumberFormat::SnormNz);
        (void)image.GetNumberConversion();
    } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    std::puts("Pixel sample coordinates: 8 instruction/sampler/MSAA combinations and logical loads/queries PASS; speculative rejection PASS");
    std::printf("Catalogue ABI: %016llx\n", static_cast<unsigned long long>(Vulkan::ShaderCatalogABI()));
}
