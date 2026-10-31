// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/amdgpu/regs.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>

int main() {
    using namespace AmdGpu;
    Regs source{}, replay{};
    source.SetDefaults();
    replay.SetDefaults();
    RegDirty dirty;
    RegDelta delta;
    std::mt19937 random{1234};
    // Replay packets with repeated/overlapping writes, empty submissions, ClearState,
    // and writes across bitmap-word boundaries. Compare the entire register file.
    for (u32 submission = 0; submission < 2000; ++submission) {
        dirty.Clear();
        if (submission % 17 == 0) {
            source.SetDefaults();
            dirty.reset = true;
        }
        for (u32 packet = 0, n = random() % 25; packet < n; ++packet) {
            const u32 start = random() % Regs::NumRegs;
            const u32 count = std::min<u32>(random() % 200, Regs::NumRegs - start);
            for (u32 i = 0; i < count; ++i) source.reg_array[start + i] = random();
            dirty.Mark(start, count);
        }
        delta.Capture(source, dirty);
        assert(std::is_sorted(delta.blocks.begin(), delta.blocks.end()));
        assert(std::adjacent_find(delta.blocks.begin(), delta.blocks.end()) == delta.blocks.end());
        delta.Apply(replay);
        assert(source.reg_array == replay.reg_array);
    }
    dirty.Clear();
    dirty.Mark(Regs::NumRegs, 10);
    dirty.Mark(0, 0);
    u32 count = 0;
    dirty.ForEachBlock([&](u32) { ++count; });
    assert(count == 0);
    dirty.Mark(Regs::NumRegs - 1, std::numeric_limits<u32>::max());
    dirty.ForEachBlock([&](u32 b) { assert(b == RegDirty::NumBlocks - 1); ++count; });
    assert(count == 1);
    dirty.Clear();
    dirty.Mark(0, Regs::NumRegs);
    count = 0;
    dirty.ForEachBlock([&](u32 b) { assert(b == count++); });
    assert(count == RegDirty::NumBlocks);
    std::puts("PASS: sparse register replay matches full state across 2,000 submissions");
}
