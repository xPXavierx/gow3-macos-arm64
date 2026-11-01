// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "video_core/texture_cache/readback_copy.h"

int main() {
    std::uint64_t generation = 0;
    int resident = 7, snapshot = 0, resolves = 0;
    bool pending = false;
    auto version = [&] { return generation; };
    auto blocked = [&] { return pending; };
    auto resolve = [&] { ++resolves; if (pending) resident = 42; pending = false; };
    auto copy = [&] { snapshot = resident; };
    // Stale mode: no read protection means the prior published bytes may be copied.
    VideoCore::CopyWithReadbackDependencies(version, blocked, resolve, copy);
    assert(snapshot == 7 && resolves == 0);
    // Precise mode: the actual dependency is resolved before copying.
    pending = true;
    generation = 1;
    VideoCore::CopyWithReadbackDependencies(version, blocked, resolve, copy);
    assert(snapshot == 42 && !pending && resolves == 1);
    // Producer publishes AND retires while copying: checking pending alone would
    // miss the update and leave the old snapshot. The production helper retries.
    resident = 7;
    bool raced = false;
    VideoCore::CopyWithReadbackDependencies(version, blocked, resolve, [&] {
        snapshot = resident;
        if (!raced) { raced = true; resident = 99; ++generation; }
    });
    // Resolve represents draining the recorder and waiting for its current bytes.
    assert(raced && snapshot == resident && snapshot == 99 && resolves == 2);
    // A newly pending copy during the first copy also cannot escape the dependency.
    resident = 7;
    raced = false;
    VideoCore::CopyWithReadbackDependencies(version, blocked, resolve, [&] {
        snapshot = resident;
        if (!raced) { raced = true; pending = true; ++generation; }
    });
    assert(snapshot == 42 && !pending && resolves == 3);
    std::puts("PASS: backing snapshots preserve pending and already-retired readback dependencies");
}
