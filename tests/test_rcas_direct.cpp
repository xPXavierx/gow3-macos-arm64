// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the real presentation filters, including sRGB decode and BGRA aliases.
#include <cassert>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <unistd.h>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_presenter.h"
#include "video_core/renderer_vulkan/host_passes/fsr_pass.h"
#include "video_core/renderer_vulkan/host_passes/pp_pass.h"
#include <vk_mem_alloc.h>

int main() {
    char control[] = "/tmp/gow3-rcas-test-XXXXXX";
    const int fd = mkstemp(control);
    assert(fd >= 0);
    close(fd);
    setenv("GOW3_FSR_RCAS_CONTROL", control, 1);
    Vulkan::Instance instance(0, std::getenv("GOW3_VK_VALIDATION") != nullptr);
    vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance());
    d.init(instance.GetDevice());
    std::setvbuf(stdout,nullptr,_IONBF,0);
    Vulkan::Scheduler scheduler(instance);
    const auto device = instance.GetDevice();
    Vulkan::HostPasses::FsrPass fsr;
    fsr.Create(device, instance.GetAllocator(), 3);
    Vulkan::HostPasses::PostProcessingPass pp;
    pp.Create(device, vk::Format::eR8G8B8A8Unorm);
    double total_error = 0;
    unsigned compared = 0, largest_error = 0;
    for (const unsigned w : {64u, 136u}) {
        const unsigned h = 48, bytes = w * h * 4;
        for (const auto format : {vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8A8Unorm}) {
            const bool bgra = format == vk::Format::eB8G8R8A8Unorm;
            const VkBufferCreateInfo bc{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size=bytes*2u,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
            const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|
                VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,.usage=VMA_MEMORY_USAGE_AUTO,
                .requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
            VkBuffer buffer{}; VmaAllocation allocation{}; VmaAllocationInfo mapped{};
            assert(vmaCreateBuffer(instance.GetAllocator(),&bc,&ac,&buffer,&allocation,&mapped)==VK_SUCCESS);
            auto* pixels=static_cast<unsigned char*>(mapped.pMappedData);
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
                auto* p=pixels+(y*w+x)*4;
                p[bgra?2:0]=32+(x/16%4)*48;
                p[1]=24+(y/16)*80;
                p[bgra?0:2]=128;
                p[3]=0; // Presentation, including the direct path, must be opaque.
            }
            VideoCore::UniqueImage input(device,instance.GetAllocator()),output(device,instance.GetAllocator());
            input.Create({.flags=vk::ImageCreateFlagBits::eMutableFormat,.imageType=vk::ImageType::e2D,
                .format=vk::Format::eR8G8B8A8Unorm,.extent={w,h,1},.mipLevels=1,.arrayLayers=1,
                .samples=vk::SampleCountFlagBits::e1,
                .usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst});
            auto view=Vulkan::Check(device.createImageViewUnique({.image=input,.viewType=vk::ImageViewType::e2D,
                .format=bgra?vk::Format::eB8G8R8A8Srgb:vk::Format::eR8G8B8A8Srgb,
                .components={vk::ComponentSwizzle::eIdentity,vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,vk::ComponentSwizzle::eOne},
                .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}},nullptr,d));
            output.Create({.imageType=vk::ImageType::e2D,.format=vk::Format::eR8G8B8A8Unorm,
                .extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,
                .usage=vk::ImageUsageFlagBits::eColorAttachment|vk::ImageUsageFlagBits::eTransferSrc});
            auto out_view=Vulkan::Check(device.createImageViewUnique({.image=output,.viewType=vk::ImageViewType::e2D,
                .format=vk::Format::eR8G8B8A8Unorm,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}},nullptr,d));
            const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor,0,1,0,1};
            auto cmd=scheduler.CommandBuffer();
            vk::ImageMemoryBarrier2 barrier{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferWrite,
                .oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eTransferDstOptimal,
                .image=input,.subresourceRange=range};
            cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&barrier},d);
            cmd.copyBufferToImage(buffer,input,vk::ImageLayout::eTransferDstOptimal,
                vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={w,h,1}},d);
            barrier.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;
            barrier.srcAccessMask=vk::AccessFlagBits2::eTransferWrite;
            barrier.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader;
            barrier.dstAccessMask=vk::AccessFlagBits2::eShaderRead;
            barrier.oldLayout=vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout=vk::ImageLayout::eShaderReadOnlyOptimal;
            cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&barrier},d);
            Vulkan::Frame frame{};
            frame.width=w;frame.height=h;frame.image=output;frame.image_view=*out_view;
            std::vector<unsigned char> reference, direct;
            for (const int legacy : {1,2,0}) {
                FILE* file=std::fopen(control,"w");assert(file);std::fputc('0'+legacy,file);std::fclose(file);
                cmd=scheduler.CommandBuffer();
                barrier={.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                    .dstStageMask=vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    .dstAccessMask=vk::AccessFlagBits2::eColorAttachmentWrite,
                    .oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eColorAttachmentOptimal,
                    .image=output,.subresourceRange=range};
                cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&barrier},d);
                const auto sharpened=legacy ? fsr.Render(cmd,*view,{w,h},{w,h},
                    {.enable=false,.use_rcas=true,.rcas_attenuation=.5f},false) : *view;
                Vulkan::HostPasses::PostProcessingPass::Settings settings;
                if(!legacy) settings.rcas_attenuation=.5f;
                pp.Render(cmd,sharpened,{w,h},frame,settings);
                const vk::MemoryBarrier2 read{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                    .srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,
                    .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,
                    .dstAccessMask=vk::AccessFlagBits2::eTransferRead};
                cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&read},d);
                cmd.copyImageToBuffer(output,vk::ImageLayout::eGeneral,buffer,
                    vk::BufferImageCopy{.bufferOffset=bytes,.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={w,h,1}},d);
                scheduler.Finish();
                const auto* result=pixels+bytes;
                for(unsigned y=8;y<h;y+=16)for(unsigned x=8;x<w;x+=16) {
                    const unsigned expected[]{32+(x/16%4)*48,24+(y/16)*80,128,255};
                    for(unsigned c=0;c<4;++c)assert(std::abs(int(result[(y*w+x)*4+c])-int(expected[c]))<=2);
                }
                if(legacy==1)reference.assign(result,result+bytes);
                else {
                    for(unsigned i=0;i<bytes;++i) {
                        const auto error=unsigned(std::abs(int(reference[i])-int(result[i])));
                        total_error+=error;++compared;largest_error=std::max(largest_error,error);
                        if(!legacy)assert(std::abs(int(direct[i])-int(result[i]))<=1);
                    }
                    if(legacy==2)direct.assign(result,result+bytes);
                }
            }
            vmaDestroyBuffer(instance.GetAllocator(),buffer,allocation);
        }
    }
    std::remove(control);
    std::printf("RCAS direct vs EASU identity + RCAS: mean %.6f/255, max %u/255, %u bytes\n",
        total_error/compared,largest_error,compared);
    assert(total_error/compared < 1.0);
    std::puts("PASS: direct RCAS, RGBA/BGRA sRGB aliases, odd dispatch width, opaque output and color preservation");
}
