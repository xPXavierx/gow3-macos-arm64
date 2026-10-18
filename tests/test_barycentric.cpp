// SPDX-License-Identifier: GPL-2.0-or-later
// Render nonuniform clip-W triangles with the production emitter and compare
// perspective and reconstructed linear weights against analytic screen geometry.
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <bit>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include <vk_mem_alloc.h>

using namespace Shader;
static std::vector<u32> ShaderCode(bool vertex, IR::Attribute linear, bool resources = false) {
    Info info{};
    info.hw_stage=vertex ? HwStage::Vertex : HwStage::Fragment;
    info.sw_stage=vertex ? SwStage::Vertex : SwStage::Fragment;
    RuntimeInfo runtime{}; runtime.Initialize(info.hw_stage,info.sw_stage);
    Common::ObjectPool<IR::Inst> pool; IR::Block block(pool); IR::IREmitter ir(block);
    ir.Prologue();
    IR::F32 resource_scale = ir.Imm32(1.f);
    if (resources) {
        info.ud_mask.Set(IR::ScalarReg(0));
        info.buffers.push_back({.used_types=vertex ? IR::Type::U32 | IR::Type::F32 : IR::Type::U32,
                               .buffer_type=BufferType::Guest});
        resource_scale = ir.BitCast<IR::F32>(ir.GetUserData(IR::ScalarReg(0)));
        resource_scale = ir.FPMul(resource_scale, ir.BitCast<IR::F32>(
            ir.ReadConstBuffer(ir.Imm32(0u), ir.Imm32(3u), {})));
        if (vertex) {
            const auto triple = ir.LoadBufferF32(3, ir.Imm32(0u), ir.Imm32(0u), {});
            for (u32 c=0; c<3; ++c) resource_scale = ir.FPMul(resource_scale,
                IR::F32{ir.CompositeExtract(triple,c)});
        }
    }
    if (vertex) {
        info.loads.Set(IR::Attribute::VertexId,0);
        const auto id=ir.GetAttributeU32(IR::Attribute::VertexId);
        const auto zero=ir.IEqual(id,ir.Imm32(0u));
        const auto one=ir.IEqual(id,ir.Imm32(1u));
        // NDC (-.8,-.8),(.8,-.8),(-.8,.8); distinct W = 1,2,4.
        const std::array<std::array<float,4>,3> pos{{
            {-.8f,-.8f,.5f,1.f},{1.6f,-1.6f,1.f,2.f},{-3.2f,3.2f,2.f,4.f}}};
        for (u32 c=0;c<4;++c) {
            const IR::F32 value{ir.Select(zero,ir.Imm32(pos[0][c]),
                                ir.Select(one,ir.Imm32(pos[1][c]),ir.Imm32(pos[2][c])))};
            ir.SetAttribute(IR::Attribute::Position0,c<2 ? IR::F32{ir.FPMul(value,resource_scale)} : value,c);
            info.stores.Set(IR::Attribute::Position0,c);
        }
    } else {
        runtime.hw.fs.color_buffers[0].num_format=AmdGpu::NumberFormat::Float;
        for (u32 c=0;c<4;++c) {
            const auto attr=c<2 ? IR::Attribute::BaryCoordSmooth : linear;
            ir.SetAttribute(IR::Attribute::RenderTarget0,ir.FPMul(ir.GetAttribute(attr,c%2),resource_scale),c);
            info.loads.Set(attr,c%2); info.stores.Set(IR::Attribute::RenderTarget0,c);
        }
    }
    ir.Epilogue();
    IR::Program program(info); program.blocks.push_back(&block);
    program.syntax_list.push_back({.data={.block=&block},.type=IR::AbstractSyntaxNode::Type::Block});
    program.syntax_list.push_back({.type=IR::AbstractSyntaxNode::Type::Return});
    Profile profile{};profile.supported_spirv=0x00010600;
    profile.supports_fragment_shader_barycentric=true;profile.needs_single_barycentric_mode=true;
    profile.manual_buffer_bounds=true;profile.needs_buffer_offsets=resources;
    Backend::Bindings bindings{};if(resources && vertex)bindings.unified=7;
    return Backend::SPIRV::EmitSPIRV(profile,runtime,program,bindings);
}

