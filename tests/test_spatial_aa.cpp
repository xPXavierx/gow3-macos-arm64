// SPDX-License-Identifier: GPL-2.0-or-later
// Production spatial AA: RGB/BGRA aliases, flat colors, diagonal edges, zero-alpha,
// odd sizes, mode changes and black-frame history rejection. Optional raw replay.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/host_passes/spatial_aa_pass.h"
#include <vk_mem_alloc.h>

int main(int argc,char**argv) {
    Vulkan::Instance instance(0,std::getenv("GOW3_VK_VALIDATION")!=nullptr);
    vk::detail::DynamicLoader loader; vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance());d.init(instance.GetDevice());
    const auto device=instance.GetDevice();
    Vulkan::Scheduler scheduler(instance);
    Vulkan::HostPasses::SpatialAaPass aa;aa.Create(device,instance.GetAllocator());
    const bool replay=argc==5;
    unsigned checked=0;
    for(bool bgra:{false,true})for(unsigned w:{129u,193u}) {
        if(replay&&(bgra||w!=129))continue;
        const unsigned width=replay?std::stoul(argv[2]):w,height=replay?std::stoul(argv[3]):97;
        const size_t bytes=size_t(width)*height*4;
        std::vector<unsigned char> input(bytes);
        if(replay) {
            std::ifstream f(argv[1],std::ios::binary);assert(f.read(reinterpret_cast<char*>(input.data()),bytes));
        } else {
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                auto*p=&input[(y*width+x)*4];
                if(y<24){p[0]=30;p[1]=45;p[2]=60;p[(x*3/width)%3]=200;}
                else p[0]=p[1]=p[2]=(x+.5>2.65*(y-24+.5)+4)?220:30;
                p[3]=0;
                if(bgra)std::swap(p[0],p[2]);
            }
        }
        VkBuffer buffer{};VmaAllocation allocation{};VmaAllocationInfo mapped{};
        const VkBufferCreateInfo bc{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes*2,
            .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,.usage=VMA_MEMORY_USAGE_AUTO};
        assert(vmaCreateBuffer(instance.GetAllocator(),&bc,&ac,&buffer,&allocation,&mapped)==VK_SUCCESS);
        VideoCore::UniqueImage source(device,instance.GetAllocator());
        source.Create({.flags=vk::ImageCreateFlagBits::eMutableFormat,.imageType=vk::ImageType::e2D,
            .format=vk::Format::eR8G8B8A8Unorm,.extent={width,height,1},.mipLevels=1,.arrayLayers=1,
            .samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
            .usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst});
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor,0,1,0,1};
        auto view=[&](bool srgb){return device.createImageViewUnique({.image=vk::Image(source),
            .viewType=vk::ImageViewType::e2D,
            .format=bgra?(srgb?vk::Format::eB8G8R8A8Srgb:vk::Format::eB8G8R8A8Unorm):
                (srgb?vk::Format::eR8G8B8A8Srgb:vk::Format::eR8G8B8A8Unorm),
            .subresourceRange=range},nullptr,d).value;};
        auto perceptual=view(false),linear=view(true);
        bool uploaded=false;
        auto upload=[&](bool black) {
            scheduler.Finish();
            if(black)std::memset(mapped.pMappedData,0,bytes);
            else std::memcpy(mapped.pMappedData,input.data(),bytes);
            vmaFlushAllocation(instance.GetAllocator(),allocation,0,bytes);
            auto cmd=scheduler.CommandBuffer();
            vk::ImageMemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                .srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
                .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferWrite,
                .oldLayout=uploaded?vk::ImageLayout::eShaderReadOnlyOptimal:vk::ImageLayout::eUndefined,
                .newLayout=vk::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
                .image=vk::Image(source),.subresourceRange=range};
            cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&b},d);
            cmd.copyBufferToImage(buffer,vk::Image(source),vk::ImageLayout::eTransferDstOptimal,
                vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={width,height,1}},d);
            b.oldLayout=b.newLayout;b.newLayout=vk::ImageLayout::eShaderReadOnlyOptimal;
            b.srcAccessMask=vk::AccessFlagBits2::eTransferWrite;b.dstAccessMask=vk::AccessFlagBits2::eShaderRead;
            b.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;b.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader;
            cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&b},d);uploaded=true;
        };
        auto readback=[&] {
            auto cmd=scheduler.CommandBuffer();
            const vk::MemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                .srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,
                .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead};
            cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&b},d);
            cmd.copyImageToBuffer(aa.Image(),vk::ImageLayout::eGeneral,buffer,
                vk::BufferImageCopy{.bufferOffset=bytes,.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},
                    .imageExtent={width,height,1}},d);
            scheduler.Finish();vmaInvalidateAllocation(instance.GetAllocator(),allocation,bytes,bytes);
            return static_cast<const unsigned char*>(mapped.pMappedData)+bytes;
        };
        for(int mode:{1,2,3}) {
            upload(false);auto cmd=scheduler.CommandBuffer();
            aa.Render(cmd,scheduler,*perceptual,*linear,{width,height},mode);scheduler.Finish();
            const auto query=device.createQueryPoolUnique({.queryType=vk::QueryType::eTimestamp,.queryCount=2},nullptr,d).value;
            cmd=scheduler.CommandBuffer();cmd.resetQueryPool(*query,0,2,d);
            cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands,*query,0,d);
            constexpr unsigned repeats=40;
            for(unsigned i=0;i<repeats;++i)aa.Render(cmd,scheduler,*perceptual,*linear,{width,height},mode);
            cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands,*query,1,d);scheduler.Finish();
            uint64_t ticks[2]{};
            assert(d.vkGetQueryPoolResults(device,*query,0,2,sizeof(ticks),ticks,sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT)==VK_SUCCESS);
            const double ms=(ticks[1]-ticks[0])*instance.GetPhysicalDevice().getProperties(d).limits.timestampPeriod/1e6/repeats;
            const auto* result=readback();
            if(replay) {
                const std::filesystem::path dest=std::filesystem::path(argv[1]).parent_path()/('m'+std::to_string(mode)+'-'+std::filesystem::path(argv[4]).filename().string());
                std::ofstream f(dest,std::ios::binary);f.write(reinterpret_cast<const char*>(result),bytes);
            } else {
                for(unsigned section=0;section<3;++section) {
                    const unsigned x=(2*section+1)*width/6,y=8;
                    for(unsigned c=0;c<3;++c){assert(std::abs(int(result[(y*width+x)*4+c])-int(c==section?200:30+c*15))<=1);++checked;}
                }
                unsigned fractional=0;
                for(unsigned y=28;y<height-4;++y)for(unsigned x=4;x<width-4;++x) {
                    const int v=result[(y*width+x)*4];
                    fractional+=v>31&&v<219;assert(result[(y*width+x)*4+3]==255);
                }
                assert(fractional>15);
            }
            std::printf("Spatial AA GPU: mode=%d %ux%u bgra_alias=%d average=%.6f ms (%u repeats)\n",mode,width,height,bgra,ms,repeats);
            upload(true);cmd=scheduler.CommandBuffer();aa.Render(cmd,scheduler,*perceptual,*linear,{width,height},mode);
            result=readback();for(size_t i=0;i<bytes;i+=4){assert(result[i]==0&&result[i+1]==0&&result[i+2]==0&&result[i+3]==255);++checked;}
            // Exercise the direct Vulkan presentation layout before the next mode.
            aa.PrepareForVulkan(scheduler.CommandBuffer());scheduler.Finish();
        }
        scheduler.Finish();vmaDestroyBuffer(instance.GetAllocator(),buffer,allocation);
    }
    std::printf("PASS: production spatial AA, RGB/BGRA colors, diagonal edges, odd-size resize, mode changes, zero alpha and no history; %u checks\n",checked);
}
