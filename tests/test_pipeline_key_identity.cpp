// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>
#include <utility>
#include <tsl/robin_map.h>
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"

int main() {
    using Key = Vulkan::GraphicsPipelineKey;
    tsl::robin_map<Key, unsigned> cache;
    for (unsigned i = 1; i <= 4096; ++i) {
        Key key;
        key.stage_hashes[0] = i;
        key.stage_hashes[3] = i * 7919;
        key.num_color_attachments = 1;
        key.num_samples = key.depth_samples = 1;
        key.mrt_mask = 1;
        // The current serialized layout ends with two unused bytes after the
        // bit fields. Nonzero padding must survive: it participates in the
        // existing byte-wise hash/equality and may arrive from a cache file.
        auto* bytes = reinterpret_cast<unsigned char*>(&key);
        bytes[sizeof(Key) - 2] = static_cast<unsigned char>(i);
        bytes[sizeof(Key) - 1] = static_cast<unsigned char>(i >> 3);
        assert(key.depth_clip_enable == 0 && key.prim_type == AmdGpu::PrimitiveType{});
        assert(key.polygon_mode == AmdGpu::PolygonMode{} && key.depth_clamp_enable == 0);
        Key copied = key;
        Key assigned;
        assigned = copied;
        Key moved = std::move(assigned);
        assert(std::memcmp(&key, &copied, sizeof(Key)) == 0);
        assert(std::memcmp(&key, &moved, sizeof(Key)) == 0);
        assert(std::hash<Key>{}(key) == std::hash<Key>{}(moved));
        assert(cache.emplace(moved, i).second);
        const auto found = cache.find(key);
        assert(found != cache.end() && found->second == i);
    }
    // Robin-hood collisions, displacement and rehash must retain every key.
    cache.reserve(32768);
    for (const auto& entry : cache) {
        Key copied = entry.first;
        const auto found = cache.find(copied);
        assert(found != cache.end() && found->second == entry.second);
    }
    assert(cache.size() == 4096);
    std::puts("pipeline-key-identity-test: OK (4096 keys, copy/move/displacement/rehash)");
}
