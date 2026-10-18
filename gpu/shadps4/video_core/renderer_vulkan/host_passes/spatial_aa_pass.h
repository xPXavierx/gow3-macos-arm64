// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan { class Scheduler; }
namespace Vulkan::HostPasses {
// No temporal data or game shader replacement. Input views retain display swizzles.
class SpatialAaPass {
public:
    SpatialAaPass();
    ~SpatialAaPass();
    void Create(vk::Device device, VmaAllocator allocator);
    void Render(vk::CommandBuffer& cmd, Scheduler& scheduler, vk::ImageView perceptual,
                vk::ImageView linear, vk::Extent2D size, int mode);
    vk::Image Image() const { return vk::Image(output); }
    vk::ImageView PerceptualView() const { return *perceptual_output; }
    vk::ImageView LinearView() const { return *linear_output; }
    void PrepareForVulkan(vk::CommandBuffer cmd);
private:
    void Allocate(vk::CommandBuffer& cmd, Scheduler& scheduler, vk::Extent2D size);
    vk::Device device{};
    VmaAllocator allocator{};
    vk::UniqueDescriptorSetLayout descriptors;
    vk::UniquePipelineLayout layout;
    vk::UniqueSampler sampler;
    std::array<vk::UniquePipeline,4> pipelines;
    VideoCore::UniqueImage edges, weights, output, area, search;
    vk::UniqueImageView edge_view, weight_view, perceptual_output, linear_output, area_view, search_view;
    vk::Extent2D extent{};
    bool output_readonly{};
};
}
