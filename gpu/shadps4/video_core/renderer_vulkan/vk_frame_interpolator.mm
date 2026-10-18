// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_frame_interpolator.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vulkan/vulkan_metal.h>

#if __has_include(<MetalFX/MTLFXFrameInterpolator.h>)
#define GOW3_FRAME_INTERPOLATION_API 1
#else
#define GOW3_FRAME_INTERPOLATION_API 0
#endif

namespace Vulkan {
struct FrameInterpolator::Impl {
    VkDevice device{};
    PFN_vkCreateSemaphore create{};
    PFN_vkDestroySemaphore destroy{};
    PFN_vkExportMetalObjectsEXT export_objects{};
    id<MTLDevice> metal;
    id<MTLCommandQueue> queue;
    id<MTLSharedEvent> ready, done;
#if GOW3_FRAME_INTERPOLATION_API
    id<MTLFXFrameInterpolator> effect API_AVAILABLE(macos(26.0));
#endif
    VkSemaphore ready_vk{}, done_vk{};
    unsigned width{}, height{}, output_width{}, output_height{};
    MTLPixelFormat color_format{}, depth_format{}, motion_format{}, output_format{}, ui_format{};
    uint64_t value{};
    bool configured{}, force_reset{true};
    unsigned history_frames{};
    std::atomic<bool> error{};
    const char* problem = "MetalFX frame interpolation requires the macOS 26 SDK";

