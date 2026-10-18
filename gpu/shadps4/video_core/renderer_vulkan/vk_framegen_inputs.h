// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/renderer_vulkan/vk_frame_interpolator.h"
#include "video_core/texture_cache/image.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
namespace VideoCore {
class TextureCache;
}
namespace Vulkan {
class Instance;
class Scheduler;
class Runtime;
class ObjectMotion;
namespace FrameGen {
inline bool Requested() {
#ifdef __APPLE__
    static const bool on = [] {
        const char* p = std::getenv("GOW3_FRAMEGEN");
        return p && p[0] == '1';
    }();
    return on;
#else
    return false;
#endif
}
inline std::atomic<u64> scene_depth_address{}, copy_depth_address{};
inline std::atomic<bool> scene_open{};
inline bool GeometryDepth(u64 address) {
    return Requested() && address &&
           (address == scene_depth_address.load() ||
            (scene_open.load() && address == copy_depth_address.load()));
}
} // namespace FrameGen
// Capture the GoW scene before the game clears depth for its HUD. Motion comes
// from actual previous/current vertex clip positions, not fabricated camera
// data.
class FrameGenInputs {
  public:
    FrameGenInputs(const Instance&, Scheduler&, Runtime&, VideoCore::TextureCache&, ObjectMotion&);
    ~FrameGenInputs();
    void Projection(VideoCore::ImageId depth, u64 original_address, u64 copied_address,
                    const float* coefficients);
    void Display(VideoCore::ImageId color);
    void BeforeDepthClear(u64 address);
    bool Ready(u64 presented_address) const;
    FrameInterpolator::Inputs Inputs(vk::ImageView ui) const;
    FrameInterpolator::Parameters Parameters() const;
    vk::Extent2D Size() const { return extent; }
    vk::Extent2D TemporalSize() const { return temporal_extent; }
    vk::Image ColorImage() const { return vk::Image(color[slot]); }
    vk::Image PreviousColorImage() const {
        return vk::Image(color[previous_valid ? slot ^ 1 : slot]);
    }
    vk::Image DepthImage() const { return vk::Image(depth); }
    vk::Image MotionImage() const { return vk::Image(motion); }
    void FinishFrame();

  private:
    void Allocate(vk::Extent2D size);
    void Snapshot();
    const Instance& instance;
    Scheduler& scheduler;
    Runtime& runtime;
    VideoCore::TextureCache& cache;
    ObjectMotion& objects;
    VideoCore::ImageId depth_id{}, display_id{};
    bool projection_seen{}, captured{}, previous_valid{};
    u64 captured_address{};
    unsigned slot{}, frames{};
    FrameInterpolator::Parameters parameters{};
    std::chrono::steady_clock::time_point last_capture{};
    vk::Extent2D extent{};
    vk::Extent2D temporal_extent{};
    std::array<VideoCore::UniqueImage, 2> color;
    std::array<vk::UniqueImageView, 2> color_view;
    VideoCore::UniqueImage depth, motion;
    vk::UniqueImageView depth_view, motion_view;
    vk::UniqueDescriptorSetLayout descriptors;
    vk::UniquePipelineLayout layout;
    vk::UniquePipeline pipeline;
};
} // namespace Vulkan
