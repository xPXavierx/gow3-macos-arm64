#include <cstdint>
#include <cassert>
#include <vector>
#include "video_core/texture_cache/readback_queue.h"

int main() {
    VideoCore::ReadbackQueue<int> queue;
    std::vector<int> published;
    auto complete = [&](auto& entry) {
        if (entry.valid) published.push_back(entry.payload);
    };
    queue.Push(0x1000, 4096, 3, 1);
    queue.Push(0x1000, 4096, 4, 2);
    assert(queue.RequiredTick(0x1fff, 8) == 4);
    assert(queue.RequiredTick(0x2000, 8) == 0);
    queue.Retire([](auto tick) { return tick <= 2; }, complete);
    assert(published.empty());
    queue.Retire([](auto tick) { return tick <= 3; }, complete);
    assert((published == std::vector<int>{1}));
    unsigned canceled = 0;
    queue.Cancel(0x1001, 1, [&](auto&) { ++canceled; });
    queue.Cancel(0x1001, 1, [&](auto&) { ++canceled; });
    assert(canceled == 1);
    queue.Push(0x1000, 4096, 5, 3);
    queue.Retire([](auto) { return true; }, complete);
    assert((published == std::vector<int>{1, 3}));
    assert(queue.Bytes() == 0);
    assert(!queue.CanFit(65ull << 20));
    for (int i = 0; i < 64; ++i) queue.Push(0x1000, 1, 6, i);
    assert(!queue.CanFit(1));
    assert(queue.RequiredTick(0x1ff0, 1) == 6);
    assert(queue.RequiredTick(0x3000, 1) == 0);
    queue.Retire([](auto) { return true; }, [](auto&) {});
    queue.Push(0x1000, 64ull << 20, 7, 4);
    assert(!queue.CanFit(1));
    assert(queue.RequiredTick(0x1000, 0) == 0);
    queue.Retire([](auto) { return true; }, [](auto&) {});
    assert(queue.CanFit(64ull << 20));

    // Byte rule: a CPU write next to a small pending copy, on the same page but outside its
    // bytes, releases that copy's page protection instead of waiting for the GPU.
    std::vector<std::uint64_t> released;
    auto release = [&](auto& entry) { released.push_back(entry.address); };
    queue.Push(0x10000c00, 4, 8, 5);            // 1x1 image
    queue.Push(0x10001000, 4096, 9, 6);         // a page fully covered by a copy
    queue.Push(0x20000000, 1u << 20, 10, 7);    // large copy
    assert(queue.ReleaseOutside(0x10000100, 8, 64 << 10, release) == 1);
    assert((released == std::vector<std::uint64_t>{0x10000c00}));
    assert(queue.RequiredTick(0x10000100, 8) == 0);   // nothing left to wait for there
    assert(queue.RequiredTick(0x10000c00, 4) == 0);   // released copies never block again
    assert(queue.ReleaseOutside(0x10001010, 8, 64 << 10, release) == 0);  // inside the bytes
    assert(queue.RequiredTick(0x10001010, 8) == 9);
    assert(queue.ReleaseOutside(0x20000008, 8, 64 << 10, release) == 0);  // too large
    assert(queue.RequiredTick(0x20000008, 8) == 10);
    unsigned watched = 0, published_released = 0;
    queue.Retire([](auto) { return true; }, [&](auto& entry) {
        watched += entry.watched;
        published_released += entry.valid && !entry.watched;  // still published, no watcher left
    });
    assert(watched == 2 && published_released == 1);

    // A pending copy protects a whole 16 KiB host page. A write in another
    // 4 KiB subpage must release the watcher too, while preserving publication.
    VideoCore::ReadbackQueue<int, 14> apple;
    apple.Push(0x30003000, 4, 11, 42);
    assert(apple.RequiredTick(0x30000100, 8) == 11);
    assert(apple.RequiredTick(0x30004000, 8) == 0);
    unsigned apple_released = 0;
    assert(apple.ReleaseOutside(0x30000100, 8, 64 << 10, [&](auto&) {
        ++apple_released;
    }) == 1);
    assert(apple_released == 1 && apple.RequiredTick(0x30000100, 8) == 0);
    apple.Retire([](auto) { return true; }, [&](auto& entry) {
        assert(entry.valid && !entry.watched && entry.payload == 42);
    });
    assert(apple.Bytes() == 0);

    // Large pending copies are resolved, rather than released, even when the
    // fault touches only a different subpage at the end of the protected range.
    apple.Push(0x40000000, (64 << 10)+4, 12, 43);
    assert(apple.RequiredTick(0x40013000, 8) == 12);
    assert(apple.ReleaseOutside(0x40013000, 8, 64 << 10, [](auto&) {}) == 0);
    apple.Retire([](auto) { return true; }, [](auto& entry) { assert(entry.watched); });
    assert(apple.RequiredTick(0x40013000, 8) == 0);
}
