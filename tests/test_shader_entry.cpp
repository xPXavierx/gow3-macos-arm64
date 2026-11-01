// SPDX-License-Identifier: GPL-2.0-or-later
// Call the production compiler, including its required/speculative error policy.
#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>
#include "shader_recompiler/profile.h"
#include "shader_recompiler/recompiler.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/renderer_vulkan/vk_async_compiler.h"

int main(int argc, char**) {
    Shader::Pools pools;
    Shader::Info info{};
    info.hw_stage = Shader::HwStage::Compute;
    info.sw_stage = Shader::SwStage::Compute;
    info.pgm_hash = 0x1234;
    Shader::RuntimeInfo runtime{};
    runtime.Initialize(info.hw_stage, info.sw_stage);
    Shader::Profile profile{};
    if (argc > 1) {
        // Separate process: must exit 23 through the production assertion policy.
        (void)Shader::TranslateProgram({}, pools, info, runtime, profile);
        return 99;
    }
    Vulkan::AsyncCompiler compiler(1);
    for (const bool nonnull : {false, true}) {
        Vulkan::AsyncJob<bool> invalid;
        const std::array<u32, 1> storage{0};
        const std::span<const u32> empty = nonnull ? std::span{storage}.first(0) : std::span<const u32>{};
        assert(invalid.Start(compiler, [&] {
            Common::ScopedSpeculativeCompilation speculative;
            (void)Shader::TranslateProgram(empty, pools, info, runtime, profile);
            return true;
        }));
        invalid.Wait();
        assert(!invalid.Poll() && invalid.Failed());
        assert(invalid.Error().find("Speculative") != std::string::npos);
    }
    Vulkan::AsyncJob<bool> valid;
    assert(valid.Start(compiler, [&] {
        assert(!Common::ScopedSpeculativeCompilation::Active());
        // s_mov_b32 vcc_hi, 0; s_endpgm. A real valid stream on the reused worker.
        const std::array<u32, 3> code{0xBEEB03FF, 0, 0xBF810000};
        const auto program = Shader::TranslateProgram(code, pools, info, runtime, profile);
        return !program.blocks.empty() && !program.syntax_list.empty();
    }));
    valid.Wait();
    assert(valid.Poll() == true && !valid.Failed());
    std::puts("PASS: empty/null shader snapshots rejected; reused worker compiles valid shader");
}
