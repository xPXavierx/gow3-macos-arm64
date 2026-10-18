// SPDX-License-Identifier: GPL-2.0-or-later
// SMAA tables: Jorge Jimenez et al., MIT; FXAA: NVIDIA/Timothy Lottes, BSD-3-Clause.
#include "spatial_aa_pass.h"
#include "common/assert.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/host_shaders/spatial_aa_0_comp.h"
#include "video_core/host_shaders/spatial_aa_1_comp.h"
#include "video_core/host_shaders/spatial_aa_2_comp.h"
#include "video_core/host_shaders/spatial_aa_3_comp.h"
#include "video_core/host_shaders/spatial_aa/AreaTex.h"
#include "video_core/host_shaders/spatial_aa/SearchTex.h"
#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#endif
#include <cstdlib>
#include <cstring>
#include <vk_mem_alloc.h>

namespace Vulkan::HostPasses {
// Keep Vulkan-Hpp deleters in the renderer's dispatch/validation build context.
SpatialAaPass::SpatialAaPass()=default;
SpatialAaPass::~SpatialAaPass()=default;
namespace {
constexpr vk::ImageSubresourceRange Range{vk::ImageAspectFlagBits::eColor,0,1,0,1};
struct Params { float metrics[4]; int strong; };
void Barrier(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout old_layout,
             vk::ImageLayout new_layout, vk::AccessFlags2 access) {
    const vk::ImageMemoryBarrier2 b{
        .srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,.dstAccessMask=access,
        .oldLayout=old_layout,.newLayout=new_layout,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=image,.subresourceRange=Range};
    cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&b});
}
}

void SpatialAaPass::Create(vk::Device device_, VmaAllocator allocator_) {
    device=device_; allocator=allocator_;
    sampler=Check(device.createSamplerUnique({.magFilter=vk::Filter::eLinear,
        .minFilter=vk::Filter::eLinear,.mipmapMode=vk::SamplerMipmapMode::eNearest,
        .addressModeU=vk::SamplerAddressMode::eClampToEdge,
        .addressModeV=vk::SamplerAddressMode::eClampToEdge,
        .addressModeW=vk::SamplerAddressMode::eClampToEdge,.maxLod=0}));
    std::array<vk::DescriptorSetLayoutBinding,6> bindings{};
    for(u32 i=0;i<6;++i) bindings[i]={.binding=i,
        .descriptorType=i==5?vk::DescriptorType::eStorageImage:vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute};
    descriptors=Check(device.createDescriptorSetLayoutUnique({
        .flags=vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
        .bindingCount=bindings.size(),.pBindings=bindings.data()}));
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute,0,sizeof(Params)};
    layout=Check(device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*descriptors,
        .pushConstantRangeCount=1,.pPushConstantRanges=&push}));
    const std::array<std::span<const u32>,4> shaders={SPATIAL_AA_0_COMP,SPATIAL_AA_1_COMP,
        SPATIAL_AA_2_COMP,SPATIAL_AA_3_COMP};
    for(u32 i=0;i<4;++i) {
        auto module=CompileSPV(shaders[i],device);
        pipelines[i]=Check(device.createComputePipelineUnique({},
            {.stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=module,.pName="main"},.layout=*layout}));
        device.destroyShaderModule(module);
        SetObjectName(device,*pipelines[i],"Spatial AA pass {}",i);
    }
}

