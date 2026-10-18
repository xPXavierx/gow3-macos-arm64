// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the production color-to-depth helper and preserve an existing stencil plane.
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/blit_helper.h"
#include "video_core/texture_cache/image.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vk_mem_alloc.h>

int main() {
    Vulkan::Instance instance(0, std::getenv("GOW3_VK_VALIDATION") != nullptr);
    vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance());
    d.init(instance.GetDevice());
    Vulkan::Scheduler scheduler(instance);
    VideoCore::BlitHelper helper(instance, scheduler);
    constexpr unsigned w = 64, h = 64, n = w * h;
    VkBufferCreateInfo bc{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                          .size = n * 9,
                          .usage =
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ac{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                                        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                               .usage = VMA_MEMORY_USAGE_AUTO,
                               .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    VkBuffer raw{};
    VmaAllocation allocation{};
    VmaAllocationInfo info{};
    assert(vmaCreateBuffer(instance.GetAllocator(), &bc, &ac, &raw, &allocation, &info) ==
           VK_SUCCESS);
    auto* values = static_cast<float*>(info.pMappedData);
    for (unsigned i = 0; i < n; ++i)
        values[i] = .1f + .8f * float(i % w) / float(w - 1);
    VideoCore::UniqueImage source(instance.GetDevice(), instance.GetAllocator()),
        depth(instance.GetDevice(), instance.GetAllocator());
    source.Create(
        {.imageType = vk::ImageType::e2D,
         .format = vk::Format::eR32Sfloat,
         .extent = {w, h, 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = vk::SampleCountFlagBits::e1,
         .tiling = vk::ImageTiling::eOptimal,
         .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst});
    depth.Create({.imageType = vk::ImageType::e2D,
                  .format = vk::Format::eD32SfloatS8Uint,
                  .extent = {w, h, 1},
                  .mipLevels = 1,
                  .arrayLayers = 1,
                  .samples = vk::SampleCountFlagBits::e1,
                  .tiling = vk::ImageTiling::eOptimal,
                  .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
                           vk::ImageUsageFlagBits::eTransferDst |
                           vk::ImageUsageFlagBits::eTransferSrc});
    const auto both = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    auto cmd = scheduler.CommandBuffer();
    auto transit = [&](vk::Image image, vk::ImageAspectFlags aspect, vk::ImageLayout old,
                       vk::ImageLayout next) {
        const vk::ImageMemoryBarrier2 b{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            .oldLayout = old,
            .newLayout = next,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {aspect, 0, 1, 0, 1}};
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b}, d);
    };
    transit(vk::Image(source), vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal);
    transit(vk::Image(depth), both, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
    cmd.copyBufferToImage(
        raw, vk::Image(source), vk::ImageLayout::eTransferDstOptimal,
        vk::BufferImageCopy{.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                            .imageExtent = {w, h, 1}},
        d);
    cmd.clearDepthStencilImage(vk::Image(depth), vk::ImageLayout::eGeneral, {1.f, 0x5a},
                               vk::ImageSubresourceRange{both, 0, 1, 0, 1}, d);
    transit(vk::Image(source), vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
    transit(vk::Image(depth), both, vk::ImageLayout::eGeneral,
            vk::ImageLayout::eDepthStencilAttachmentOptimal);
    helper.ReinterpretColorAsMsDepth(w, h, 1, vk::Format::eR32Sfloat, vk::Format::eD32SfloatS8Uint,
                                     vk::Image(source), vk::Image(depth));
    scheduler.EndRendering();
    cmd = scheduler.CommandBuffer();
    transit(vk::Image(depth), both, vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eGeneral);
    cmd.copyImageToBuffer(
        vk::Image(depth), vk::ImageLayout::eGeneral, raw,
        vk::BufferImageCopy{.bufferOffset = n * 4,
                            .imageSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1},
                            .imageExtent = {w, h, 1}},
        d);
    cmd.copyImageToBuffer(
        vk::Image(depth), vk::ImageLayout::eGeneral, raw,
        vk::BufferImageCopy{.bufferOffset = n * 8,
                            .imageSubresource = {vk::ImageAspectFlagBits::eStencil, 0, 0, 1},
                            .imageExtent = {w, h, 1}},
        d);
    scheduler.Finish();
    scheduler.GetWorkSemaphore()->Refresh();
    scheduler.PopPendingOperations();
    vmaInvalidateAllocation(instance.GetAllocator(), allocation, 0, VK_WHOLE_SIZE);
    const auto* stencil = reinterpret_cast<unsigned char*>(values) + n * 8;
    for (unsigned i = 0; i < n; ++i) {
        assert(std::abs(values[i] - values[n + i]) < 1e-6f);
        assert(stencil[i] == 0x5a);
    }
    std::printf("PASS: production combined depth/stencil blit: %u depth values and %u preserved "
                "stencil bytes\n",
                n, n);
    vmaDestroyBuffer(instance.GetAllocator(), raw, allocation);
}
