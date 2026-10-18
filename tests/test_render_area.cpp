// SPDX-License-Identifier: GPL-2.0-or-later
// A discarded draw must retain vertex storage writes when its render area shrinks.
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <cstring>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_draw_effects.h"
#include <vk_mem_alloc.h>
int main() {
 using namespace Shader;
 Vulkan::RenderState area{};area.width=area.height=16384;area.num_layers=1;
 assert(Vulkan::BoundDiscardedRenderArea(area,false)==area);
 auto small=Vulkan::BoundDiscardedRenderArea(area,true);assert(small.width==1&&small.height==1);
 area.depth_stencil_attachment.has_depth=true;assert(Vulkan::BoundDiscardedRenderArea(area,true)==area);
 area.depth_stencil_attachment.has_depth=false;area.depth_stencil_attachment.has_stencil=true;assert(Vulkan::BoundDiscardedRenderArea(area,true)==area);
 area.depth_stencil_attachment={};area.num_color_attachments=3;area.color_attachments[2].image_view=vk::ImageView{reinterpret_cast<VkImageView>(uintptr_t(1))};assert(Vulkan::BoundDiscardedRenderArea(area,true)==area);
 area.color_attachments[2].image_view=nullptr;area.num_color_attachments=0;
 Info pure{};
 std::array<const Info*,MaxStageTypes-1> effects{};
 effects[u32(SwStage::Vertex)]=&pure;
 assert(Vulkan::CanElideVertexOnlyDraw(area,effects,false));
 assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,true));
 effects[u32(SwStage::Fragment)]=&pure;assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,false));effects[u32(SwStage::Fragment)]=nullptr;
 pure.buffers.push_back({.used_types=IR::Type::U32,.buffer_type=BufferType::Guest,.is_written=true});assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,false));pure.buffers.clear();
 for (const auto type : {BufferType::GdsBuffer, BufferType::SharedMemory, BufferType::FaultBuffer}) {
  pure.buffers.push_back({.used_types=IR::Type::U32,.buffer_type=type});
  assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,false));pure.buffers.clear();
 }
 pure.translation_failed=true;assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,false));pure.translation_failed=false;
 pure.uses_buffer_int64_atomics=true;assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,false));pure.uses_buffer_int64_atomics=false;
 pure.has_storage_images=true;assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,false));pure.has_storage_images=false;
 pure.images.push_back({.is_written=true});assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,false));pure.images.clear();
 pure.images.push_back({.is_atomic=true});assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,false));pure.images.clear();
 pure.uses_dma=true;assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,false));pure.uses_dma=false;
 area.depth_stencil_attachment.has_depth=true;assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,false));area.depth_stencil_attachment={};
 constexpr unsigned N=16;
 Info info{};info.hw_stage=HwStage::Vertex;info.sw_stage=SwStage::Vertex;
 info.loads.Set(IR::Attribute::VertexId);
 info.buffers.push_back({.used_types=IR::Type::U32,.buffer_type=BufferType::Guest});
 info.buffers.push_back({.used_types=IR::Type::U32,.buffer_type=BufferType::Guest,.is_written=true});
 effects[u32(SwStage::Vertex)]=&info;assert(!Vulkan::CanElideVertexOnlyDraw(area,effects,false));
 RuntimeInfo runtime_info{};runtime_info.Initialize(info.hw_stage,info.sw_stage);
 Common::ObjectPool<IR::Inst> instructions;IR::Block block(instructions);IR::IREmitter ir(block);ir.Prologue();
 const auto index=ir.GetAttributeU32(IR::Attribute::VertexId);
 const auto input=IR::U32{ir.LoadBufferU32(1,ir.Imm32(0u),index,{})};
 ir.StoreBufferU32(1,ir.Imm32(1u),index,ir.IAdd(ir.IMul(input,ir.Imm32(3u)),ir.Imm32(7u)),{});
 for(unsigned c=0;c<4;++c) {info.stores.Set(IR::Attribute::Position0,c);ir.SetAttribute(IR::Attribute::Position0,ir.Imm32(c==3 ? 1.f : 0.f),c);}
 IR::Program program(info);program.blocks.push_back(&block);program.syntax_list.push_back({.data={.block=&block},.type=IR::AbstractSyntaxNode::Type::Block});program.syntax_list.push_back({.type=IR::AbstractSyntaxNode::Type::Return});
 Profile profile{};profile.supported_spirv=0x00010600;profile.manual_buffer_bounds=true;profile.needs_buffer_offsets=true;
 Backend::Bindings shader_bindings{};const auto code=Backend::SPIRV::EmitSPIRV(profile,runtime_info,program,shader_bindings);
    Vulkan::Instance instance(0, false);
    vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance()); d.init(instance.GetDevice());
    const auto device = instance.GetDevice();
    Vulkan::Scheduler scheduler(instance);

    const VkBufferCreateInfo bi{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=N*20,
        .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT |
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT, .usage=VMA_MEMORY_USAGE_AUTO,
        .requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    VkBuffer buffer; VmaAllocation allocation; VmaAllocationInfo ai;
    assert(vmaCreateBuffer(instance.GetAllocator(), &bi, &ac, &buffer, &allocation, &ai)==VK_SUCCESS);
    auto* values=static_cast<float*>(ai.pMappedData);

    std::array<vk::DescriptorSetLayoutBinding,2> bindings{};
    for(unsigned i=0;i<2;++i) bindings[i]={.binding=i,.descriptorType=vk::DescriptorType::eStorageBuffer,
        .descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eVertex};
    auto descriptors=device.createDescriptorSetLayoutUnique({
        .flags={},
        .bindingCount=2,.pBindings=bindings.data()},nullptr,d).value;
    const vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer,6};
    auto pool=device.createDescriptorPoolUnique({.maxSets=3,.poolSizeCount=1,.pPoolSizes=&pool_size},nullptr,d).value;
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eVertex,0,sizeof(Shader::PushData)};
    auto layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*descriptors,
        .pushConstantRangeCount=1,.pPushConstantRanges=&push},nullptr,d).value;
    auto module=device.createShaderModuleUnique({.codeSize=code.size()*4,.pCode=code.data()},nullptr,d).value;

 const vk::PipelineVertexInputStateCreateInfo vi{};
 const vk::PipelineInputAssemblyStateCreateInfo ia{.topology=vk::PrimitiveTopology::eTriangleList};
 const vk::Viewport viewport{0,0,32,32,0,1};const vk::Rect2D scissor{{0,0},{32,32}};
 const vk::PipelineViewportStateCreateInfo vp{.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
 vk::PipelineRasterizationStateCreateInfo raster{.rasterizerDiscardEnable=false,.polygonMode=vk::PolygonMode::eFill,.lineWidth=1};
 const vk::PipelineMultisampleStateCreateInfo ms{.rasterizationSamples=vk::SampleCountFlagBits::e1};
 const vk::PipelineColorBlendStateCreateInfo blend{};const vk::PipelineRenderingCreateInfo rendering{};
 const vk::PipelineShaderStageCreateInfo stage{.stage=vk::ShaderStageFlagBits::eVertex,.module=*module,.pName="main"};
 const vk::GraphicsPipelineCreateInfo ci{.pNext=&rendering,.stageCount=1,.pStages=&stage,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&raster,.pMultisampleState=&ms,.pColorBlendState=&blend,.layout=*layout};
 auto original_pipeline=device.createGraphicsPipelineUnique({},ci,nullptr,d).value;
 raster.rasterizerDiscardEnable=true;
 auto discarded_pipeline=device.createGraphicsPipelineUnique({},ci,nullptr,d).value;
 const std::array<vk::DescriptorBufferInfo,2> infos{{{buffer,0,N*4},{buffer,N*4,N*16}}};
 const auto set=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*descriptors},d).value[0];
 std::array<vk::WriteDescriptorSet,2> writes{};
 for(unsigned i=0;i<2;++i) writes[i]={.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=vk::DescriptorType::eStorageBuffer,.pBufferInfo=&infos[i]};device.updateDescriptorSets(writes,{},d);
 std::array<unsigned,16> reference{};
 auto* words=reinterpret_cast<unsigned*>(values);
 for(bool bounded:{false,true}) {
  for(unsigned i=0;i<N;++i)words[i]=i+10;
  std::fill_n(words+N,N*4,0xdeadbeefu);
  const auto state=Vulkan::BoundDiscardedRenderArea(area,bounded);
  const auto cmd=scheduler.CommandBuffer();cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,bounded ? *discarded_pipeline : *original_pipeline,d);cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,*layout,0,set,{},d);
  Shader::PushData data{};cmd.pushConstants(*layout,vk::ShaderStageFlagBits::eVertex,0,sizeof(data),&data,d);
  cmd.beginRendering({.renderArea={{0,0},{state.width,state.height}},.layerCount=1},d);cmd.draw(3,1,0,0,d);cmd.endRendering(d);
  const vk::MemoryBarrier2 barrier{.srcStageMask=vk::PipelineStageFlagBits2::eVertexShader,.srcAccessMask=vk::AccessFlagBits2::eShaderWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead};cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&barrier},d);scheduler.Finish();
  for(unsigned i=0;i<16;++i)assert(words[N+i]==(i<3 ? (i+10)*3+7 : 0xdeadbeefu));
  if(!bounded)std::copy_n(words+N,16,reference.begin());else assert(!std::memcmp(reference.data(),words+N,64));
 }
 vmaDestroyBuffer(instance.GetAllocator(),buffer,allocation);
 std::puts("PASS: attachmentless discarded render area 16384x16384 -> 1x1 preserves exact vertex writes and guards; depth/stencil/color/rasterized cases retain original extent");
}
