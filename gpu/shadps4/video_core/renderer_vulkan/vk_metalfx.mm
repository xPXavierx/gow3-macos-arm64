// SPDX-License-Identifier: GPL-2.0-or-later
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#include "video_core/renderer_vulkan/vk_metalfx.h"
#include <vulkan/vulkan_metal.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace Vulkan {
struct MetalFx::Impl {
    VkDevice device{};
    PFN_vkCreateSemaphore create{};
    PFN_vkDestroySemaphore destroy{};
    PFN_vkExportMetalObjectsEXT export_objects{};
    id<MTLDevice> metal;
    id<MTLCommandQueue> queue;
    id<MTLSharedEvent> ready, done;
    id<MTLFXSpatialScaler> spatial;
    VkSemaphore ready_vk{}, done_vk{};
    uint64_t value{};
    unsigned width{}, height{}, output_width{}, output_height{};
    MTLPixelFormat color_format{}, output_format{};
    std::atomic<bool> error{};
    bool timing = std::getenv("GOW3_METALFX_TIMING") != nullptr;
    std::mutex timing_mutex;
    double timing_sum{};
    unsigned timing_count{};

    id<MTLTexture> Texture(VkImage image, VkImageView view = VK_NULL_HANDLE) {
        if ((!image && !view) || !export_objects) return nil;
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
        VkImportMetalSharedEventInfoEXT import{VK_STRUCTURE_TYPE_IMPORT_METAL_SHARED_EVENT_INFO_EXT};
        import.mtlSharedEvent = event;
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.pNext = &import;
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        ci.pNext = &type;
        return create(device, &ci, nullptr, &semaphore) == VK_SUCCESS;
    }

};

MetalFx::MetalFx(VkInstance instance, VkDevice device, PFN_vkGetInstanceProcAddr proc)
    : impl{std::make_shared<Impl>()} { @autoreleasepool {
    auto& p = *impl;
    p.device = device;
    auto gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(proc(instance, "vkGetDeviceProcAddr"));
    if (!gdpa) return;
    p.export_objects = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(gdpa(device, "vkExportMetalObjectsEXT"));
    p.create = reinterpret_cast<PFN_vkCreateSemaphore>(gdpa(device, "vkCreateSemaphore"));
    p.destroy = reinterpret_cast<PFN_vkDestroySemaphore>(gdpa(device, "vkDestroySemaphore"));
    if (!p.export_objects || !p.create || !p.destroy) return;
    VkExportMetalDeviceInfoEXT device_info{VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT};
    VkExportMetalObjectsInfoEXT objects{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT};
    objects.pNext = &device_info;
    p.export_objects(device, &objects);
    p.metal = device_info.mtlDevice;
    if (!p.metal || ![MTLFXSpatialScalerDescriptor supportsDevice:p.metal]) return;
    p.queue = [p.metal newCommandQueue];
    p.queue.label = @"God of War MetalFX";
    p.ready = [p.metal newSharedEvent];
    p.done = [p.metal newSharedEvent];
    if (!p.queue || !p.ready || !p.done || !p.Import(p.ready, p.ready_vk) ||
        !p.Import(p.done, p.done_vk)) return;
    Class fg = NSClassFromString(@"MTLFXFrameInterpolatorDescriptor");
    const bool framegen = fg && [fg respondsToSelector:@selector(supportsDevice:)] &&
        ((BOOL(*)(id,SEL,id))[fg methodForSelector:@selector(supportsDevice:)])
            (fg, @selector(supportsDevice:), p.metal);
    printf("MetalFX: device=%s temporal=%d spatial=1 frame_interpolation_supported=%d (interpolator logs its own activation)\n",
           p.metal.name.UTF8String, [MTLFXTemporalScalerDescriptor supportsDevice:p.metal], framegen);
    if (p.timing) printf("MetalFX: profiling-only separate wait/work/signal command buffers\n");
}}
MetalFx::~MetalFx() {
    // The owner finishes its Vulkan scheduler (which waits on done) first.
    if (impl->destroy) {
        if (impl->ready_vk) impl->destroy(impl->device, impl->ready_vk, nullptr);
        if (impl->done_vk) impl->destroy(impl->device, impl->done_vk, nullptr);
    }
}
bool MetalFx::Available() const { return impl->ready_vk && impl->done_vk && !impl->error; }
VkSemaphore MetalFx::ReadySemaphore() const { return impl->ready_vk; }
VkSemaphore MetalFx::DoneSemaphore() const { return impl->done_vk; }
unsigned long long MetalFx::NextValue() { return ++impl->value; }

