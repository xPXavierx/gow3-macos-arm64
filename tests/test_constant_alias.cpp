// SPDX-License-Identifier: GPL-2.0-or-later
// Exact production constant reads: uniform alias, storage fallback and runtime bounds.
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "shader_recompiler/specialization.h"
#include <fstream>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include <vk_mem_alloc.h>

static std::vector<u32> MakeCode(bool shared) {
    using namespace Shader;
    constexpr std::array<u32, 8> indices{0, 1, 3, 4, 15, 31, 32, 0xffffffffu};
    Info info{}; info.hw_stage=HwStage::Compute; info.sw_stage=SwStage::Compute;
    info.buffers.push_back({.used_types=IR::Type::U32, .buffer_type=BufferType::Guest,
                           .uniform_span=UniformAliasSpan(32)});
    info.buffers.push_back({.used_types=IR::Type::U32, .buffer_type=BufferType::Guest,
                           .is_written=true});
    RuntimeInfo ri{}; ri.Initialize(info.hw_stage,info.sw_stage); ri.hw.cs.workgroup_size={1,1,1};
    Common::ObjectPool<IR::Inst> pool; IR::Block block(pool); IR::IREmitter ir(block);
    ir.Prologue();
    for(u32 i=0;i<indices.size();++i) {
        const auto read=ir.ReadConstBuffer(ir.Imm32(0u),ir.Imm32(indices[i]),{});
        ir.StoreBufferU32(1,ir.Imm32(1u),ir.Imm32(i),read,{});
    }
    IR::Program program(info); program.blocks.push_back(&block);
    program.syntax_list.push_back({.data={.block=&block},.type=IR::AbstractSyntaxNode::Type::Block});
    program.syntax_list.push_back({.type=IR::AbstractSyntaxNode::Type::Return});
    Profile profile{}; profile.supported_spirv=0x00010600; profile.manual_buffer_bounds=true;
    profile.needs_buffer_offsets=true; profile.native_constant_buffers=true;
    AmdGpu::Buffer sharp{}; sharp.base_address=0x100; sharp.num_records=256; sharp.mtype=shared?3:0;
    std::memcpy(info.buffers[0].sharp_fetch.immediates.data(),&sharp,sizeof(sharp));
    Backend::Bindings sb{};
    StageSpecialization key(info,ri,profile,sb);
    assert(bool(key.buffers[0].shared_constants)==shared);
    const auto code=Backend::SPIRV::EmitSPIRV(profile,ri,program,sb);
    assert(sb.buffer==2 && sb.unified==3);
    return code;
}
int main(int argc, char** argv) {
    using namespace Shader;
    constexpr std::array<u32,8> indices{0,1,3,4,15,31,32,0xffffffffu};
    const std::array<std::vector<u32>,2> codes{MakeCode(false),MakeCode(true)};
    if(argc>1) for(u32 i=0;i<2;++i) {
        std::ofstream f(std::string(argv[1])+(i?".shared.spv":""),std::ios::binary);
        f.write((const char*)codes[i].data(),codes[i].size()*4);assert(f);
    }
    Vulkan::Instance instance(0,std::getenv("GOW3_VK_VALIDATION")!=nullptr);
    vk::detail::DynamicLoader loader; vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance());d.init(instance.GetDevice());
    const auto device=instance.GetDevice(); Vulkan::Scheduler scheduler(instance);
    const VkBufferCreateInfo bc{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=4096,
        .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage=VMA_MEMORY_USAGE_AUTO,.requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    VkBuffer raw;VmaAllocation allocation;VmaAllocationInfo mapped;
    assert(vmaCreateBuffer(instance.GetAllocator(),&bc,&ac,&raw,&allocation,&mapped)==VK_SUCCESS);
    const vk::Buffer buffer=raw;auto* words=static_cast<u32*>(mapped.pMappedData);
    std::array<vk::DescriptorSetLayoutBinding,3> bindings{};
    for(u32 i=0;i<3;++i) bindings[i]={.binding=i,
        .descriptorType=i==1?vk::DescriptorType::eUniformBuffer:vk::DescriptorType::eStorageBuffer,
        .descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute};
    auto descriptors=device.createDescriptorSetLayoutUnique({.bindingCount=3,.pBindings=bindings.data()},nullptr,d).value;
    const std::array<vk::DescriptorPoolSize,2> sizes{{{vk::DescriptorType::eStorageBuffer,2},{vk::DescriptorType::eUniformBuffer,1}}};
    auto dp=device.createDescriptorPoolUnique({.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes.data()},nullptr,d).value;
    const auto set=device.allocateDescriptorSets({.descriptorPool=*dp,.descriptorSetCount=1,.pSetLayouts=&*descriptors},d).value[0];
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute,0,sizeof(PushData)};
    auto layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*descriptors,
        .pushConstantRangeCount=1,.pPushConstantRanges=&push},nullptr,d).value;
    std::vector<vk::UniqueShaderModule> modules;
    std::vector<vk::UniquePipeline> pipelines;
    for(const auto& code:codes) {
        modules.push_back(device.createShaderModuleUnique({.codeSize=code.size()*4,.pCode=code.data()},nullptr,d).value);
        pipelines.push_back(device.createComputePipelineUnique({}, {.stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=*modules.back(),.pName="main"},.layout=*layout},nullptr,d).value);
    }
    unsigned cases=0;
    for(bool gpu_fill:{false,true}) for(bool uniform:{false,true})
    for(u32 count:{0u,1u,4u,32u,64u}) for(u32 offset:{0u,4u,12u,60u,124u}) {
        for(u32 i=0;i<64;++i)words[i]=0x91000000u+i;
        std::fill_n(words+256,indices.size(),0xdeaddeadu);
        std::fill_n(words+512,64,0u); // wrong alias data must remain unused in fallback mode
        const std::array<vk::DescriptorBufferInfo,3> bi{{
            {buffer,0,count?count*4u:1u}, {buffer,uniform?0u:2048u,count?count*4u:1u},
            {buffer,1024,indices.size()*4}}};
        std::array<vk::WriteDescriptorSet,3> writes{};
        for(u32 i=0;i<3;++i) writes[i]={.dstSet=set,.dstBinding=i,.descriptorCount=1,
            .descriptorType=bindings[i].descriptorType,.pBufferInfo=&bi[i]};
        device.updateDescriptorSets(writes,{},d);
        const auto cmd=scheduler.CommandBuffer();
        if(gpu_fill) {
            cmd.fillBuffer(buffer,0,256,0xaabbccddu,d);
            const vk::MemoryBarrier2 ready{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,
                .dstAccessMask=vk::AccessFlagBits2::eUniformRead|vk::AccessFlagBits2::eShaderRead};
            cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&ready},d);
        }
        PushData data{};data.AddOffset(0,offset);
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipelines[uniform?0:1],d);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*layout,0,set,{},d);
        cmd.pushConstants(*layout,vk::ShaderStageFlagBits::eCompute,0,sizeof(data),&data,d);
        cmd.dispatch(1,1,1,d);
        const vk::MemoryBarrier2 ready{.srcStageMask=vk::PipelineStageFlagBits2::eComputeShader,.srcAccessMask=vk::AccessFlagBits2::eShaderWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead};
        cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&ready},d);scheduler.Finish();
        for(u32 i=0;i<indices.size();++i) {
            const u64 index=u64(indices[i])+offset/4;
            const u32 expected=index<count ? (gpu_fill?0xaabbccddu:0x91000000u+u32(index)) : 0;
            if(words[256+i]!=expected)std::fprintf(stderr,"constant alias count=%u offset=%u uniform=%d gpu=%d index=%u got=%x expected=%x\n",count,offset,uniform,gpu_fill,indices[i],words[256+i],expected);
            assert(words[256+i]==expected);
        }
        ++cases;
    }
    vmaDestroyBuffer(instance.GetAllocator(),raw,allocation);
    std::printf("PASS: %u exact constant-alias cases: shared-storage fallback, short/null bounds, offset overflow, nonzero offsets and GPU-written data\n",cases);
}
