// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/renderer_vulkan/vk_frame_interpolator.h"
#include "video_core/texture_cache/image.h"
#include <atomic>
#include "gow3_runtime_control.h"
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
class SceneTargets;
namespace FrameGen {
inline bool Requested() {
    // Neither legacy settings nor environment variables may revive the experiment.
    return false;
}
inline bool CompactMotion() {
    static const bool compact = [] {
        const char* value = std::getenv("GOW3_FRAMEGEN_COMPACT_MOTION");
        return !value || value[0] != '0';
    }();
    return Requested() && compact;
}
inline std::atomic<u64> scene_depth_address{}, copy_depth_address{};
inline std::atomic<bool> scene_open{};
inline bool GeometryDepth(u64 address) {
    return Requested() && address &&
           (address == scene_depth_address.load() ||
            (scene_open.load() && address == copy_depth_address.load()));
}
} // namespace FrameGen
// Capture depth/motion before the HUD depth clear, and final scene color from
// the current attachment immediately before HUD geometry. Motion comes
// from actual previous/current vertex clip positions, not fabricated camera
// data.
class FrameGenInputs {
  public:
    FrameGenInputs(const Instance&, Scheduler&, Runtime&, VideoCore::TextureCache&, ObjectMotion&, SceneTargets* targets=nullptr);
    ~FrameGenInputs();
    void Projection(VideoCore::ImageId depth, u64 original_address, u64 copied_address,
                    const float* coefficients);
    void Display(VideoCore::ImageId color);
    void BeforeDepthClear(u64 address);
    bool BeforeUiDraw(VideoCore::ImageId);
    bool NeedsColor() const {return captured && !color_finalized;}
    bool Ready(u64 presented_address) const;
    const char* PendingReason(u64 presented_address) const {
        if (!captured) return "missing depth/motion snapshot";
        if (!color_finalized) return "missing final scene color";
        if (captured_address != presented_address) return "display buffer mismatch";
        return "ready";
    }
    FrameInterpolator::Inputs Inputs(vk::ImageView ui) const;
    vk::ImageView PrepareUI(vk::ImageView source, vk::CommandBuffer cmd, vk::Extent2D source_size = {});
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
    void Consumer(vk::Semaphore done, u64 value) {consumer_done=done;consumer_value=value;}
    void InvalidateHistory() { captured=previous_valid=projection_seen=color_finalized=false; FrameGen::scene_open=false; last_capture={}; }

  private:
    void Allocate(vk::Extent2D size);
    void Snapshot();
    SceneTargets* targets{};
    const Instance& instance;
    Scheduler& scheduler;
    Runtime& runtime;
    VideoCore::TextureCache& cache;
    ObjectMotion& objects;
    VideoCore::ImageId depth_id{}, display_id{};
    bool projection_seen{}, captured{}, previous_valid{}, color_finalized{};
    u64 captured_address{};
    vk::Semaphore consumer_done{};
    u64 consumer_value{};
    unsigned slot{}, frames{};
    FrameInterpolator::Parameters parameters{};
    std::chrono::steady_clock::time_point last_capture{};
    vk::Extent2D extent{}, source_extent{};
    vk::Extent2D temporal_extent{};
    std::array<VideoCore::UniqueImage, 2> color;
    std::array<vk::UniqueImageView, 2> color_view;
    VideoCore::UniqueImage depth, motion, ui;
    vk::UniqueImageView depth_view, motion_view, ui_view;
    vk::UniqueDescriptorSetLayout descriptors;
    vk::UniquePipelineLayout layout;
    vk::UniquePipeline pipeline, temporal_pipeline, ui_pipeline;
};
} // namespace Vulkan
