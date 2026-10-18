// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include "video_core/texture_cache/image.h"
#include "video_core/texture_cache/image_view.h"
#include "video_core/texture_cache/replacement_data.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"

namespace VideoCore {
class TextureReplacements {
public:
    TextureReplacements(const Vulkan::Instance&, Vulkan::Runtime&);
    ~TextureReplacements();
    bool Enabled() const { return !packs.empty(); }
    bool PollControl();
    void ObserveUpload(Image&);
    void Forget(u64 uid);
    std::optional<vk::ImageView> Sample(Image&, const ImageViewInfo&, bool native);
private:
    struct Entry {
        ReplacementData::Pack data;
        std::string name,path;
        bool failed=false;
        u64 hash{};
        UniqueImage image;
        std::vector<std::pair<ImageViewInfo,vk::UniqueImageView>> views;
    };
    struct Source { const void* backing; Entry* pack; u64 alias_generation; u64 revision; };
    struct Pending {
        u64 uid,revision,tick;
        u32 width,height,block,layers;
        Vulkan::StagingBufferRef staging;
    };
    bool Retire();
    bool Upload(Entry&);
    const Vulkan::Instance& instance;
    Vulkan::Runtime& runtime;
    std::vector<std::unique_ptr<Entry>> packs;
    std::unordered_multimap<u64,Entry*> by_hash;
    std::unordered_set<u64> dimensions;
    u64 resident_bytes=0;
    std::string control;
    bool active=true;
    u32 control_tick=0;
    // DeleteImage can run on the guest thread. Vulkan resources remain owned by packs.
    std::mutex mutex;
    std::unordered_map<u64,Source> sources;
    std::vector<Pending> pending;
    u64 revision=0,pending_bytes=0;
};
}
