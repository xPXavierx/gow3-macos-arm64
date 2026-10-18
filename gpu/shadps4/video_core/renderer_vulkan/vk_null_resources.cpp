// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from bloodborne-runtime 9f809002d247e663c92e46cc96b17b1056a38415.
// bbport: stand-ins for null descriptors (vk_null_resources.h).

#include <cstring>

#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_null_resources.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

#include <vk_mem_alloc.h> // after vk_common.h (beta extensions)

namespace Vulkan {

NullResources::NullResources(const Instance& instance_, Scheduler& scheduler) : instance{instance_} {
    const VmaAllocator allocator = instance.GetAllocator();

    // The buffer: host-visible and cleared here, no transfer needed.
    const VkBufferCreateInfo buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = BufferSize,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo buffer_alloc_ci{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VkBuffer raw_buffer{};
    VmaAllocationInfo buffer_info{};
    ASSERT_MSG(vmaCreateBuffer(allocator, &buffer_ci, &buffer_alloc_ci, &raw_buffer, &buffer_allocation,
                               &buffer_info) == VK_SUCCESS,
               "Null descriptor stand-in: buffer allocation failed");
    std::memset(buffer_info.pMappedData, 0, BufferSize);
    vmaFlushAllocation(allocator, buffer_allocation, 0, VK_WHOLE_SIZE);
    buffer = raw_buffer;

    // The images: 1x1 (x1), one layer, float and unsigned integer formats.
    const vk::Device device = instance.GetDevice();
    const VmaAllocationCreateInfo image_alloc_ci{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    for (u32 integer = 0; integer < 3; ++integer) {
        const vk::Format format = integer == 1 ? vk::Format::eR8G8B8A8Uint : integer == 2 ? vk::Format::eR8G8B8A8Sint : vk::Format::eR8G8B8A8Unorm;
        for (u32 shape = 0; shape < NumShapes; ++shape) {
            const bool msaa = shape == Shape2DMsaa;
            const VkImageCreateInfo image_ci{
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = shape == Shape1D   ? VK_IMAGE_TYPE_1D
                             : shape == Shape3D ? VK_IMAGE_TYPE_3D
                                                : VK_IMAGE_TYPE_2D,
                .format = static_cast<VkFormat>(format),
                .extent = {1, 1, 1},
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = msaa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                // Multisampled storage images need shaderStorageImageMultisample: sampled only.
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                         (msaa ? VkImageUsageFlags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) : VkImageUsageFlags(VK_IMAGE_USAGE_STORAGE_BIT)),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            };
            VkImage raw_image{};
            ASSERT_MSG(vmaCreateImage(allocator, &image_ci, &image_alloc_ci, &raw_image,
                                      &image_allocations[integer][shape], nullptr) == VK_SUCCESS,
                       "Null descriptor stand-in: image allocation failed");
            images[integer][shape] = raw_image;
        }
        const auto make_view = [&](Shape shape, vk::ImageViewType type) {
            const vk::ImageViewCreateInfo view_ci{
                .image = images[integer][shape],
                .viewType = type,
                .format = format,
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            auto [result, view] = device.createImageView(view_ci);
            ASSERT_MSG(result == vk::Result::eSuccess, "Null descriptor stand-in: image view failed");
            return view;
        };
        views[integer][View1D] = make_view(Shape1D, vk::ImageViewType::e1D);
        views[integer][View1DArray] = make_view(Shape1D, vk::ImageViewType::e1DArray);
        views[integer][View2D] = make_view(Shape2D, vk::ImageViewType::e2D);
        views[integer][View2DArray] = make_view(Shape2D, vk::ImageViewType::e2DArray);
        views[integer][View2DMsaa] = make_view(Shape2DMsaa, vk::ImageViewType::e2D);
        views[integer][View2DMsaaArray] = make_view(Shape2DMsaa, vk::ImageViewType::e2DArray);
        views[integer][View3D] = make_view(Shape3D, vk::ImageViewType::e3D);
    }

    // To the general layout, cleared to zero, ahead of any draw in the command stream.
    scheduler.Record([images = images, views = views](vk::CommandBuffer cmdbuf) {
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        std::array<vk::ImageMemoryBarrier2, 3 * NumShapes> to_general{};
        for (u32 i = 0; i < to_general.size(); ++i) {
            to_general[i] = vk::ImageMemoryBarrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask = vk::AccessFlagBits2::eMemoryWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = images[i / NumShapes][i % NumShapes],
                .subresourceRange = range,
            };
        }
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = static_cast<u32>(to_general.size()),
            .pImageMemoryBarriers = to_general.data(),
        });
        const vk::ClearColorValue zero{}; // all bits zero: 0.0 and 0 alike
        for (u32 numeric = 0; numeric < images.size(); ++numeric) {
            for (u32 shape = 0; shape < NumShapes; ++shape) {
                if (shape != Shape2DMsaa) {
                    cmdbuf.clearColorImage(images[numeric][shape], vk::ImageLayout::eGeneral, zero, range);
                    continue;
                }
                // vkCmdClearColorImage only accepts single-sampled images.
                const vk::RenderingAttachmentInfo color{
                    .imageView = views[numeric][View2DMsaa],
                    .imageLayout = vk::ImageLayout::eGeneral,
                    .loadOp = vk::AttachmentLoadOp::eClear,
                    .storeOp = vk::AttachmentStoreOp::eStore,
                    .clearValue = vk::ClearValue{zero},
                };
                cmdbuf.beginRendering(vk::RenderingInfo{
                    .renderArea = {{0, 0}, {1, 1}}, .layerCount = 1,
                    .colorAttachmentCount = 1, .pColorAttachments = &color,
                });
                cmdbuf.endRendering();
            }
        }
        const vk::MemoryBarrier2 ready{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
        };
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &ready,
        });
    });
    LOG_INFO(Render_Vulkan, "No nullDescriptor: empty bindings use zeroed stand-in resources");
}

NullResources::~NullResources() {
    const vk::Device device = instance.GetDevice();
    const VmaAllocator allocator = instance.GetAllocator();
    for (u32 integer = 0; integer < 3; ++integer) {
        for (const vk::ImageView view : views[integer]) {
            device.destroyImageView(view);
        }
        for (u32 shape = 0; shape < NumShapes; ++shape) {
            vmaDestroyImage(allocator, images[integer][shape], image_allocations[integer][shape]);
        }
    }
    vmaDestroyBuffer(allocator, buffer, buffer_allocation);
}

vk::ImageView NullResources::ImageView(AmdGpu::ImageType type, AmdGpu::NumberFormat format) const {
    const auto& set = views[format == AmdGpu::NumberFormat::Uint ? 1 : format == AmdGpu::NumberFormat::Sint ? 2 : 0];
    switch (type) {
    case AmdGpu::ImageType::Color1D:
        return set[View1D];
    case AmdGpu::ImageType::Color1DArray:
        return set[View1DArray];
    case AmdGpu::ImageType::Color2DArray:
    case AmdGpu::ImageType::Cube: // the recompiler declares cubes as 2D arrays
        return set[View2DArray];
    case AmdGpu::ImageType::Color2DMsaa:
        return set[View2DMsaa];
    case AmdGpu::ImageType::Color2DMsaaArray:
        return set[View2DMsaaArray];
    case AmdGpu::ImageType::Color3D:
        return set[View3D];
    default:
        return set[View2D];
    }
}

} // namespace Vulkan
