// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_framegen_inputs.h"
#include <algorithm>
#include "video_core/host_shaders/framegen_inputs_comp.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_object_motion.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/texture_cache/texture_cache.h"
#include <cmath>
#include <cstdio>
#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#endif
namespace Vulkan {
namespace {
constexpr vk::ImageSubresourceRange Range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
void Barrier(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout old, vk::AccessFlags2 access) {
    const vk::ImageMemoryBarrier2 b{.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                                    .srcAccessMask = vk::AccessFlagBits2::eMemoryRead |
                                                     vk::AccessFlagBits2::eMemoryWrite,
                                    .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                                    .dstAccessMask = access,
                                    .oldLayout = old,
                                    .newLayout = vk::ImageLayout::eGeneral,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = image,
                                    .subresourceRange = Range};
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
}
} // namespace
FrameGenInputs::FrameGenInputs(const Instance& i, Scheduler& s, Runtime& r,
                               VideoCore::TextureCache& c, ObjectMotion& o)
    : instance{i}, scheduler{s}, runtime{r}, cache{c}, objects{o} {
    const auto device = instance.GetDevice();
    std::array<vk::DescriptorSetLayoutBinding, 6> bindings{};
    for (u32 n = 0; n < 6; ++n)
        bindings[n] = {.binding = n,
                       .descriptorType = n < 3 ? vk::DescriptorType::eSampledImage
                                               : vk::DescriptorType::eStorageImage,
                       .descriptorCount = 1,
                       .stageFlags = vk::ShaderStageFlagBits::eCompute};
    descriptors = Check(device.createDescriptorSetLayoutUnique(
        {.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
         .bindingCount = 6,
         .pBindings = bindings.data()}));
    layout = Check(
        device.createPipelineLayoutUnique({.setLayoutCount = 1, .pSetLayouts = &*descriptors}));
    auto module = CompileSPV(FRAMEGEN_INPUTS_COMP, device);
    pipeline = Check(device.createComputePipelineUnique(
        {},
        {.stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = module, .pName = "main"},
         .layout = *layout}));
    device.destroyShaderModule(module);
    objects.OnFrameStart();
}
FrameGenInputs::~FrameGenInputs() = default;
void FrameGenInputs::Projection(VideoCore::ImageId id, u64 original, u64 copied, const float* p) {
    if (!id || !original || !copied)
        return;
    auto& info = cache.GetImage(id).info;
    if (info.size.width < 640 || info.size.height < 360)
        return;
    for (unsigned i = 0; i < 4; ++i)
        if (!std::isfinite(p[i]))
            return;
    const float near = p[3] / (p[2] + 1), far = p[3] / (p[2] - 1);
    const float aspect = float(info.size.width) / info.size.height;
    if (p[0] <= 0 || p[1] <= 0 || p[2] <= 1 || near <= 0 || far <= near || far > 1000000 ||
        std::abs(p[0] / p[1] - aspect) > .03f)
        return;
    parameters.near_plane = near;
    parameters.far_plane = far;
    parameters.vertical_fov_degrees = 2.f * std::atan(2.f * p[1]) * 180.f / 3.14159265359f;
    parameters.aspect = aspect;
    parameters.reversed_depth = false;
    parameters.composited_ui = true;
    depth_id = id;
    projection_seen = true;
    FrameGen::scene_depth_address = original;
    FrameGen::copy_depth_address = copied;
    FrameGen::scene_open = true;
}
void FrameGenInputs::Display(VideoCore::ImageId id) {
    if (id)
        display_id = id;
}
void FrameGenInputs::BeforeDepthClear(u64 address) {
    if (projection_seen && !captured && display_id &&
        address == FrameGen::copy_depth_address.load())
        Snapshot();
}
void FrameGenInputs::Allocate(vk::Extent2D size) {
    if (size == extent)
        return;
    scheduler.Finish();
    auto cmd = scheduler.CommandBuffer();
    const auto device = instance.GetDevice();
    static const unsigned divisor = [] {
        const char* v = std::getenv("GOW3_FRAMEGEN_INPUT_DIVISOR");
        return v && v[0] == '2' ? 2u : 1u;
    }();
    temporal_extent = {(size.width + divisor - 1) / divisor, (size.height + divisor - 1) / divisor};
    parameters.motion_scale_x = float(temporal_extent.width) / size.width;
    parameters.motion_scale_y = float(temporal_extent.height) / size.height;
    auto make = [&](VideoCore::UniqueImage& image, vk::UniqueImageView& view, vk::Format format,
                    vk::Extent2D target) {
        view.reset();
        image = VideoCore::UniqueImage(device, instance.GetAllocator());
        image.Create({.imageType = vk::ImageType::e2D,
                      .format = format,
                      .extent = {target.width, target.height, 1},
                      .mipLevels = 1,
                      .arrayLayers = 1,
                      .samples = vk::SampleCountFlagBits::e1,
                      .tiling = vk::ImageTiling::eOptimal,
                      .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
                               vk::ImageUsageFlagBits::eTransferSrc});
        vk::ImageViewCreateInfo ci{.image = vk::Image(image),
                                   .viewType = vk::ImageViewType::e2D,
                                   .format = format,
                                   .subresourceRange = Range};
#ifdef __APPLE__
        VkExportMetalObjectCreateInfoEXT export_info{
            VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
        export_info.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;
        ci.pNext = &export_info;
#endif
        view = Check(device.createImageViewUnique(ci));
        Barrier(cmd, vk::Image(image), vk::ImageLayout::eUndefined,
                vk::AccessFlagBits2::eShaderWrite);
    };
    make(color[0], color_view[0], vk::Format::eR8G8B8A8Unorm, size);
    make(color[1], color_view[1], vk::Format::eR8G8B8A8Unorm, size);
    make(depth, depth_view, vk::Format::eR32Sfloat, temporal_extent);
    make(motion, motion_view, vk::Format::eR16G16Sfloat, temporal_extent);
    extent = size;
    previous_valid = false;
}
void FrameGenInputs::Snapshot() {
    auto& c = cache.GetImage(display_id);
    auto& d = cache.GetImage(depth_id);
    const auto format=c.info.pixel_format;
    if(format!=vk::Format::eR8G8B8A8Unorm && format!=vk::Format::eR8G8B8A8Srgb &&
       format!=vk::Format::eB8G8R8A8Unorm && format!=vk::Format::eB8G8R8A8Srgb)return;
    const vk::Extent2D size{c.info.size.width, c.info.size.height};
    if (size.width != d.info.size.width || size.height != d.info.size.height ||
        !objects.Image(size.width, size.height))
        return;
    scheduler.EndRendering();
    Allocate(size);
    auto cmd = scheduler.CommandBuffer();
    VideoCore::ImageViewInfo ci{};
    ci.format = vk::Format::eB8G8R8A8Unorm;
    ci.mapping.a = vk::ComponentSwizzle::eOne;
    VideoCore::ImageViewInfo di{};
    di.format = vk::Format::eR32Sfloat;
    const auto cv = *c.FindView(ci).image_view, dv = *d.FindView(di).image_view;
    runtime.Transit(&c, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderRead);
    runtime.Transit(&d, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderRead);
    runtime.FlushBarriers();
    bool valid = false;
    auto mv = objects.PrepareRead(cmd, size.width, size.height, valid);
    if (!valid)
        return;
    for (auto image : {vk::Image(color[slot]), vk::Image(depth), vk::Image(motion)})
        Barrier(cmd, image, vk::ImageLayout::eGeneral, vk::AccessFlagBits2::eShaderWrite);
    const std::array<vk::DescriptorImageInfo, 6> images = {
        {{{}, cv, vk::ImageLayout::eGeneral},
         {{}, dv, vk::ImageLayout::eGeneral},
         {{}, mv, vk::ImageLayout::eGeneral},
         {{}, *color_view[slot], vk::ImageLayout::eGeneral},
         {{}, *depth_view, vk::ImageLayout::eGeneral},
         {{}, *motion_view, vk::ImageLayout::eGeneral}}};
    std::array<vk::WriteDescriptorSet, 6> writes{};
    for (u32 n = 0; n < 6; ++n)
        writes[n] = {.dstBinding = n,
                     .descriptorCount = 1,
                     .descriptorType = n < 3 ? vk::DescriptorType::eSampledImage
                                             : vk::DescriptorType::eStorageImage,
                     .pImageInfo = &images[n]};
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *layout, 0, writes);
    cmd.dispatch((size.width + 7) / 8, (size.height + 7) / 8, 1);
    for (auto image : {vk::Image(color[slot]), vk::Image(depth), vk::Image(motion)})
        Barrier(cmd, image, vk::ImageLayout::eGeneral, vk::AccessFlagBits2::eShaderRead);
    const auto now=std::chrono::steady_clock::now();
    const float delta=last_capture.time_since_epoch().count()?std::chrono::duration<float>(now-last_capture).count():1.f/60;
    if(delta<=0 || delta>.25f)previous_valid=false;
    parameters.delta_seconds=std::clamp(delta,1.f/240,.25f);last_capture=now;
    captured = true;
    captured_address = c.info.guest_address;
    FrameGen::scene_open = false;
    static bool reported = false;
    if (!reported) {
        std::printf("Framegen inputs: captured pre-HUD %ux%u projection near=%.6g "
                    "far=%.6g fov=%.6g vertices=actual\n",
                    size.width, size.height, parameters.near_plane, parameters.far_plane,
                    parameters.vertical_fov_degrees);
        reported = true;
    }
}
bool FrameGenInputs::Ready(u64 address) const { return captured && captured_address == address; }
FrameInterpolator::Inputs FrameGenInputs::Inputs(vk::ImageView ui) const {
    return {*color_view[slot], previous_valid ? *color_view[slot ^ 1] : *color_view[slot],
            *depth_view, *motion_view, ui};
}
FrameInterpolator::Parameters FrameGenInputs::Parameters() const {
    auto p = parameters;
    p.reset = !previous_valid;
    return p;
}
void FrameGenInputs::FinishFrame() {
    previous_valid = captured;
    slot ^= 1;
    captured = false;
    projection_seen = false;
    FrameGen::scene_open = false;
    ++frames;
    objects.OnFrameStart();
}
} // namespace Vulkan