    id<MTLTexture> Texture(VkImage image, VkImageView view = VK_NULL_HANDLE) {
        if ((!image && !view) || !export_objects)
            return nil;
        VkExportMetalTextureInfoEXT texture{VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT};
        texture.image = image;
        texture.imageView = view;
        texture.plane = VK_IMAGE_ASPECT_PLANE_0_BIT;
        VkExportMetalObjectsInfoEXT objects{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT};
        objects.pNext = &texture;
        export_objects(device, &objects);
        return texture.mtlTexture;
    }
    bool Import(id<MTLSharedEvent> event, VkSemaphore& semaphore) {
        VkImportMetalSharedEventInfoEXT import{
            VK_STRUCTURE_TYPE_IMPORT_METAL_SHARED_EVENT_INFO_EXT};
        import.mtlSharedEvent = event;
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.pNext = &import;
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        ci.pNext = &type;
        return create(device, &ci, nullptr, &semaphore) == VK_SUCCESS;
    }
};

FrameInterpolator::FrameInterpolator(VkInstance instance, VkDevice device,
                                     PFN_vkGetInstanceProcAddr proc)
    : impl{std::make_shared<Impl>()} {
    @autoreleasepool {
#if GOW3_FRAME_INTERPOLATION_API
        auto& p = *impl;
        if (@available(macOS 26.0, *)) {
            p.problem = "MetalFX frame interpolation unavailable on this GPU";
            p.device = device;
            auto gdpa =
                reinterpret_cast<PFN_vkGetDeviceProcAddr>(proc(instance, "vkGetDeviceProcAddr"));
            if (!gdpa)
                return;
            p.export_objects = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(
                gdpa(device, "vkExportMetalObjectsEXT"));
            p.create = reinterpret_cast<PFN_vkCreateSemaphore>(gdpa(device, "vkCreateSemaphore"));
            p.destroy =
                reinterpret_cast<PFN_vkDestroySemaphore>(gdpa(device, "vkDestroySemaphore"));
            if (!p.export_objects || !p.create || !p.destroy)
                return;
            VkExportMetalDeviceInfoEXT dev{VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT};
            VkExportMetalObjectsInfoEXT objects{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT};
            objects.pNext = &dev;
            p.export_objects(device, &objects);
            p.metal = dev.mtlDevice;
            // Dynamic class lookup avoids a strong reference when linking older SDK
            // stubs.
            Class cls = NSClassFromString(@"MTLFXFrameInterpolatorDescriptor");
            if (!cls || !p.metal || ![cls supportsDevice:p.metal])
                return;
            p.queue = [p.metal newCommandQueue];
            p.queue.label = @"God of War MetalFX interpolation";
            p.ready = [p.metal newSharedEvent];
            p.done = [p.metal newSharedEvent];
            if (!p.queue || !p.ready || !p.done || !p.Import(p.ready, p.ready_vk) ||
                !p.Import(p.done, p.done_vk))
                return;
            p.problem = nullptr;
        } else
            p.problem = "MetalFX frame interpolation requires macOS 26";
#else
        (void)instance;
        (void)device;
        (void)proc;
#endif
    }
}
FrameInterpolator::~FrameInterpolator() {
    if (impl->destroy) {
        if (impl->ready_vk)
            impl->destroy(impl->device, impl->ready_vk, nullptr);
        if (impl->done_vk)
            impl->destroy(impl->device, impl->done_vk, nullptr);
    }
}
bool FrameInterpolator::Available() const { return !impl->problem && !impl->error; }
const char* FrameInterpolator::Problem() const {
    return impl->error ? "MetalFX frame interpolation GPU failure" : impl->problem;
}
VkSemaphore FrameInterpolator::ReadySemaphore() const { return impl->ready_vk; }
VkSemaphore FrameInterpolator::DoneSemaphore() const { return impl->done_vk; }
unsigned long long FrameInterpolator::NextValue() { return ++impl->value; }
void FrameInterpolator::ResetHistory(){impl->force_reset=true;impl->history_frames=0;}
bool FrameInterpolator::OutputInterpolated() const {
    return Available() && !impl->force_reset && impl->history_frames >= 3;
}

bool FrameInterpolator::Configure(unsigned w, unsigned h, unsigned ow, unsigned oh,
                                  const Inputs& in, VkImage output) {
    @autoreleasepool {
#if GOW3_FRAME_INTERPOLATION_API
        if (@available(macOS 26.0, *)) {
            auto& p = *impl;
            if (!Available() || !w || !h || !ow || !oh)
                return false;
            auto c = p.Texture({}, in.color), d = p.Texture({}, in.depth),
                 m = p.Texture({}, in.motion), o = p.Texture(output), u = p.Texture({}, in.ui);
            if (!c || !d || !m || !o || c.width != ow || c.height != oh || d.width != w ||
                d.height != h || m.width != w || m.height != h || o.width != ow || o.height != oh ||
                o.storageMode != MTLStorageModePrivate || (u && (u.width != ow || u.height != oh)))
                return false;
            const auto uf = u ? u.pixelFormat : MTLPixelFormatInvalid;
            if (p.configured && p.width == w && p.height == h && p.output_width == ow &&
                p.output_height == oh && p.color_format == c.pixelFormat &&
                p.depth_format == d.pixelFormat && p.motion_format == m.pixelFormat &&
                p.output_format == o.pixelFormat && p.ui_format == uf)
                return true;
            auto* desc = (MTLFXFrameInterpolatorDescriptor*)[NSClassFromString(
                @"MTLFXFrameInterpolatorDescriptor") new];
            desc.inputWidth = w;
            desc.inputHeight = h;
            desc.outputWidth = ow;
            desc.outputHeight = oh;
            desc.colorTextureFormat = c.pixelFormat;
            desc.depthTextureFormat = d.pixelFormat;
            desc.motionTextureFormat = m.pixelFormat;
            desc.outputTextureFormat = o.pixelFormat;
            desc.uiTextureFormat = uf;
            p.effect = [desc newFrameInterpolatorWithDevice:p.metal];
            if (!p.effect)
                return false;
            p.width = w;
            p.height = h;
            p.output_width = ow;
            p.output_height = oh;
            p.color_format = c.pixelFormat;
            p.depth_format = d.pixelFormat;
            p.motion_format = m.pixelFormat;
            p.output_format = o.pixelFormat;
            p.ui_format = uf;
            p.configured = true;
            p.force_reset = true;
            p.history_frames = 0;
            std::printf("MetalFX frame interpolation: configured depth/motion=%ux%u "
                        "color/output=%ux%u UI=%d\n",
                        w, h, ow, oh, bool(u));
            return true;
        }
#endif
        return false;
    }
}

bool FrameInterpolator::Encode(const Inputs& in, VkImage output, const Parameters& param,
                               unsigned long long value) {
    @autoreleasepool {
#if GOW3_FRAME_INTERPOLATION_API
        if (@available(macOS 26.0, *)) {
            auto& p = *impl;
            if (!Available() || !p.configured || !std::isfinite(param.delta_seconds) ||
                param.delta_seconds <= 0 || param.delta_seconds > 0.25f ||
                !std::isfinite(param.near_plane) || !std::isfinite(param.far_plane) ||
                param.near_plane <= 0 || param.far_plane <= param.near_plane ||
                !std::isfinite(param.vertical_fov_degrees) || param.vertical_fov_degrees <= 0 ||
                param.vertical_fov_degrees >= 180 || !std::isfinite(param.aspect) ||
                param.aspect <= 0 || !std::isfinite(param.motion_scale_x) ||
                !std::isfinite(param.motion_scale_y))
                return false;
            auto c = p.Texture({}, in.color), prev = p.Texture({}, in.previous_color),
                 d = p.Texture({}, in.depth), m = p.Texture({}, in.motion), o = p.Texture(output),
                 u = p.Texture({}, in.ui);
            const bool reset = p.force_reset || param.reset;
            auto matches = [](id<MTLTexture> t, MTLPixelFormat f, unsigned w, unsigned h,
                              MTLTextureUsage usage) {
                return t && t.pixelFormat == f && t.width == w && t.height == h &&
                       (t.usage & usage) == usage;
            };
            if (!matches(c, p.color_format, p.output_width, p.output_height,
                         p.effect.colorTextureUsage) ||
                (!reset && !matches(prev, p.color_format, p.output_width, p.output_height,
                                    p.effect.colorTextureUsage)) ||
                !matches(d, p.depth_format, p.width, p.height, p.effect.depthTextureUsage) ||
                !matches(m, p.motion_format, p.width, p.height, p.effect.motionTextureUsage) ||
                !matches(o, p.output_format, p.output_width, p.output_height,
                         p.effect.outputTextureUsage) ||
                (p.ui_format != MTLPixelFormatInvalid &&
                 !matches(u, p.ui_format, p.output_width, p.output_height,
                          p.effect.uiTextureUsage)))
                return false;
            p.effect.colorTexture = c;
            p.effect.prevColorTexture = reset ? c : prev;
            p.effect.depthTexture = d;
            p.effect.motionTexture = m;
            p.effect.outputTexture = o;
            p.effect.uiTexture = u;
            p.effect.uiTextureComposited = param.composited_ui;
            p.effect.motionVectorScaleX = param.motion_scale_x;
            p.effect.motionVectorScaleY = param.motion_scale_y;
            p.effect.deltaTime = param.delta_seconds;
            p.effect.nearPlane = param.near_plane;
            p.effect.farPlane = param.far_plane;
            p.effect.fieldOfView = param.vertical_fov_degrees;
            p.effect.aspectRatio = param.aspect;
            p.effect.depthReversed = param.reversed_depth;
            p.effect.jitterOffsetX = 0;
            p.effect.jitterOffsetY = 0;
            p.effect.shouldResetHistory = reset;
            auto cb = [p.queue commandBuffer];
            if (!cb)
                return false;
            static const bool profile = [] {
                const char* v = std::getenv("GOW3_FRAMEGEN_TIMING");
                return v && v[0] == '1';
            }();
            if (profile) {
                // Profiling only: exclude upstream Vulkan work from the interpolation
                // duration.
                auto wait = [p.queue commandBuffer];
                [wait encodeWaitForEvent:p.ready value:value];
                [wait commit];
            }
            cb.label = @"MetalFX generated intermediate frame";
            if (!profile)
                [cb encodeWaitForEvent:p.ready value:value];
            [p.effect encodeToCommandBuffer:cb];
            [cb encodeSignalEvent:p.done value:value];
            auto state = impl;
            [cb addCompletedHandler:^(id<MTLCommandBuffer> completed) {
              if (profile && value % 60 == 0 && completed.status == MTLCommandBufferStatusCompleted)
                  std::printf("Framegen GPU work: %.3f ms (isolated profiling; "
                              "excludes upstream wait)\n",
                              (completed.GPUEndTime - completed.GPUStartTime) * 1000);
              if (completed.status == MTLCommandBufferStatusError) {
                  state->error = true;
                  std::fprintf(stderr, "MetalFX frame interpolation: GPU error: %s\n",
                               completed.error.description.UTF8String);
                  if (state->done.signaledValue < value)
                      state->done.signaledValue = value;
              }
            }];
            [cb commit];
            p.force_reset = false;
            p.history_frames = reset ? 1 : p.history_frames + 1;
            return true;
        }
#endif
        return false;
    }
}
} // namespace Vulkan
