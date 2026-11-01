// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/assert.h"
#include "common/types.h"
#include "../../../../src/runtime_store.h"

namespace AmdGpu {
// A failed backing attempt must not have written a prefix (native runtime contract).
template <typename BackingWrite>
void WriteFenceLabel(void* address, u64 value, u32 size, BackingWrite&& backing_write) {
    ASSERT_MSG(size == 4 || size == 8, "Invalid fence label size: {}", size);
    if (!backing_write(address, &value, size)) {
        runtime_store_once(address, &value, size);
    }
}
} // namespace AmdGpu