bool MetalFx::Configure(unsigned w, unsigned h, unsigned ow, unsigned oh,
                        VkImageView color, VkImage output) { @autoreleasepool {
    if (!Available() || !w || !h || w > ow || h > oh) return false;
    auto& p = *impl;
    auto ct = p.Texture(VK_NULL_HANDLE, color), ot = p.Texture(output);
    if (!ct || !ot) return false;
    const auto format = ct.pixelFormat;
    // The caller supplies an encoded (UNORM) view. Creating another view here
    // can discard the native swizzle MoltenVK uses for RGBA/BGRA reinterpretation.
    if (format == MTLPixelFormatRGBA8Unorm_sRGB || format == MTLPixelFormatBGRA8Unorm_sRGB)
        return false;
    if (p.width == w && p.height == h && p.output_width == ow && p.output_height == oh &&
        p.color_format == format && p.output_format == ot.pixelFormat && p.spatial) return true;
    auto* descriptor = [MTLFXSpatialScalerDescriptor new];
    descriptor.inputWidth = w; descriptor.inputHeight = h;
    descriptor.outputWidth = ow; descriptor.outputHeight = oh;
    descriptor.colorTextureFormat = format;
    descriptor.outputTextureFormat = ot.pixelFormat;
    descriptor.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
    p.spatial = [descriptor newSpatialScalerWithDevice:p.metal];
    if (!p.spatial) {
        fprintf(stderr, "MetalFX: spatial scaler creation failed input_format=%lu output_format=%lu\n",
                (unsigned long)format, (unsigned long)ot.pixelFormat);
        return false;
    }
    p.width = w; p.height = h; p.output_width = ow; p.output_height = oh;
    p.color_format = format; p.output_format = ot.pixelFormat;
    printf("MetalFX: configured spatial %ux%u -> %ux%u input_format=%lu output_format=%lu\n",
           w, h, ow, oh, (unsigned long)format, (unsigned long)ot.pixelFormat);
    return true;
}}

bool MetalFx::Encode(VkImageView color, VkImage output, unsigned long long value) { @autoreleasepool {
    auto& p = *impl;
    auto ct = p.Texture(VK_NULL_HANDLE, color), ot = p.Texture(output);
    if (!Available() || !ct || !ot || !p.spatial) return false;
    if (!ct || (ct.usage & p.spatial.colorTextureUsage) != p.spatial.colorTextureUsage ||
        (ot.usage & p.spatial.outputTextureUsage) != p.spatial.outputTextureUsage) return false;
    auto cb = [p.queue commandBuffer];
    if (!cb) return false;
    cb.label = @"MetalFX spatial upscale (no frame generation)";
    if (p.timing) {
        // Command-buffer GPU times include event waits. Isolate the encoder in
        // its own in-order queue submission for profiling, rather than reporting
        // the input rendering wait as MetalFX work. Production uses one buffer.
        auto wait = [p.queue commandBuffer];
        if (!wait) return false;
        [wait encodeWaitForEvent:p.ready value:value];
        [wait commit];
    } else {
        [cb encodeWaitForEvent:p.ready value:value];
    }
    p.spatial.colorTexture = ct; p.spatial.outputTexture = ot;
    p.spatial.inputContentWidth = p.width; p.spatial.inputContentHeight = p.height;
    [p.spatial encodeToCommandBuffer:cb];
    if (!p.timing) [cb encodeSignalEvent:p.done value:value];
    // A completed handler may outlive the owning Presenter after the GPU event
    // fires. Retain its state until the handler finishes.
    auto state = impl;
    [cb addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        if (completed.status == MTLCommandBufferStatusError) {
            state->error = true;
            fprintf(stderr, "MetalFX: GPU error: %s\n", completed.error.description.UTF8String);
            // Release the dependent Vulkan submission so the next frame can
            // report the failed encoder rather than waiting forever.
            if (state->done.signaledValue < value) state->done.signaledValue = value;
        }
        if (state->timing && completed.status == MTLCommandBufferStatusCompleted &&
            completed.GPUStartTime > 0 && completed.GPUEndTime > completed.GPUStartTime) {
            std::scoped_lock lock(state->timing_mutex);
            state->timing_sum += (completed.GPUEndTime-completed.GPUStartTime) * 1000;
            if (++state->timing_count == 120) {
                printf("MetalFX GPU: 120 samples average=%.6f ms last_tick=%llu (isolated work command buffer; profiling only)\n",
                       state->timing_sum / 120, value);
                state->timing_sum = 0; state->timing_count = 0;
            }
        }
    }];
    [cb commit];
    if (p.timing) {
        auto signal = [p.queue commandBuffer];
        if (!signal) return false;
        [signal encodeSignalEvent:p.done value:value];
        [signal commit];
    }
    return true;
}}
}
