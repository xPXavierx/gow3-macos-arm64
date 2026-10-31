// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <string_view>
#include "common/assert.h"
#include "common/serdes.h"
#include "video_core/renderer_vulkan/vk_async_compiler.h"

namespace Common::Log {
bool ShouldLog(Level) { return false; }
void Write(std::string_view, Level, const char*, int, const char*, std::string&&) {}
}

int main(int argc, char**) {
    if (argc > 1) ASSERT_MSG(false, "Required draw must remain fatal");
    Vulkan::AsyncCompiler compiler(1);
    Vulkan::AsyncJob<bool> invalid_snapshot, later_work;
    assert(invalid_snapshot.Start(compiler, []() -> bool {
        Common::ScopedSpeculativeCompilation scope;
        ASSERT_MSG(false, "Future vertex descriptor is incomplete");
        return true;
    }));
    invalid_snapshot.Wait();
    assert(!invalid_snapshot.Poll() && invalid_snapshot.Failed());
    assert(invalid_snapshot.Error().find("Speculative") != std::string::npos);
    // Reuse the same worker: unwinding must restore the fatal assertion policy.
    assert(later_work.Start(compiler, [] {
        return !Common::ScopedSpeculativeCompilation::Active();
    }));
    later_work.Wait();
    assert(later_work.Poll() == true);
    assert(!Common::ScopedSpeculativeCompilation::Active());

    // Fetch serialization reserves spare capacity. Description checksums must
    // seal only the bytes actually written, then reject corrupt length fields.
    Serialization::Archive description;
    description.Grow(64);
    Serialization::Writer writer{description};
    writer.Write(u32{42});
    assert(description.TakeWritten().size() == sizeof(u32));
    Serialization::Archive malformed;
    Serialization::Writer invalid{malformed};
    invalid.Write(std::numeric_limits<size_t>::max());
    invalid.Write(u8{0});
    Serialization::Archive input{malformed.TakeWritten()};
    std::vector<u32> elements;
    bool rejected = false;
    try {
        Common::ScopedSpeculativeCompilation scope;
        Serialization::Reader reader{input};
        reader.Read(elements);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected && elements.empty());
}