void SpatialAaPass::Allocate(vk::CommandBuffer& cmd, Scheduler& scheduler, vk::Extent2D size) {
    if(size==extent) return;
    // Resolution changes drain the owner before replacing images. Mode changes
    // reuse the same images and never read a previous frame's edge/weight data.
    scheduler.Finish();cmd=scheduler.CommandBuffer();
    ASSERT(device);
    auto make=[&](VideoCore::UniqueImage& image,vk::UniqueImageView& view,
                  vk::Extent2D wh,vk::Format format,bool storage) {
        view.reset();image=VideoCore::UniqueImage(device,allocator);
        image.Create({.flags=vk::ImageCreateFlagBits::eMutableFormat,.imageType=vk::ImageType::e2D,
            .format=format,.extent={wh.width,wh.height,1},.mipLevels=1,.arrayLayers=1,
            .samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
            .usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|
                (storage?vk::ImageUsageFlagBits::eStorage:vk::ImageUsageFlagBits::eTransferDst)});
        vk::ImageViewCreateInfo ci{.image=vk::Image(image),.viewType=vk::ImageViewType::e2D,
            .format=format,.subresourceRange=Range};
#ifdef __APPLE__
        VkExportMetalObjectCreateInfoEXT export_info{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
        export_info.exportObjectType=VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;
        if(std::getenv("GOW3_METALFX_MODE"))ci.pNext=&export_info;
#endif
        view=Check(device.createImageViewUnique(ci));
    };
    linear_output.reset();
    make(edges,edge_view,size,vk::Format::eR8G8B8A8Unorm,true);
    make(weights,weight_view,size,vk::Format::eR8G8B8A8Unorm,true);
    make(output,perceptual_output,size,vk::Format::eR8G8B8A8Unorm,true);
    const vk::ImageViewUsageCreateInfo sampled{.usage=vk::ImageUsageFlagBits::eSampled};
    linear_output=Check(device.createImageViewUnique({.pNext=&sampled,.image=vk::Image(output),
        .viewType=vk::ImageViewType::e2D,.format=vk::Format::eR8G8B8A8Srgb,.subresourceRange=Range}));
    for(auto image:{vk::Image(edges),vk::Image(weights),vk::Image(output)})
        Barrier(cmd,image,vk::ImageLayout::eUndefined,vk::ImageLayout::eGeneral,
            vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite);
    if(!area_view) {
        make(area,area_view,{AREATEX_WIDTH,AREATEX_HEIGHT},vk::Format::eR8G8Unorm,false);
        make(search,search_view,{SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT},vk::Format::eR8Unorm,false);
        VkBuffer buffer{};VmaAllocation allocation{};VmaAllocationInfo info{};
        const VkBufferCreateInfo ci{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size=AREATEX_SIZE+SEARCHTEX_SIZE,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
        const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,.usage=VMA_MEMORY_USAGE_AUTO};
        ASSERT(vmaCreateBuffer(allocator,&ci,&ac,&buffer,&allocation,&info)==VK_SUCCESS);
        std::memcpy(info.pMappedData,areaTexBytes,AREATEX_SIZE);
        std::memcpy(static_cast<char*>(info.pMappedData)+AREATEX_SIZE,searchTexBytes,SEARCHTEX_SIZE);
        vmaFlushAllocation(allocator,allocation,0,VK_WHOLE_SIZE);
        for(auto image:{vk::Image(area),vk::Image(search)})
            Barrier(cmd,image,vk::ImageLayout::eUndefined,vk::ImageLayout::eTransferDstOptimal,
                vk::AccessFlagBits2::eTransferWrite);
        cmd.copyBufferToImage(buffer,vk::Image(area),vk::ImageLayout::eTransferDstOptimal,
            vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},
                .imageExtent={AREATEX_WIDTH,AREATEX_HEIGHT,1}});
        cmd.copyBufferToImage(buffer,vk::Image(search),vk::ImageLayout::eTransferDstOptimal,
            vk::BufferImageCopy{.bufferOffset=AREATEX_SIZE,
                .imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},
                .imageExtent={SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,1}});
        for(auto image:{vk::Image(area),vk::Image(search)})
            Barrier(cmd,image,vk::ImageLayout::eTransferDstOptimal,vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eShaderRead);
        scheduler.DeferOperation([a=allocator,buffer,allocation]{vmaDestroyBuffer(a,buffer,allocation);});
    }
    extent=size;output_readonly=false;
}

void SpatialAaPass::Render(vk::CommandBuffer& cmd,Scheduler& scheduler,vk::ImageView perceptual,
        vk::ImageView linear,vk::Extent2D size,int mode) {
    ASSERT(mode>=1&&mode<=3);
    Allocate(cmd,scheduler,size);
    const Params params{{1.f/size.width,1.f/size.height,float(size.width),float(size.height)},mode==3};
    auto pass=[&](unsigned index,vk::Image image,vk::ImageView target,vk::ImageView aux) {
        Barrier(cmd,image,image==vk::Image(output)&&output_readonly?
            vk::ImageLayout::eShaderReadOnlyOptimal:vk::ImageLayout::eGeneral,
            vk::ImageLayout::eGeneral,vk::AccessFlagBits2::eShaderWrite);
        const std::array<vk::DescriptorImageInfo,6> images={{{*sampler,perceptual,vk::ImageLayout::eShaderReadOnlyOptimal},
            {*sampler,aux,vk::ImageLayout::eGeneral},{*sampler,*area_view,vk::ImageLayout::eShaderReadOnlyOptimal},
            {*sampler,*search_view,vk::ImageLayout::eShaderReadOnlyOptimal},
            {*sampler,linear,vk::ImageLayout::eShaderReadOnlyOptimal},{{},target,vk::ImageLayout::eGeneral}}};
        std::array<vk::WriteDescriptorSet,6> writes{};
        for(u32 i=0;i<6;++i)writes[i]={.dstBinding=i,.descriptorCount=1,
            .descriptorType=i==5?vk::DescriptorType::eStorageImage:vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo=&images[i]};
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipelines[index]);
        cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,*layout,0,writes);
        cmd.pushConstants(*layout,vk::ShaderStageFlagBits::eCompute,0,sizeof(params),&params);
        cmd.dispatch((size.width+7)/8,(size.height+7)/8,1);
        Barrier(cmd,image,vk::ImageLayout::eGeneral,vk::ImageLayout::eGeneral,vk::AccessFlagBits2::eShaderRead);
    };
    // Every texel is overwritten, including zero edges/weights. No history is retained.
    if(mode==1) {
        pass(1,vk::Image(edges),*edge_view,*edge_view);
        pass(2,vk::Image(weights),*weight_view,*edge_view);
        pass(3,vk::Image(output),*perceptual_output,*weight_view);
    } else {
        pass(0,vk::Image(output),*perceptual_output,*edge_view);
    }
    output_readonly=false;
}
void SpatialAaPass::PrepareForVulkan(vk::CommandBuffer cmd) {
    Barrier(cmd,vk::Image(output),vk::ImageLayout::eGeneral,vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::AccessFlagBits2::eShaderRead);
    output_readonly=true;
}
}
