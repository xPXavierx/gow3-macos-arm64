// SPDX-License-Identifier: GPL-2.0-or-later
// Compare precise scalar lowering on the GPU, including independent rounding boundaries.
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/reinterpret.h"
#include "shader_recompiler/specialization.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include <vk_mem_alloc.h>
static std::vector<u32> RunVariant(bool optimized, const char* path) {
 using namespace Shader;

 constexpr unsigned N=262144;
 Info info{};info.hw_stage=HwStage::Compute;info.sw_stage=SwStage::Compute;
 info.loads.Set(IR::Attribute::WorkgroupId);info.loads.Set(IR::Attribute::LocalInvocationId);
 info.buffers.push_back({.used_types=IR::Type::U32,.buffer_type=BufferType::Guest});
 info.buffers.push_back({.used_types=IR::Type::U32,.buffer_type=BufferType::Guest,.is_written=true});
 RuntimeInfo runtime_info{};runtime_info.Initialize(info.hw_stage,info.sw_stage);runtime_info.hw.cs.workgroup_size={64,1,1};
 runtime_info.props.explicit_rounded_math=optimized;
 auto baseline=runtime_info;baseline.props.explicit_rounded_math=!optimized;
 Backend::Bindings key_bindings{};Profile key_profile{};
 assert(!(StageSpecialization(info,runtime_info,key_profile,key_bindings)==StageSpecialization(info,baseline,key_profile,key_bindings)));
 Common::ObjectPool<IR::Inst> instructions;IR::Block block(instructions);IR::IREmitter ir(block);ir.Prologue();
 const auto index=ir.IAdd(ir.IMul(ir.GetAttributeU32(IR::Attribute::WorkgroupId),ir.Imm32(64u)),ir.GetAttributeU32(IR::Attribute::LocalInvocationId));
 const auto input=ir.BitCast<IR::F32>(IR::U32{ir.LoadBufferU32(1,ir.Imm32(0u),index,{})});
 const IR::F32 y{ir.BitCast<IR::F32>(IR::U32{ir.LoadBufferU32(1,ir.Imm32(0u),ir.IAdd(index,ir.Imm32(N)),{})})};
 const IR::F32 z{ir.BitCast<IR::F32>(IR::U32{ir.LoadBufferU32(1,ir.Imm32(0u),ir.IAdd(index,ir.Imm32(N*2)),{})})};
 const auto value=ir.CompositeConstruct(IR::F32{ir.FPAdd(input,y)},IR::F32{ir.FPSub(input,y)},IR::F32{ir.FPMul(input,y)},IR::F32{ir.FPAdd(ir.FPMul(input,y),z)});
 for(u32 k=0;k<4;++k)ir.StoreBufferU32(1,ir.Imm32(1u),ir.IAdd(ir.IMul(index,ir.Imm32(4u)),ir.Imm32(k)),ir.BitCast<IR::U32>(IR::F32{ir.CompositeExtract(value,k)}),{});
 IR::Program program(info);program.blocks.push_back(&block);
 program.syntax_list.push_back({.data={.block=&block},.type=IR::AbstractSyntaxNode::Type::Block});program.syntax_list.push_back({.type=IR::AbstractSyntaxNode::Type::Return});
 Profile profile{};profile.supported_spirv=0x00010600;profile.manual_buffer_bounds=true;profile.needs_buffer_offsets=true;profile.support_fp32_signed_zero_inf_nan_preserve=true;
 Backend::Bindings shader_bindings{};const auto code=Backend::SPIRV::EmitSPIRV(profile,runtime_info,program,shader_bindings);
 if(path){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(code.data()),code.size()*4);assert(f);}
    Vulkan::Instance instance(0, std::getenv("GOW3_VK_VALIDATION")!=nullptr);
    vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance()); d.init(instance.GetDevice());
    const auto device = instance.GetDevice();
    Vulkan::Scheduler scheduler(instance);

    const VkBufferCreateInfo bi{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=N*28,
        .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT |
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT, .usage=VMA_MEMORY_USAGE_AUTO,
        .requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    VkBuffer buffer; VmaAllocation allocation; VmaAllocationInfo ai;
    assert(vmaCreateBuffer(instance.GetAllocator(), &bi, &ac, &buffer, &allocation, &ai)==VK_SUCCESS);
    auto* values=static_cast<float*>(ai.pMappedData);

    std::array<vk::DescriptorSetLayoutBinding,2> bindings{};
    for(unsigned i=0;i<2;++i) bindings[i]={.binding=i,.descriptorType=vk::DescriptorType::eStorageBuffer,
        .descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute};
    auto descriptors=device.createDescriptorSetLayoutUnique({
        .flags={},
        .bindingCount=2,.pBindings=bindings.data()},nullptr,d).value;
    const vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer,6};
    auto pool=device.createDescriptorPoolUnique({.maxSets=3,.poolSizeCount=1,.pPoolSizes=&pool_size},nullptr,d).value;
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute,0,sizeof(Shader::PushData)};
    auto layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*descriptors,
        .pushConstantRangeCount=1,.pPushConstantRanges=&push},nullptr,d).value;
    auto module=device.createShaderModuleUnique({.codeSize=code.size()*4,.pCode=code.data()},nullptr,d).value;
    const vk::ComputePipelineCreateInfo pipeline_ci{
        .stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=*module,.pName="main"},
        .layout=*layout};
    auto pipeline=device.createComputePipelineUnique({},pipeline_ci,nullptr,d).value;

 auto* bits=reinterpret_cast<u32*>(values);u32 random=123;
 for(u32 i=0;i<N*3;++i){random^=random<<13;random^=random>>17;random^=random<<5;bits[i]=random;}
 // Values around cancellation distinguish the original two-rounding chain from
 // accidentally contracting multiply/add into a single fma.
 const std::array<float,10> edge{0.f,-0.f,1.f,-1.f,1.0000001192092896f,.9999998807907104f,1e-38f,-1e-38f,1e30f,-1e30f};
 for(u32 i=0;i<1000;++i){values[i]=edge[i%10];values[N+i]=edge[(i/10)%10];values[N*2+i]=edge[(i/100)%10];}
 values[1000]=1.0000001192092896f;values[N+1000]=.9999998807907104f;values[N*2+1000]=-1.f;
 std::fill_n(values+N*3,N*4,-123.f);
 const std::array<vk::DescriptorBufferInfo,2> infos{{{buffer,0,N*12},{buffer,N*12,N*16}}};
 std::array<vk::WriteDescriptorSet,2> writes{};
 const auto set=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*descriptors},d).value[0];
 for(unsigned i=0;i<2;++i) writes[i]={.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=vk::DescriptorType::eStorageBuffer,.pBufferInfo=&infos[i]};
 device.updateDescriptorSets(writes,{},d);
 const auto cmd=scheduler.CommandBuffer();cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline,d);cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*layout,0,set,{},d);
 Shader::PushData push_data{};cmd.pushConstants(*layout,vk::ShaderStageFlagBits::eCompute,0,sizeof(push_data),&push_data,d);cmd.dispatch(N/64,1,1,d);
 const vk::MemoryBarrier2 barrier{.srcStageMask=vk::PipelineStageFlagBits2::eComputeShader,.srcAccessMask=vk::AccessFlagBits2::eShaderWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead};cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&barrier},d);scheduler.Finish();
 const float cancellation=values[N*3+1000*4+3];assert(cancellation==0.f);
 std::printf("PASS: optimized=%d, %u random/edge arithmetic results; multiply-add cancellation preserved at %.9g\n",optimized,N*4,cancellation);
 std::vector<u32> output(N*4);std::memcpy(output.data(),values+N*3,output.size()*sizeof(u32));
 vmaDestroyBuffer(instance.GetAllocator(),buffer,allocation);
 return output;
}
int main(int argc,char** argv) {
 const std::string reference_path=argc>1?std::string(argv[1])+".reference.spv":"";
 const std::string optimized_path=argc>1?std::string(argv[1])+".optimized.spv":"";
 const auto reference=RunVariant(false,argc>1?reference_path.c_str():nullptr);
 const auto optimized=RunVariant(true,argc>1?optimized_path.c_str():nullptr);
 assert(reference.size()==optimized.size());u32 nan_payload_differences=0;
 for(size_t i=0;i<reference.size();++i) {
  if(reference[i]==optimized[i])continue;
  if(std::isnan(std::bit_cast<float>(reference[i])) && std::isnan(std::bit_cast<float>(optimized[i]))) {
   ++nan_payload_differences;continue;
  }
  std::fprintf(stderr,"Scalar math mismatch at %zu: reference=%08x optimized=%08x\n",i,reference[i],optimized[i]);
  std::abort();
 }
 std::printf("PASS: %zu GPU results, finite/infinite values bit-identical, NaN classifications identical (%u payload differences)\n",reference.size(),nan_payload_differences);
}