int main(int argc, char** argv) {
    Vulkan::Instance instance(0,false);
    assert(instance.IsFragmentShaderBarycentricSupported());
    vk::detail::DynamicLoader loader;vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance());d.init(instance.GetDevice());
    auto device=instance.GetDevice();Vulkan::Scheduler scheduler(instance);
    const u32 size=argc>1 ? std::strtoul(argv[1],nullptr,10) : 32;
    assert(size>=32 && size<=2048);
    const bool resources=argc>2 && std::strcmp(argv[2],"--resource-bindings")==0;
    const VkImageCreateInfo ci{.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R32G32B32A32_SFLOAT,
        .extent={size,size,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    const VmaAllocationCreateInfo local{.usage=VMA_MEMORY_USAGE_AUTO};
    VkImage raw_image;VmaAllocation image_mem;
    assert(vmaCreateImage(instance.GetAllocator(),&ci,&local,&raw_image,&image_mem,nullptr)==VK_SUCCESS);
    const vk::Image image=raw_image;
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor,0,1,0,1};
    auto view=device.createImageViewUnique({.image=image,.viewType=vk::ImageViewType::e2D,
        .format=vk::Format::eR32G32B32A32Sfloat,.subresourceRange=range},nullptr,d).value;
    const VkBufferCreateInfo bi{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size*size*16,
        .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    const VmaAllocationCreateInfo host{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage=VMA_MEMORY_USAGE_AUTO,.requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    VkBuffer buffer;VmaAllocation memory;VmaAllocationInfo mapped;
    assert(vmaCreateBuffer(instance.GetAllocator(),&bi,&host,&buffer,&memory,&mapped)==VK_SUCCESS);
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eAllGraphics,0,sizeof(PushData)};
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    if(resources) for(u32 i=0;i<8;++i)bindings.push_back({.binding=i,.descriptorType=vk::DescriptorType::eStorageBuffer,
        .descriptorCount=1,.stageFlags=i==7 ? vk::ShaderStageFlagBits::eVertex : vk::ShaderStageFlagBits::eFragment});
    auto descriptor_layout=device.createDescriptorSetLayoutUnique({.bindingCount=u32(bindings.size()),.pBindings=bindings.data()},nullptr,d).value;
    const vk::DescriptorSetLayout raw_layout=*descriptor_layout;
    auto layout=device.createPipelineLayoutUnique({.setLayoutCount=resources ? 1u : 0u,.pSetLayouts=resources ? &raw_layout : nullptr,
        .pushConstantRangeCount=1,.pPushConstantRanges=&push},nullptr,d).value;
    VkBuffer input{};VmaAllocation input_mem{};VmaAllocationInfo input_map{};
    vk::UniqueDescriptorPool descriptor_pool;vk::DescriptorSet descriptor_set{};
    if(resources) {
        const VkBufferCreateInfo input_ci{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=64,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
        assert(vmaCreateBuffer(instance.GetAllocator(),&input_ci,&host,&input,&input_mem,&input_map)==VK_SUCCESS);
        std::fill_n(static_cast<float*>(input_map.pMappedData),16,1.f);
        static_cast<float*>(input_map.pMappedData)[0]=9.f; // the pushed 4-byte offset must skip this
        const vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer,8};
        descriptor_pool=std::move(device.createDescriptorPoolUnique({.maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size},nullptr,d).value);
        descriptor_set=device.allocateDescriptorSets({.descriptorPool=*descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&raw_layout},d).value[0];
        const vk::DescriptorBufferInfo data{input,0,64};std::vector<vk::WriteDescriptorSet> writes;
        for(u32 i=0;i<8;++i)writes.push_back({.dstSet=descriptor_set,.dstBinding=i,.descriptorCount=1,
            .descriptorType=vk::DescriptorType::eStorageBuffer,.pBufferInfo=&data});
        device.updateDescriptorSets(writes,{},d);
    }
    const auto vs=ShaderCode(true,IR::Attribute::BaryCoordNoPersp,resources);
    auto vertex=device.createShaderModuleUnique({.codeSize=vs.size()*4,.pCode=vs.data()},nullptr,d).value;
    for (const auto linear:{IR::Attribute::BaryCoordNoPersp}) {
        const auto fs=ShaderCode(false,linear,resources);
        auto fragment=device.createShaderModuleUnique({.codeSize=fs.size()*4,.pCode=fs.data()},nullptr,d).value;
        const std::array<vk::PipelineShaderStageCreateInfo,2> stages{{
            {.stage=vk::ShaderStageFlagBits::eVertex,.module=*vertex,.pName="main"},
            {.stage=vk::ShaderStageFlagBits::eFragment,.module=*fragment,.pName="main"}}};
        const vk::PipelineVertexInputStateCreateInfo vi{};
        const vk::PipelineInputAssemblyStateCreateInfo ia{.topology=vk::PrimitiveTopology::eTriangleList};
        const vk::Viewport vp{0,0,float(size),float(size),0,1};const vk::Rect2D sc{{0,0},{size,size}};
        const vk::PipelineViewportStateCreateInfo viewport{.viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};
        const vk::PipelineRasterizationStateCreateInfo raster{.polygonMode=vk::PolygonMode::eFill,.cullMode=vk::CullModeFlagBits::eNone,.lineWidth=1};
        const vk::PipelineMultisampleStateCreateInfo ms{.rasterizationSamples=vk::SampleCountFlagBits::e1};
        const vk::PipelineColorBlendAttachmentState attachment{.colorWriteMask=vk::ColorComponentFlagBits::eR|vk::ColorComponentFlagBits::eG|vk::ColorComponentFlagBits::eB|vk::ColorComponentFlagBits::eA};
        const vk::PipelineColorBlendStateCreateInfo blend{.attachmentCount=1,.pAttachments=&attachment};
        const vk::Format format=vk::Format::eR32G32B32A32Sfloat;
        const vk::PipelineRenderingCreateInfo rendering{.colorAttachmentCount=1,.pColorAttachmentFormats=&format};
        const vk::GraphicsPipelineCreateInfo pc{.pNext=&rendering,.stageCount=2,.pStages=stages.data(),
            .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&viewport,
            .pRasterizationState=&raster,.pMultisampleState=&ms,.pColorBlendState=&blend,.layout=*layout};
        auto result=device.createGraphicsPipelineUnique({},pc,nullptr,d);
        assert(result.result==vk::Result::eSuccess);auto pipeline=std::move(result.value);
        const auto cmd=scheduler.CommandBuffer();
        const vk::ImageMemoryBarrier2 begin{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
            .dstStageMask=vk::PipelineStageFlagBits2::eColorAttachmentOutput,.dstAccessMask=vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eColorAttachmentOptimal,
            .image=image,.subresourceRange=range};
        cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&begin},d);
        const vk::RenderingAttachmentInfo target{.imageView=*view,.imageLayout=vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp=vk::AttachmentLoadOp::eClear,.storeOp=vk::AttachmentStoreOp::eStore};
        cmd.beginRendering({.renderArea=sc,.layerCount=1,.colorAttachmentCount=1,.pColorAttachments=&target},d);
        for(u32 draw=0;draw<(resources ? 256u : 1u);++draw) {
            PushData pushed{};pushed.ud_regs[0]=std::bit_cast<u32>(draw==255 || !resources ? 1.f : 2.f);pushed.AddOffset(0,4);
            // Production ordering: constants, descriptors, then the graphics pipeline.
            cmd.pushConstants(*layout,vk::ShaderStageFlagBits::eAllGraphics,0,sizeof(pushed),&pushed,d);
            if(resources)cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,*layout,0,descriptor_set,{},d);
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,*pipeline,d);cmd.draw(3,1,0,0,d);
        }
        cmd.endRendering(d);
        const vk::ImageMemoryBarrier2 copy{.srcStageMask=vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask=vk::AccessFlagBits2::eColorAttachmentWrite,.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask=vk::AccessFlagBits2::eTransferRead,.oldLayout=vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout=vk::ImageLayout::eTransferSrcOptimal,.image=image,.subresourceRange=range};
        cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&copy},d);
        const vk::BufferImageCopy region{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={size,size,1}};
        cmd.copyImageToBuffer(image,vk::ImageLayout::eTransferSrcOptimal,buffer,region,d);
        const vk::MemoryBarrier2 barrier{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead};
        cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&barrier},d);scheduler.Finish();
        auto* values=static_cast<float*>(mapped.pMappedData);u32 checked=0;
        for (u32 y=0;y<size;++y) for(u32 x=0;x<size;++x) {
            const float a=((x+.5f)/size*2-1+.8f)/1.6f,b=((y+.5f)/size*2-1+.8f)/1.6f;
            if(a<.04f||b<.04f||a+b>.96f)continue;
            const float q=(1-a-b)+a/2+b/4;
            const std::array<float,4> expected{a/2/q,b/4/q,a,b};
            for(u32 c=0;c<4;++c) {
                const float actual=values[(y*size+x)*4+c];
                if (!std::isfinite(actual)||std::abs(actual-expected[c])>0.0001f) {
                    std::fprintf(stderr,"FAIL mode %u pixel %u,%u channel %u actual %.7f expected %.7f\n",u32(linear),x,y,c,actual,expected[c]);return 1;
                }
            }
            ++checked;
        }
        assert(checked>200);std::printf("PASS: barycentric mode %u, %u pixels, nonuniform W, perspective + linear readback%s\n",u32(linear),checked,resources ? "; 256 draws, split-stage descriptors, constant/vector buffer reads and pushed offsets" : "");
    }
    if(input_mem)vmaDestroyBuffer(instance.GetAllocator(),input,input_mem);
    view.reset();vmaDestroyImage(instance.GetAllocator(),raw_image,image_mem);vmaDestroyBuffer(instance.GetAllocator(),buffer,memory);
}
