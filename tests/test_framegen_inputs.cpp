// SPDX-License-Identifier: GPL-2.0-or-later
// Actual production compute shaders: color/UI reduction, nearest-surface depth
// and compact/legacy motion must match independent synthetic pixel references.
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/texture_cache/image.h"
#include "video_core/host_shaders/framegen_inputs_comp.h"
#include "video_core/host_shaders/framegen_inputs_compact_comp.h"
#include "video_core/host_shaders/framegen_ui_comp.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vk_mem_alloc.h>

int main() {
    Vulkan::Instance instance(0,std::getenv("GOW3_VK_VALIDATION")!=nullptr);
    vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance());
    d.init(instance.GetDevice());
    Vulkan::Scheduler scheduler(instance);
    const auto device=instance.GetDevice();
    constexpr unsigned w=8,h=8,slot=1024;
    std::array<vk::DescriptorSetLayoutBinding,6> bindings{};
    for(unsigned i=0;i<6;++i) bindings[i]={.binding=i,
        .descriptorType=i<3?vk::DescriptorType::eSampledImage:vk::DescriptorType::eStorageImage,
        .descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute};
    auto desc=device.createDescriptorSetLayoutUnique({.flags=vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount=6,.pBindings=bindings.data()},nullptr,d).value;
    auto layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*desc},nullptr,d).value;
    unsigned cases=0;
    for(bool compact:{false,true}) for(unsigned color_div:{1u,2u}) for(unsigned depth_div:{1u,2u}) for(bool ui_only:{false,true}) {
        const unsigned cw=w/color_div,ch=h/color_div,dw=w/depth_div,dh=h/depth_div;
        VkBufferCreateInfo bc{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=6*slot,
            .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            .usage=VMA_MEMORY_USAGE_AUTO,.requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        VkBuffer buffer{}; VmaAllocation allocation{}; VmaAllocationInfo mapped{};
        assert(vmaCreateBuffer(instance.GetAllocator(),&bc,&ac,&buffer,&allocation,&mapped)==VK_SUCCESS);
        auto* memory=static_cast<unsigned char*>(mapped.pMappedData);std::memset(memory,0xdd,6*slot);
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
            const auto i=y*w+x;auto* color=memory+4*i;color[0]=x*16;color[1]=y*16;color[2]=64;color[3]=255;
            reinterpret_cast<float*>(memory+slot)[i]=(x%2 && y%2)?0.25f:0.75f;
            auto* motion=reinterpret_cast<_Float16*>(memory+2*slot)+(compact?2:4)*i;
            const bool valid=x%3!=0;
            motion[0]=compact && !valid?0:_Float16(int(x)-4);motion[1]=compact && !valid?0:_Float16(int(y)-4);
            if(!compact){motion[2]=valid?1:0;motion[3]=0.25f;}
        }
        const std::array formats{vk::Format::eR8G8B8A8Unorm,vk::Format::eR32Sfloat,
            compact?vk::Format::eR16G16Sfloat:vk::Format::eR16G16B16A16Sfloat,
            vk::Format::eR8G8B8A8Unorm,vk::Format::eR32Sfloat,vk::Format::eR16G16Sfloat};
        std::array<VideoCore::UniqueImage,6> images;std::array<vk::UniqueImageView,6> views;
        std::array<vk::DescriptorImageInfo,6> descriptors;
        std::array<vk::WriteDescriptorSet,6> writes;
        std::array<vk::ImageMemoryBarrier2,6> barriers;
        for(unsigned i=0;i<6;++i) {
            unsigned iw=i<3?w:i==3?cw:dw,ih=i<3?h:i==3?ch:dh;
            images[i]=VideoCore::UniqueImage(device,instance.GetAllocator());
            images[i].Create({.imageType=vk::ImageType::e2D,.format=formats[i],.extent={iw,ih,1},.mipLevels=1,.arrayLayers=1,
                .samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
                .usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst});
            views[i]=device.createImageViewUnique({.image=vk::Image(images[i]),.viewType=vk::ImageViewType::e2D,
                .format=formats[i],.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}},nullptr,d).value;
            descriptors[i]={{},*views[i],vk::ImageLayout::eGeneral};
            writes[i]={.dstBinding=i,.descriptorCount=1,.descriptorType=bindings[i].descriptorType,.pImageInfo=&descriptors[i]};
            barriers[i]={.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask=vk::AccessFlagBits2::eMemoryWrite,.oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
                .image=vk::Image(images[i]),.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
        }
        auto module=ui_only?Vulkan::CompileSPV(FRAMEGEN_UI_COMP,device):compact?Vulkan::CompileSPV(FRAMEGEN_INPUTS_COMPACT_COMP,device):Vulkan::CompileSPV(FRAMEGEN_INPUTS_COMP,device);
        auto pipeline=device.createComputePipelineUnique({}, {.stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=module,.pName="main"},.layout=*layout},nullptr,d).value;
        device.destroyShaderModule(module,nullptr,d);
        auto cmd=scheduler.CommandBuffer();cmd.pipelineBarrier2({.imageMemoryBarrierCount=6,.pImageMemoryBarriers=barriers.data()},d);
        for(unsigned i=0;i<3;++i) cmd.copyBufferToImage(buffer,vk::Image(images[i]),vk::ImageLayout::eGeneral,
            vk::BufferImageCopy{.bufferOffset=i*slot,.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={w,h,1}},d);
        for(auto& b:barriers){b.oldLayout=vk::ImageLayout::eGeneral;b.srcAccessMask=vk::AccessFlagBits2::eTransferWrite;b.dstAccessMask=vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite;}
        cmd.pipelineBarrier2({.imageMemoryBarrierCount=6,.pImageMemoryBarriers=barriers.data()},d);
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline,d);cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,*layout,0,writes,d);
        cmd.dispatch(1,1,1,d);
        for(auto& b:barriers){b.srcAccessMask=vk::AccessFlagBits2::eShaderWrite;b.dstAccessMask=vk::AccessFlagBits2::eTransferRead;}
        cmd.pipelineBarrier2({.imageMemoryBarrierCount=6,.pImageMemoryBarriers=barriers.data()},d);
        for(unsigned i=3;i<(ui_only?4:6);++i) cmd.copyImageToBuffer(vk::Image(images[i]),vk::ImageLayout::eGeneral,buffer,
            vk::BufferImageCopy{.bufferOffset=i*slot,.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={i==3?cw:dw,i==3?ch:dh,1}},d);
        scheduler.Finish();
        for(unsigned y=0;y<ch;++y)for(unsigned x=0;x<cw;++x) {
            const auto* color=memory+3*slot+4*(y*cw+x);
            assert(color[0]==x*color_div*16+(color_div==2?8:0));
            assert(color[1]==y*color_div*16+(color_div==2?8:0));assert(color[2]==64 && color[3]==255);
        }
        if(!ui_only)for(unsigned y=0;y<dh;++y)for(unsigned x=0;x<dw;++x) {
            const unsigned sx=x*depth_div+(depth_div==2?1:0),sy=y*depth_div+(depth_div==2?1:0),i=y*dw+x;
            const float z=reinterpret_cast<float*>(memory+4*slot)[i];assert(std::abs(z-((sx%2&&sy%2)?0.25f:0.75f))<1e-6f);
            const auto* flow=reinterpret_cast<_Float16*>(memory+5*slot)+2*i;const bool valid=sx%3!=0;
            assert(float(flow[0])==(valid?float(int(sx)-4):0));assert(float(flow[1])==(valid?float(int(sy)-4):0));
        }
        for(unsigned i=3;i<(ui_only?4:6);++i){unsigned bytes=(i==3?cw*ch:dw*dh)*4;for(unsigned b=bytes;b<slot;++b)assert(memory[i*slot+b]==0xdd);}
        vmaDestroyBuffer(instance.GetAllocator(),buffer,allocation);++cases;
    }
    std::printf("PASS: %u production framegen packing/UI variants, exact colors, depth, motion and buffer guards\n",cases);
}
