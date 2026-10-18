// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/specialization.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include <vk_mem_alloc.h>
using namespace Shader;
static std::vector<u32> Code(bool empty) {
    Info info{};info.hw_stage=HwStage::Compute;info.sw_stage=SwStage::Compute;
    info.buffers.push_back({.used_types=IR::Type::U32,.buffer_type=BufferType::Guest,.is_written=true});
    auto sharp=AmdGpu::Image::Null(false);sharp.base_address=empty?0:0x100;
    sharp.data_format=u64(AmdGpu::DataFormat::Format32);sharp.num_format=u64(AmdGpu::NumberFormat::Uint);
    ImageResource image{.is_atomic=true,.is_written=true};
    std::memcpy(image.sharp_fetch.immediates.data(),&sharp,sizeof(sharp));info.images.push_back(image);
    RuntimeInfo runtime{};runtime.Initialize(info.hw_stage,info.sw_stage);runtime.hw.cs.workgroup_size={1,1,1};
    Profile profile{};profile.supported_spirv=0x00010600;profile.manual_null_images=true;
    Backend::Bindings bindings{};
    StageSpecialization first(info,runtime,profile,bindings);
    sharp.base_address=empty?0x100:0;Info alternate=info;
    std::memcpy(alternate.images[0].sharp_fetch.immediates.data(),&sharp,sizeof(sharp));
    StageSpecialization second(alternate,runtime,profile,bindings);
    assert(!(first==second)&&!(second==first));
    Common::ObjectPool<IR::Inst> pool;IR::Block block(pool);IR::IREmitter ir(block);ir.Prologue();
    const auto zero=ir.Imm32(0u);const auto xy=ir.CompositeConstruct(zero,zero);
    const auto f=ir.BitCast<IR::F32>(ir.Imm32(42u));
    ir.ImageWrite(zero,xy,{}, {},ir.CompositeConstruct(f,f,f,f),{});
    ir.DeviceMemoryBarrier();
    const auto old=ir.ImageAtomicIAdd(zero,xy,ir.Imm32(1u),{});
    ir.DeviceMemoryBarrier();
    const auto read=ir.ImageRead(zero,xy,{}, {},{});
    ir.StoreBufferU32(1,zero,zero,ir.BitCast<IR::U32>(IR::F32{ir.CompositeExtract(read,0)}),{});
    ir.StoreBufferU32(1,zero,ir.Imm32(1u),IR::U32{old},{});ir.Epilogue();
    IR::Program program(info);program.blocks.push_back(&block);
    program.syntax_list.push_back({.data={.block=&block},.type=IR::AbstractSyntaxNode::Type::Block});
    program.syntax_list.push_back({.type=IR::AbstractSyntaxNode::Type::Return});
    return Backend::SPIRV::EmitSPIRV(profile,runtime,program,bindings);
}
int main() {
    Vulkan::Instance instance(0,std::getenv("GOW3_VK_VALIDATION")!=nullptr);vk::detail::DynamicLoader loader;vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));d.init(instance.GetInstance());d.init(instance.GetDevice());
    const auto device=instance.GetDevice();Vulkan::Scheduler scheduler(instance);
    Vulkan::Runtime renderer_runtime(instance, scheduler);
    Common::SlotVector<VideoCore::ImageView> views;
    VideoCore::ImageInfo image_info{};
    image_info.type=AmdGpu::ImageType::Color2D;image_info.pixel_format=vk::Format::eR8G8B8A8Unorm;
    image_info.size={1,1,1};image_info.pitch=1;image_info.num_bits=32;image_info.resources={1,1};
    image_info.guest_size=4;image_info.mips_layout[0]={4,1,1,0};
    VideoCore::Image production_image(instance,renderer_runtime,views,image_info);
    const char* color_mode = std::getenv("GOW3_LAZY_COLOR");
    if (color_mode && color_mode[0] == '3') production_image.TryCompactMinTarget();
    const bool lazy = color_mode && (color_mode[0] == '1' || color_mode[0] == '3');
    assert(bool(production_image.backing->image.image_ci.usage & vk::ImageUsageFlagBits::eStorage)
           == !lazy);
    VideoCore::ImageViewInfo storage{};storage.format=vk::Format::eR32Uint;
    storage.is_storage=true;storage.range={.base={},.extent={1,1}};
    const vk::ImageView view=*production_image.FindView(storage).image_view;
    assert(production_image.backing->image.image_ci.usage & vk::ImageUsageFlagBits::eStorage);
    auto* storage_backing = production_image.backing;
    production_image.TryCompactMinTarget();
    assert(production_image.backing == storage_backing); // No compact/promote loop.
    const vk::Image image=production_image.GetImage();
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor,0,1,0,1};
    const VkBufferCreateInfo bc{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=512,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    const VmaAllocationCreateInfo host{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage=VMA_MEMORY_USAGE_AUTO,.requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    VkBuffer buffer;VmaAllocation bm;VmaAllocationInfo mapped;
    assert(vmaCreateBuffer(instance.GetAllocator(),&bc,&host,&buffer,&bm,&mapped)==VK_SUCCESS);
    const std::array<vk::DescriptorSetLayoutBinding,2> bindings{{
        {0,vk::DescriptorType::eStorageBuffer,1,vk::ShaderStageFlagBits::eCompute},
        {1,vk::DescriptorType::eStorageImage,1,vk::ShaderStageFlagBits::eCompute}}};
    auto descriptors=device.createDescriptorSetLayoutUnique({.bindingCount=2,.pBindings=bindings.data()},nullptr,d).value;
    const std::array<vk::DescriptorPoolSize,2> sizes{{{vk::DescriptorType::eStorageBuffer,2},{vk::DescriptorType::eStorageImage,2}}};
    auto pool=device.createDescriptorPoolUnique({.maxSets=2,.poolSizeCount=2,.pPoolSizes=sizes.data()},nullptr,d).value;
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute,0,sizeof(PushData)};
    auto layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*descriptors,.pushConstantRangeCount=1,.pPushConstantRanges=&push},nullptr,d).value;
    for(bool empty:{true,false}) {
        // Use the rasterizer's null descriptor classification; a default ImageDesc used to
        // turn absent storage resources into sampled-image writes against a storage layout.
        const VideoCore::TextureCache::ImageDesc fallback{ImageResource{.is_written=true}};
        assert(fallback.type==VideoCore::TextureCache::BindingType::Storage && fallback.view_info.is_storage);
        const auto code=Code(empty);auto module=device.createShaderModuleUnique({.codeSize=code.size()*4,.pCode=code.data()},nullptr,d).value;
        auto pipeline=device.createComputePipelineUnique({},{.stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=*module,.pName="main"},.layout=*layout},nullptr,d).value;
        const auto set=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*descriptors},d).value[0];
        const vk::DescriptorBufferInfo binfo{buffer,0,256};const vk::DescriptorImageInfo iinfo{{},view,vk::ImageLayout::eGeneral};
        const std::array<vk::WriteDescriptorSet,2> writes{{
            {.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=vk::DescriptorType::eStorageBuffer,.pBufferInfo=&binfo},
            {.dstSet=set,.dstBinding=1,.descriptorCount=1,.descriptorType=fallback.type==VideoCore::TextureCache::BindingType::Storage ? vk::DescriptorType::eStorageImage : vk::DescriptorType::eSampledImage,.pImageInfo=&iinfo}}};
        device.updateDescriptorSets(writes,{},d);
        const auto cmd=scheduler.CommandBuffer();
        const vk::ImageMemoryBarrier2 begin{.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferWrite,
            .oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,.image=image,.subresourceRange=range};
        cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&begin},d);
        static_cast<u32*>(mapped.pMappedData)[96]=17;
        const vk::BufferImageCopy initialize{.bufferOffset=384,
            .imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={1,1,1}};
        cmd.copyBufferToImage(buffer,image,vk::ImageLayout::eGeneral,initialize,d);
        const vk::MemoryBarrier2 ready{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,.dstAccessMask=vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite};
        cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&ready},d);
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline,d);cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*layout,0,set,{},d);cmd.dispatch(1,1,1,d);
        const vk::MemoryBarrier2 done{.srcStageMask=vk::PipelineStageFlagBits2::eComputeShader,.srcAccessMask=vk::AccessFlagBits2::eShaderWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eTransfer|vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eTransferRead|vk::AccessFlagBits2::eHostRead};
        cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&done},d);
        const vk::BufferImageCopy region{.bufferOffset=256,.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={1,1,1}};
        cmd.copyImageToBuffer(image,vk::ImageLayout::eGeneral,buffer,region,d);
        const vk::MemoryBarrier2 readable{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead};
        cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&readable},d);scheduler.Finish();
        auto* values=static_cast<u32*>(mapped.pMappedData);
        std::printf("null=%d: read=%u atomic=%u image=%u\n",empty,values[0],values[1],values[64]);
        assert(values[0]==(empty?0u:43u)&&values[1]==(empty?0u:42u)&&values[64]==(empty?17u:43u));
    }
    vmaDestroyBuffer(instance.GetAllocator(),buffer,bm);
    std::puts("PASS: null reads/atomics return zero, writes suppressed, stand-in unchanged; live image remains writable; null/live specialization distinct");
}
