// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace VideoCore {
// A backing-view copy cannot rely on a guest read fault to observe a readback.
// Drain the recorder before waiting (the wait may flush its command buffer).
// A generation change also catches a readback published AND retired during the
// copy, when checking only the currently pending ranges would miss it.
template <typename Generation, typename Blocked, typename Resolve, typename Copy>
void CopyWithReadbackDependencies(Generation&& generation, Blocked&& blocked,
                                  Resolve&& resolve, Copy&& copy) {
    for (bool retry = false;; retry = true) {
        const auto before = generation();
        if (retry || blocked()) resolve();
        copy();
        if (generation() == before) return;
    }
}
} // namespace VideoCore
