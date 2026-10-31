// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <future>
#include "shader_recompiler/frontend/fetch_shader.h"
#include "common/logging/log.h"

extern "C" { uint64_t runtime_disabled_optimizations = 0; }
void assert_fail_impl() { std::abort(); }
[[noreturn]] void unreachable_impl() { std::abort(); }
namespace Common::Log {
bool ShouldLog(Level) { return false; }
void Write(std::string_view, Level, const char*, int, const char*, std::string&&) {}
}
namespace Shader {
// A compiler snapshot must never call the walker or follow guest resource pointers.
void CallSrtWalker(PFN_SrtWalker, const u32*, u32*) { std::abort(); }
}

int main() {
    using namespace Shader::Gcn;
    // s_load_dwordx4 s[8:11], s[2:3], 0; tbuffer_load_format_xyzw; s_setpc_b64.
    std::array<u32, 4> code{0xC0000000u | (2u << 22) | (8u << 15) | (1u << 9) | (1u << 8),
                            0xE8000000u | (3u << 16), 2u << 16, 0xBE802000u};
    AmdGpu::Buffer buffer{};
    buffer.base_address = 0x12340;
    buffer.stride = 16;
    std::array<u32, 4> ud{};
    const auto* code_pointer = code.data();
    const auto* buffer_pointer = &buffer;
    std::memcpy(ud.data(), &code_pointer, sizeof(code_pointer));
    std::memcpy(ud.data() + 2, &buffer_pointer, sizeof(buffer_pointer));
    Shader::Info info;
    info.has_fetch_shader = true;
    info.fetch_shader_sgpr_base = 0;
    info.user_data = ud;
    FetchShaderSnapshot snapshot;
    snapshot.Capture(info);
    assert(snapshot.fetch && snapshot.fetch->attributes.size() == 1);
    assert(snapshot.buffers[0].base_address == 0x12340);

    // A queued draw overrides mutable members with an older effective snapshot.
    // Pipeline workers must retain that draw even after both inputs advance.
    auto draw_ud = ud;
    std::array<u32,4> draw_flat{17,23,31,47};
    info.flattened_ud_buf = {99,99,99,99};
    info.pgm_base = 0x9999;
    Shader::Info::ud_snapshots[0] = {&info,draw_ud.data(),u32(draw_ud.size()),
                                    draw_flat.data(),u32(draw_flat.size()),0x1234};
    Shader::Info::num_ud_snapshots = 1;
    Shader::Info owned;
    std::vector<u32> owned_ud;
    FetchShaderSnapshot queued;
    queued.CapturePipeline(info,owned,owned_ud);
    Shader::Info::num_ud_snapshots = 0;
    draw_ud.fill(0); draw_flat.fill(0);
    assert(owned.pgm_base == 0x1234);
    assert(owned.flattened_ud_buf == std::vector<u32>({17,23,31,47}));
    {
        ScopedFetchShaderSnapshot scope{queued};
        assert(ParseFetchShader(owned)->attributes[0].GetSharp(owned).base_address == 0x12340);
    }

    // Both guest pointers are invalid after capture. A compiler worker must not dereference them.
    ud.fill(0);
    auto worker = std::async(std::launch::async, [&] {
        ScopedFetchShaderSnapshot scope{snapshot};
        const auto fetch = ParseFetchShader(info);
        assert(fetch && fetch->attributes.size() == 1);
        const auto sharp = fetch->attributes[0].GetSharp(info);
        assert(sharp.base_address == 0x12340 && sharp.GetStride() == 16);
        FetchShaderSnapshot nested;
        nested.info = &info;
        {
            ScopedFetchShaderSnapshot inner{nested};
            assert(!ParseFetchShader(info));
        }
        assert(ParseFetchShader(info));
    });
    worker.get();
    info.has_fetch_shader = false;
    assert(!ParseFetchShader(info));

#ifndef ARCH_X86_64
    std::array<u32, 4> flat{11, 22, 33, 44};
    std::array<u8, 4> walker{1, 2, 3, 4};
    std::array<u8, 4> different_walker{1, 2, 3, 5};
    Shader::Info captured;
    captured.user_data = {}; // no guest memory remains accessible
    captured.srt_info.flattened_bufsize_dw = flat.size();
    captured.srt_info.walker_func = {walker.data(), true};
    captured.srt_info.walker_func_size = walker.size();
    captured.compilation_flat_snapshot = Shader::Info::CompilationSnapshot{flat, captured.srt_info};
    captured.RefreshFlatBuf();
    assert(std::equal(flat.begin(), flat.end(), captured.flattened_ud_buf.begin()));
    // Equal sizes are insufficient: reject a different resource-walk layout too.
    captured.srt_info.walker_func = {different_walker.data(), true};
    bool rejected = false;
    try { captured.RefreshFlatBuf(); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    captured.srt_info.walker_func = {walker.data(), true};
    ++captured.srt_info.flattened_bufsize_dw;
    rejected = false;
    try { captured.RefreshFlatBuf(); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
#endif
}
