// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_framegen_inputs.h"
#include <algorithm>
#include "video_core/host_shaders/framegen_inputs_comp.h"
#include "video_core/host_shaders/framegen_inputs_compact_comp.h"
#include "video_core/host_shaders/framegen_temporal_comp.h"
#include "video_core/host_shaders/framegen_temporal_compact_comp.h"
#include "video_core/host_shaders/framegen_ui_comp.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_object_motion.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scene_resolution.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/texture_cache/texture_cache.h"
#include <cmath>
#include <cstdio>
#include <fstream>
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
                               VideoCore::TextureCache& c, ObjectMotion& o, SceneTargets* t)
    : targets{t}, instance{i}, scheduler{s}, runtime{r}, cache{c}, objects{o} {
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
    auto module = FrameGen::CompactMotion() ? CompileSPV(FRAMEGEN_INPUTS_COMPACT_COMP, device)
                                         : CompileSPV(FRAMEGEN_INPUTS_COMP, device);
    pipeline = Check(device.createComputePipelineUnique(
        {},
        {.stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = module, .pName = "main"},
         .layout = *layout}));
    device.destroyShaderModule(module);
    module = FrameGen::CompactMotion() ? CompileSPV(FRAMEGEN_TEMPORAL_COMPACT_COMP, device)
                                      : CompileSPV(FRAMEGEN_TEMPORAL_COMP, device);
    temporal_pipeline = Check(device.createComputePipelineUnique(
        {}, {.stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = module, .pName = "main"},
             .layout = *layout}));
    device.destroyShaderModule(module);
    module = CompileSPV(FRAMEGEN_UI_COMP, device);
    ui_pipeline = Check(device.createComputePipelineUnique(
        {}, {.stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = module, .pName = "main"},
             .layout = *layout}));
    device.destroyShaderModule(module);
    SetObjectName(device, *pipeline, "Framegen legacy color and temporal capture");
    SetObjectName(device, *temporal_pipeline, "Framegen temporal capture");
    SetObjectName(device, *ui_pipeline, "Framegen final scene and UI capture");
    objects.OnFrameStart();
}
FrameGenInputs::~FrameGenInputs() = default;
void FrameGenInputs::Projection(VideoCore::ImageId id, u64 original, u64 copied, const float* p) {
    if (!FrameGen::Requested()) return;
    if (!id || !original || !copied)
        return;
    auto& info = cache.GetImage(id).info;
    if (info.size.width < 640 || info.size.height < 360)
        return;
    for (unsigned i = 0; i < 4; ++i)
        if (!std::isfinite(p[i]))
            return;
    if(std::getenv("GOW3_FRAMEGEN_DIAGNOSTICS")) {static unsigned count=0;if(count++<8)std::printf("FG diagnostic: projection %g %g %g %g depth=%ux%u\n",p[0],p[1],p[2],p[3],info.size.width,info.size.height);}
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
    if (id) {
        display_id = id;
        // Some scenes copy to VideoOut before the HUD depth clear, others
        // after HUD composition. Snapshot's earlier display_id can therefore
        // still name the previous rotating buffer. Bind the finished scene to
        // the display attachment observed after its color capture. Ready keeps
        // checking this address against the actual flip; it never accepts an
        // arbitrary next buffer or reuses a snapshot across FinishFrame.
        if (captured && color_finalized)
            captured_address = cache.GetImage(id).info.guest_address;
    }
}
void FrameGenInputs::BeforeDepthClear(u64 address) {
    if(std::getenv("GOW3_FRAMEGEN_DIAGNOSTICS")) {static unsigned count=0;if(projection_seen && count++<8)std::printf("FG diagnostic: clear captured=%d display=%d match=%d\n",captured,bool(display_id),address==FrameGen::copy_depth_address.load());}
    if (projection_seen && !captured && display_id &&
        address == FrameGen::copy_depth_address.load())
        Snapshot();
}
void FrameGenInputs::Allocate(vk::Extent2D size) {
    auto wanted=size;
    if(Gow3Runtime::Enabled()) {const auto [w,h]=Gow3Runtime::RenderSize(Gow3Runtime::Applied());wanted={w,h};}
    const unsigned auxiliary_divisor=[] {
        if(const auto* path=std::getenv("GOW3_FRAMEGEN_TEMPORAL_CONTROL")) {
            unsigned value=2;std::ifstream control(path);control>>value;
            if(value==1 || value==2 || value==4 || value==8)return value;
        }
        return 2u;
    }();
    const vk::Extent2D wanted_temporal{(wanted.width+auxiliary_divisor-1)/auxiliary_divisor,(wanted.height+auxiliary_divisor-1)/auxiliary_divisor};
    if (size == source_extent && (!Gow3Runtime::Enabled() || (wanted==extent && wanted_temporal==temporal_extent))) return;
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
    source_extent = size;
    // Experimental: keep real rendering unchanged, reduce only interpolation
    // color/output. Depth and motion retain their original calibrated size.
    static const unsigned color_divisor = [] {
        const char* value = std::getenv("GOW3_FRAMEGEN_COLOR_DIVISOR");
        return value && value[0] == '2' ? 2u : 1u;
    }();
    size = {(size.width + color_divisor - 1) / color_divisor,
            (size.height + color_divisor - 1) / color_divisor};
    if(Gow3Runtime::Enabled()) {
        size=wanted;temporal_extent=wanted_temporal;
        parameters.motion_scale_x=float(temporal_extent.width)/source_extent.width;
        parameters.motion_scale_y=float(temporal_extent.height)/source_extent.height;
    }
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
    make(ui, ui_view, vk::Format::eR8G8B8A8Unorm, size);
    extent = size;
    previous_valid = false;
    std::printf("Framegen resources: real=%ux%u interpolation=%ux%u depth/motion=%ux%u compact_motion=%d\n",
                source_extent.width, source_extent.height, extent.width, extent.height,
                temporal_extent.width, temporal_extent.height, FrameGen::CompactMotion());
}
void FrameGenInputs::Snapshot() {
    // Color is finalized later, before HUD geometry. Only temporal inputs need
    // preserving before this depth clear. Keep an opt-in local A/B control.
    const bool temporal_only = [] {
        if (const auto* path=std::getenv("GOW3_FRAMEGEN_INPUT_CONTROL")) {
            std::ifstream control(path); char mode='1'; control>>mode; return mode!='0';
        }
        return true;
    }();
    auto& c = cache.GetImage(display_id);
    auto& d = cache.GetImage(depth_id);
    const auto format=c.info.pixel_format;
    if(format!=vk::Format::eR8G8B8A8Unorm && format!=vk::Format::eR8G8B8A8Srgb &&
       format!=vk::Format::eB8G8R8A8Unorm && format!=vk::Format::eB8G8R8A8Srgb)return;
    const vk::Extent2D size{c.info.size.width, c.info.size.height};
    if(std::getenv("GOW3_FRAMEGEN_DIAGNOSTICS")) {static unsigned count=0;if(count++<8)std::printf("FG diagnostic: snapshot color=%ux%u depth=%ux%u motion=%d\n",size.width,size.height,d.info.size.width,d.info.size.height,bool(objects.Image(size.width,size.height)));}
    auto motion_size=size;
    if(Gow3Runtime::Enabled()) {const auto [w,h]=Gow3Runtime::RenderSize(Gow3Runtime::Applied());motion_size={w,h};}
    if(!objects.Image(motion_size.width,motion_size.height)) motion_size=size;
    if (size.width != d.info.size.width || size.height != d.info.size.height ||
        !objects.Image(motion_size.width,motion_size.height)) return;
    scheduler.EndRendering();
    // All next-frame geometry can run while the previous interpolation reads
    // these packed inputs. Fence only their reuse, not that geometry submit.
    if(consumer_value) {
        if(instance.GetDevice().getSemaphoreCounterValue(consumer_done).value<consumer_value) {
            scheduler.Flush();scheduler.WaitExternal(consumer_done,consumer_value);
        }
        consumer_value=0;
    }
    Allocate(size);
    auto cmd = scheduler.CommandBuffer();
    VideoCore::ImageViewInfo ci{};
    ci.format = vk::Format::eB8G8R8A8Unorm;
    ci.mapping.a = vk::ComponentSwizzle::eOne;
    VideoCore::ImageViewInfo di{};
    di.format = vk::Format::eR32Sfloat;
    // Capture physical scene targets directly. Exporting the logical guest
    // image here used to resolve the entire scene and then reduce it again.
    auto sample=[&](VideoCore::Image& image,const VideoCore::ImageViewInfo& view) {
        if(targets) if(auto proxy=targets->SampleProxy(image,view)) {
            targets->PrepareSample(image,0,vk::ImageLayout::eGeneral);return proxy->view;
        }
        runtime.Transit(&image,vk::ImageLayout::eGeneral,vk::PipelineStageFlagBits2::eComputeShader,
                        vk::AccessFlagBits2::eShaderRead);
        return *image.FindView(view).image_view;
    };
    const auto cv=temporal_only?vk::ImageView{}:sample(c,ci),dv=sample(d,di);
    runtime.FlushBarriers();
    bool valid = false;
    auto mv = objects.PrepareRead(cmd, motion_size.width, motion_size.height, valid);
    parameters.motion_scale_x=float(temporal_extent.width)/motion_size.width;
    parameters.motion_scale_y=float(temporal_extent.height)/motion_size.height;
    if (!valid)
        return;
    if(!temporal_only)
        Barrier(cmd, vk::Image(color[slot]), vk::ImageLayout::eGeneral, vk::AccessFlagBits2::eShaderWrite);
    for (auto image : {vk::Image(depth), vk::Image(motion)})
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
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, temporal_only?*temporal_pipeline:*pipeline);
    if(temporal_only) {
        const std::array selected{writes[1],writes[2],writes[4],writes[5]};
        cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *layout, 0, selected);
    } else cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *layout, 0, writes);
    const auto dispatch_extent=temporal_only?temporal_extent:vk::Extent2D{std::max(extent.width,temporal_extent.width),std::max(extent.height,temporal_extent.height)};
    cmd.dispatch((dispatch_extent.width + 7) / 8, (dispatch_extent.height + 7) / 8, 1);
    if(!temporal_only)
        Barrier(cmd, vk::Image(color[slot]), vk::ImageLayout::eGeneral, vk::AccessFlagBits2::eShaderRead);
    for (auto image : {vk::Image(depth), vk::Image(motion)})
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
bool FrameGenInputs::BeforeUiDraw(VideoCore::ImageId id) {
    if(!captured || color_finalized || !id)return false;
    auto& image=cache.GetImage(id);
    const auto format=image.info.pixel_format;
    if(format!=vk::Format::eR8G8B8A8Unorm && format!=vk::Format::eR8G8B8A8Srgb &&
       format!=vk::Format::eB8G8R8A8Unorm && format!=vk::Format::eB8G8R8A8Srgb)return false;
    if(image.info.size.width!=source_extent.width || image.info.size.height!=source_extent.height) {
        if(std::getenv("GOW3_FRAMEGEN_DIAGNOSTICS")) {static unsigned n=0;if(n++<8)std::printf("FG color boundary rejected: source=%ux%u expected=%ux%u address=%llx\n",image.info.size.width,image.info.size.height,source_extent.width,source_extent.height,(unsigned long long)image.info.guest_address);}
        return false;
    }
    if(std::getenv("GOW3_FRAMEGEN_DIAGNOSTICS")) {static unsigned n=0;if(n++<4)std::printf("FG final scene source: address=%llx format=%s\n",(unsigned long long)image.info.guest_address,vk::to_string(image.info.pixel_format).c_str());}
    scheduler.EndRendering();auto cmd=scheduler.CommandBuffer();
    VideoCore::ImageViewInfo view{};view.format=vk::Format::eB8G8R8A8Unorm;view.mapping.a=vk::ComponentSwizzle::eOne;
    vk::ImageView source;
    if(targets)if(auto proxy=targets->SampleProxy(image,view)) {
        source=proxy->view;targets->PrepareSample(image,0,vk::ImageLayout::eGeneral);
    }
    if(!source) {
        runtime.Transit(&image,vk::ImageLayout::eGeneral,vk::PipelineStageFlagBits2::eComputeShader,vk::AccessFlagBits2::eShaderRead);
        source=*image.FindView(view).image_view;
    }
    runtime.FlushBarriers();
    Barrier(cmd,vk::Image(color[slot]),vk::ImageLayout::eGeneral,vk::AccessFlagBits2::eShaderWrite);
    const std::array<vk::DescriptorImageInfo,2> images{{
        {{},source,vk::ImageLayout::eGeneral},{{},*color_view[slot],vk::ImageLayout::eGeneral}}};
    const std::array<vk::WriteDescriptorSet,2> writes{{
        {.dstBinding=0,.descriptorCount=1,.descriptorType=vk::DescriptorType::eSampledImage,.pImageInfo=&images[0]},
        {.dstBinding=3,.descriptorCount=1,.descriptorType=vk::DescriptorType::eStorageImage,.pImageInfo=&images[1]}}};
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*ui_pipeline);
    cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,*layout,0,writes);
    cmd.dispatch((extent.width+7)/8,(extent.height+7)/8,1);
    Barrier(cmd,vk::Image(color[slot]),vk::ImageLayout::eGeneral,vk::AccessFlagBits2::eShaderRead);
    color_finalized=true;return true;
}
bool FrameGenInputs::Ready(u64 address) const {
    return captured && color_finalized && captured_address == address;
}
FrameInterpolator::Inputs FrameGenInputs::Inputs(vk::ImageView ui) const {
    return {*color_view[slot], previous_valid ? *color_view[slot ^ 1] : *color_view[slot],
            *depth_view, *motion_view, ui};
}
vk::ImageView FrameGenInputs::PrepareUI(vk::ImageView source, vk::CommandBuffer cmd, vk::Extent2D physical) {
    if(!physical.width)physical=source_extent;
    if(physical==extent)return source;

    Barrier(cmd, vk::Image(ui), vk::ImageLayout::eGeneral, vk::AccessFlagBits2::eShaderWrite);
    const std::array<vk::DescriptorImageInfo,2> images{{
        {{}, source, vk::ImageLayout::eGeneral}, {{}, *ui_view, vk::ImageLayout::eGeneral}}};
    const std::array<vk::WriteDescriptorSet,2> writes{{
        {.dstBinding=0,.descriptorCount=1,.descriptorType=vk::DescriptorType::eSampledImage,.pImageInfo=&images[0]},
        {.dstBinding=3,.descriptorCount=1,.descriptorType=vk::DescriptorType::eStorageImage,.pImageInfo=&images[1]}}};
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *ui_pipeline);
    cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,*layout,0,writes);
    cmd.dispatch((extent.width+7)/8,(extent.height+7)/8,1);
    Barrier(cmd,vk::Image(ui),vk::ImageLayout::eGeneral,vk::AccessFlagBits2::eShaderRead);
    return *ui_view;
}
FrameInterpolator::Parameters FrameGenInputs::Parameters() const {
    auto p = parameters;
    p.reset = !previous_valid;
    return p;
}
void FrameGenInputs::FinishFrame() {
    previous_valid = captured && color_finalized;
    slot ^= 1;
    captured = color_finalized = false;
    projection_seen = false;
    FrameGen::scene_open = false;
    ++frames;
    objects.OnFrameStart();
}
} // namespace Vulkan
