// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the actual SPIR-V backend without opening a window or loading the game.
// Output is validated by spirv-val (see docs/upscaler.md).
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <setjmp.h>

int main(int argc, char** argv) {
    using namespace Shader;
    const std::filesystem::path dir = argc > 1 ? argv[1] : ".";
    std::filesystem::create_directories(dir);
    std::unique_ptr<Vulkan::Instance> instance;
    vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic dispatch;
    if (argc > 2 && std::strcmp(argv[2], "--gpu") == 0) {
        instance =
            std::make_unique<Vulkan::Instance>(0, std::getenv("GOW3_VK_VALIDATION") != nullptr);
        dispatch.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
        dispatch.init(instance->GetInstance());
        dispatch.init(instance->GetDevice());
    }
    for (bool vertex : {true, false}) {
        for (bool motion : {false, true}) {
            Info info{};
            info.hw_stage = vertex ? HwStage::Vertex : HwStage::Fragment;
            info.sw_stage = vertex ? SwStage::Vertex : SwStage::Fragment;
            RuntimeInfo runtime{};
            runtime.Initialize(info.hw_stage, info.sw_stage);
            if (vertex) {
                runtime.hw.vs.motion_vectors = motion;
            } else {
                runtime.hw.fs.motion_vectors = motion;
                runtime.hw.fs.color_buffers[0].num_format = AmdGpu::NumberFormat::Float;
            }
            Common::ObjectPool<IR::Inst> pool;
            IR::Block block(pool);
            IR::IREmitter ir(block);
            const auto output = vertex ? IR::Attribute::Position0 : IR::Attribute::RenderTarget0;
            for (u32 component = 0; component < 4; ++component) {
                info.stores.Set(output, component);
                if (!vertex && component < 2) {
                    info.loads.Set(IR::Attribute::FragCoord, component);
                    ir.SetAttribute(output, ir.GetAttribute(IR::Attribute::FragCoord, component),
                                    component);
                } else {
                    ir.SetAttribute(output, ir.Imm32(component == 3 ? 1.0f : 0.0f), component);
                }
            }
            ir.Epilogue();
            IR::Program program(info);
            program.blocks.push_back(&block);
            program.syntax_list.push_back(
                {.data = {.block = &block}, .type = IR::AbstractSyntaxNode::Type::Block});
            program.syntax_list.push_back({.type = IR::AbstractSyntaxNode::Type::Return});
            Profile profile{};
            profile.supported_spirv = 0x00010600;
            profile.support_int64 = true;
            Backend::Bindings bindings{};
            const auto code = Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
            if (instance) {
                const auto [result, module] = instance->GetDevice().createShaderModuleUnique(
                    {.codeSize = code.size() * sizeof(u32), .pCode = code.data()}, nullptr,
                    dispatch);
                if (result != vk::Result::eSuccess)
                    return 2;
            }
            const auto path = dir / (std::string(vertex ? "vertex" : "fragment") +
                                     (motion ? "-motion.spv" : "-plain.spv"));
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(code.data()), code.size() * sizeof(u32));
            if (!out) {
                return 1;
            }
        }
    }
}
