// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic motion/depth and native Vulkan -> MetalFX -> Vulkan ordering.
#include "video_core/renderer_vulkan/vk_frame_interpolator.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_metal.h>
int main() {
    setenv("GOW3_METALFX_MODE", "spatial", 1);
    Vulkan::Instance instance(0, std::getenv("GOW3_VK_VALIDATION") != nullptr);
    vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic d;
    auto proc = loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    d.init(proc);
    d.init(instance.GetInstance());
    d.init(instance.GetDevice());
    Vulkan::Scheduler scheduler(instance);
    Vulkan::FrameInterpolator fx(instance.GetInstance(), instance.GetDevice(), proc);
    if (!fx.Available()) {
        std::fprintf(stderr, "Unavailable: %s\n", fx.Problem());
        return 77;
    }
    for (bool lowres : {false, true})
        for (bool bgra : {false, true})
            for (unsigned w : {640u, 768u}) {
                const unsigned h = 360, n = w * h, div = lowres ? 2 : 1;
                const unsigned mw = w / div, mh = h / div;
                const bool composited = w == 768;
                const size_t bytes = n * 4;
                VkBufferCreateInfo bc{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                      .size = bytes * 6,
                                      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                               VK_BUFFER_USAGE_TRANSFER_DST_BIT};
                VmaAllocationCreateInfo ac{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                                                    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                                           .usage = VMA_MEMORY_USAGE_AUTO,
                                           .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
                VkBuffer raw{};
                VmaAllocation allocation{};
                VmaAllocationInfo mapped{};
                assert(vmaCreateBuffer(instance.GetAllocator(), &bc, &ac, &raw, &allocation,
                                       &mapped) == VK_SUCCESS);
                auto* data = (unsigned char*)mapped.pMappedData;
                std::array<VideoCore::UniqueImage, 6> images;
                std::array<vk::UniqueImageView, 5> views;
                const std::array formats{
                    vk::Format::eR8G8B8A8Unorm,
                    vk::Format::eR8G8B8A8Unorm,
                    vk::Format::eR32Sfloat,
                    vk::Format::eR16G16Sfloat,
                    vk::Format::eR8G8B8A8Unorm,
                    bgra ? vk::Format::eB8G8R8A8Unorm : vk::Format::eR8G8B8A8Unorm};
                for (unsigned i = 0; i < 6; ++i) {
                    images[i] =
                        VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
                    images[i].Create(
                        {.imageType = vk::ImageType::e2D,
                         .format = formats[i],
                         .extent = {i == 2 || i == 3 ? mw : w, i == 2 || i == 3 ? mh : h, 1},
                         .mipLevels = 1,
                         .arrayLayers = 1,
                         .samples = vk::SampleCountFlagBits::e1,
                         .tiling = vk::ImageTiling::eOptimal,
                         .usage = vk::ImageUsageFlagBits::eSampled |
                                  vk::ImageUsageFlagBits::eTransferDst |
                                  vk::ImageUsageFlagBits::eTransferSrc |
                                  vk::ImageUsageFlagBits::eColorAttachment});
                    if (i < 5) {
                        VkExportMetalObjectCreateInfoEXT e{
                            VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
                        e.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;
                        auto [result, view] = instance.GetDevice().createImageViewUnique(
                            {.pNext = &e,
                             .image = vk::Image(images[i]),
                             .viewType = vk::ImageViewType::e2D,
                             .format = formats[i],
                             .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
                            nullptr, d);
                        assert(result == vk::Result::eSuccess);
                        views[i] = std::move(view);
                    }
                }
                for (unsigned y = 0; y < h; ++y)
                    for (unsigned x = 0; x < w; ++x) {
                        const unsigned at = y * w + x;
                        for (unsigned j = 0; j < 2; ++j) {
                            auto* p = data + bytes * j + at * 4;
                            const bool object =
                                x >= 24 - j * 8 && x < 56 - j * 8 && y >= 24 && y < 72;
                            p[0] = object ? 240 : 16;
                            p[1] = object ? 24 : 32;
                            p[2] = object ? 16 : 180;
                            p[3] = 255;
                        }
                        const bool current = x >= 24 && x < 56 && y >= 24 && y < 72;
                        ((float*)(data + bytes * 2))[at] = current ? .5f : .9f;
                        ((_Float16*)(data + bytes * 3))[at * 2] = current ? -8 : 0;
                        ((_Float16*)(data + bytes * 3))[at * 2 + 1] = 0;
                        auto* ui = data + bytes * 4 + at * 4;
                        ui[0] = 0;
                        ui[1] = 0;
                        ui[2] = 0;
                        ui[3] = 0;
                        if (composited)
                            std::memcpy(ui, data + at * 4, 4);
                        if (x < 12 && y < 12) {
                            ui[0] = 240;
                            ui[1] = 210;
                            ui[2] = 32;
                            ui[3] = 255;
                        }
                    }
                auto temporal = [&](unsigned left) {
                    for (unsigned y = 0; y < mh; ++y)
                        for (unsigned x = 0; x < mw; ++x) {
                            const auto at = y * mw + x;
                            const bool object = x * div >= left && x * div < left + 32 &&
                                                y * div >= 24 && y * div < 72;
                            reinterpret_cast<float*>(data + bytes * 2)[at] = object ? .5f : .9f;
                            reinterpret_cast<_Float16*>(data + bytes * 3)[at * 2] =
                                object ? -8.f / div : 0;
                            reinterpret_cast<_Float16*>(data + bytes * 3)[at * 2 + 1] = 0;
                        }
                };
                temporal(24);
                auto cmd = scheduler.CommandBuffer();
                std::array<vk::ImageMemoryBarrier2, 6> barriers;
                for (unsigned i = 0; i < 6; ++i) {
                    barriers[i] = {
                        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                        .dstAccessMask = vk::AccessFlagBits2::eMemoryWrite,
                        .oldLayout = vk::ImageLayout::eUndefined,
                        .newLayout = vk::ImageLayout::eGeneral,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = vk::Image(images[i]),
                        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
                }
                cmd.pipelineBarrier2(
                    {.imageMemoryBarrierCount = 6, .pImageMemoryBarriers = barriers.data()}, d);
                for (unsigned i = 0; i < 5; ++i)
                    cmd.copyBufferToImage(
                        raw, vk::Image(images[i]), vk::ImageLayout::eGeneral,
                        vk::BufferImageCopy{
                            .bufferOffset = bytes * i,
                            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                            .imageExtent = {i == 2 || i == 3 ? mw : w, i == 2 || i == 3 ? mh : h,
                                            1}},
                        d);
                for (auto& b : barriers) {
                    b.oldLayout = vk::ImageLayout::eGeneral;
                    b.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
                    b.dstAccessMask =
                        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
                }
                cmd.pipelineBarrier2(
                    {.imageMemoryBarrierCount = 6, .pImageMemoryBarriers = barriers.data()}, d);
                Vulkan::FrameInterpolator::Inputs inputs{*views[0], *views[1], *views[2], *views[3],
                                                         *views[4]};
                assert(fx.Configure(mw, mh, w, h, inputs, vk::Image(images[5])));
                Vulkan::FrameInterpolator::Parameters param{.delta_seconds = 1.f / 60,
                                                            .near_plane = .25f,
                                                            .far_plane = 10000,
                                                            .vertical_fov_degrees = 60,
                                                            .aspect = float(w) / h};
                param.composited_ui = composited;
                auto invalid = param;
                invalid.near_plane = 0;
                assert(!fx.Encode(inputs, vk::Image(images[5]), invalid, fx.NextValue()));
                for (unsigned pass = 0; pass < 3; ++pass) {
                    if (pass == 2) {
                        for (unsigned y = 0; y < h; ++y)
                            for (unsigned x = 0; x < w; ++x) {
                                unsigned at = y * w + x;
                                bool object = x >= 32 && x < 64 && y >= 24 && y < 72;
                                auto* p = data + at * 4;
                                p[0] = object ? 240 : 16;
                                p[1] = object ? 24 : 32;
                                p[2] = object ? 16 : 180;
                                p[3] = 255;
                                ((float*)(data + bytes * 2))[at] = object ? .5f : .9f;
                                ((_Float16*)(data + bytes * 3))[at * 2] = object ? -8 : 0;
                                if (composited && !(x < 12 && y < 12))
                                    std::memcpy(data + bytes * 4 + at * 4, p, 4);
                            }
                        temporal(32);
                        cmd = scheduler.CommandBuffer();
                        for (auto& b : barriers) {
                            b.srcAccessMask = vk::AccessFlagBits2::eMemoryRead |
                                              vk::AccessFlagBits2::eMemoryWrite;
                            b.dstAccessMask = vk::AccessFlagBits2::eTransferRead |
                                              vk::AccessFlagBits2::eTransferWrite;
                        }
                        cmd.pipelineBarrier2(
                            {.imageMemoryBarrierCount = 6, .pImageMemoryBarriers = barriers.data()},
                            d);
                        cmd.copyImage(
                            vk::Image(images[0]), vk::ImageLayout::eGeneral, vk::Image(images[1]),
                            vk::ImageLayout::eGeneral,
                            vk::ImageCopy{
                                .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                .extent = {w, h, 1}},
                            d);
                        vk::MemoryBarrier2 copied{
                            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                            .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
                            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite};
                        cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &copied},
                                             d);
                        for (unsigned i : {0u, 2u, 3u, 4u})
                            cmd.copyBufferToImage(
                                raw, vk::Image(images[i]), vk::ImageLayout::eGeneral,
                                vk::BufferImageCopy{
                                    .bufferOffset = bytes * i,
                                    .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                    .imageExtent = {i == 2 || i == 3 ? mw : w,
                                                    i == 2 || i == 3 ? mh : h, 1}},
                                d);
                        for (auto& b : barriers) {
                            b.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
                            b.dstAccessMask = vk::AccessFlagBits2::eShaderRead |
                                              vk::AccessFlagBits2::eShaderWrite;
                        }
                        cmd.pipelineBarrier2(
                            {.imageMemoryBarrierCount = 6, .pImageMemoryBarriers = barriers.data()},
                            d);
                    }
                    const auto value = fx.NextValue();
                    Vulkan::SubmitInfo ready{};
                    ready.AddSignal(fx.ReadySemaphore(), value);
                    scheduler.Flush(ready);
                    auto frame_inputs = inputs;
                    if (pass == 0)
                        frame_inputs.color = inputs.previous_color;
                    assert(fx.Encode(frame_inputs, vk::Image(images[5]), param, value));
                    scheduler.WaitExternal(fx.DoneSemaphore(), value);
                    cmd = scheduler.CommandBuffer();
                    scheduler.Finish();
                    assert(fx.Available());
                    assert(fx.OutputInterpolated() == (pass == 2));
                }
                cmd = scheduler.CommandBuffer();
                barriers[5].srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
                barriers[5].dstAccessMask = vk::AccessFlagBits2::eTransferRead;
                cmd.pipelineBarrier2(
                    {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barriers[5]}, d);
                cmd.copyImageToBuffer(
                    vk::Image(images[5]), vk::ImageLayout::eGeneral, raw,
                    vk::BufferImageCopy{
                        .bufferOffset = bytes * 5,
                        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                        .imageExtent = {w, h, 1}},
                    d);
                scheduler.Finish();
                const auto* output = data + bytes * 5;
                const unsigned red = bgra ? 2 : 0, blue = bgra ? 0 : 2;
                unsigned changed = 0;
                double centroid = 0, weight = 0;
                for (unsigned y = 16; y < h - 4; ++y)
                    for (unsigned x = 4; x < w - 4; ++x) {
                        const auto* p = output + (y * w + x) * 4;
                        assert(p[3] >= 250);
                        if (std::abs(int(p[red]) - int(data[(y * w + x) * 4])) > 16)
                            ++changed;
                        if (p[red] > 128) {
                            centroid += x;
                            weight += 1;
                        }
                    }
                assert(changed > 32 && weight > 500);
                centroid /= weight;
                assert(centroid > 40 && centroid < 47);
                auto* ui = output + (4 * w + 4) * 4;
                assert(std::abs(int(ui[red]) - 240) < 5 && std::abs(int(ui[1]) - 210) < 5 &&
                       std::abs(int(ui[blue]) - 32) < 5);
                std::printf("PASS: %s %ux%u depth/motion_div=%u native interpolation changed=%u "
                            "object_centroid=%.3f (previous=39.5 current=47.5), UI "
                            "stable, invalid projection rejected\n",
                            bgra ? "BGRA" : "RGBA", w, h, div, changed, centroid);
                vmaDestroyBuffer(instance.GetAllocator(), raw, allocation);
            }
}
