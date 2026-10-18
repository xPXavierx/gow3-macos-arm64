// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the production sRGB lowering and compare with a double-precision reference.
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/reinterpret.h"
#include "shader_recompiler/specialization.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include <vk_mem_alloc.h>
// Rebinding a float view or a custom-border sampler must not reuse code whose
// color conversion assumes [0,1]. Exercise keys from real resource descriptors.
static void CheckRangeSpecialization() {
 using namespace Shader;
 Info info{};info.hw_stage=HwStage::Compute;info.sw_stage=SwStage::Compute;
 auto image=AmdGpu::Image::Null(false);image.base_address=0x100;
 image.data_format=u64(AmdGpu::DataFormat::Format16_16_16_16);
 image.num_format=u64(AmdGpu::NumberFormat::Unorm);
 info.images.emplace_back();info.samplers.emplace_back();
 AmdGpu::Sampler sampler{};sampler.force_degamma.Assign(1);
 const auto bind=[&] {
  std::memcpy(info.images[0].sharp_fetch.immediates.data(),&image,sizeof(image));
  std::memcpy(info.samplers[0].sharp_fetch.immediates.data(),&sampler,sizeof(sampler));
 };
 RuntimeInfo runtime{};runtime.Initialize(info.hw_stage,info.sw_stage);
 Profile profile{};Backend::Bindings bindings{};bind();
 StageSpecialization normalized(info,runtime,profile,bindings);
 assert(normalized.images[0].has_unorm_sample_range);
 image.num_format=u64(AmdGpu::NumberFormat::Float);bind();
 StageSpecialization hdr(info,runtime,profile,bindings);
 assert(!hdr.images[0].has_unorm_sample_range && !(hdr==normalized) && !(normalized==hdr));
 image.num_format=u64(AmdGpu::NumberFormat::Unorm);image.data_format=u64(AmdGpu::DataFormat::FormatBc6);bind();
 StageSpecialization bc6(info,runtime,profile,bindings);
 assert(!bc6.images[0].has_unorm_sample_range && !(bc6==normalized));
 image.data_format=u64(AmdGpu::DataFormat::Format16_16_16_16);
 sampler.border_color_type.Assign(AmdGpu::BorderColor::Custom);bind();
 StageSpecialization custom(info,runtime,profile,bindings);
 assert(custom.samplers[0].has_custom_border && !(custom==normalized) && !(normalized==custom));
 sampler.border_color_type.Assign(AmdGpu::BorderColor::TransparentBlack);bind();
 StageSpecialization again(info,runtime,profile,bindings);assert(again==normalized && normalized==again);
 std::puts("PASS: normalized/HDR/BC6/custom-border shader permutations remain distinct on resource rebinding");
}
int main(int argc,char** argv) {
 using namespace Shader;
 CheckRangeSpecialization();
 constexpr unsigned N=1048576;
 const bool normalized=argc>2 && std::strcmp(argv[2],"--unorm")==0;
 Info info{};info.hw_stage=HwStage::Compute;info.sw_stage=SwStage::Compute;
 info.loads.Set(IR::Attribute::WorkgroupId);info.loads.Set(IR::Attribute::LocalInvocationId);
 info.buffers.push_back({.used_types=IR::Type::U32,.buffer_type=BufferType::Guest});
 info.buffers.push_back({.used_types=IR::Type::U32,.buffer_type=BufferType::Guest,.is_written=true});
 RuntimeInfo runtime_info{};runtime_info.Initialize(info.hw_stage,info.sw_stage);runtime_info.hw.cs.workgroup_size={64,1,1};
 runtime_info.props.explicit_rounded_math=argc>3 && std::strcmp(argv[3],"--rounded")==0;
 Common::ObjectPool<IR::Inst> instructions;IR::Block block(instructions);IR::IREmitter ir(block);ir.Prologue();
 const auto index=ir.IAdd(ir.IMul(ir.GetAttributeU32(IR::Attribute::WorkgroupId),ir.Imm32(64u)),ir.GetAttributeU32(IR::Attribute::LocalInvocationId));
 const auto input=ir.BitCast<IR::F32>(IR::U32{ir.LoadBufferU32(1,ir.Imm32(0u),index,{})});
 const auto rgba=ir.CompositeConstruct(input,input,input,ir.Imm32(0.33333334f));
 const auto converted=IR::ApplyForceDegamma(ir,rgba,normalized);
 for(unsigned k=0;k<4;++k) ir.StoreBufferU32(1,ir.Imm32(1u),ir.IAdd(ir.IMul(index,ir.Imm32(4u)),ir.Imm32(k)),ir.BitCast<IR::U32>(IR::F32{ir.CompositeExtract(converted,k)}),{});
 IR::Program program(info);program.blocks.push_back(&block);
 program.syntax_list.push_back({.data={.block=&block},.type=IR::AbstractSyntaxNode::Type::Block});program.syntax_list.push_back({.type=IR::AbstractSyntaxNode::Type::Return});
 Profile profile{};profile.supported_spirv=0x00010600;profile.manual_buffer_bounds=true;profile.needs_buffer_offsets=true;profile.support_fp32_signed_zero_inf_nan_preserve=true;
 Backend::Bindings shader_bindings{};const auto code=Backend::SPIRV::EmitSPIRV(profile,runtime_info,program,shader_bindings);
 if(argc>1){std::ofstream f(argv[1],std::ios::binary);f.write(reinterpret_cast<const char*>(code.data()),code.size()*4);assert(f);}
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

 for(unsigned i=4096;i<N;++i) values[i]=float(i-4096)/float(N-4096-1);
 // Dense coverage above the sRGB interval, negative values, threshold neighbours,
 // exact zeros, infinities and NaNs. Alpha must pass through unchanged.
 for(unsigned i=0;i<4096;++i) values[i]=float(i)*64.f/4095.f;
 const std::array<float,12> edges{-1.f,-0.f,0.f,std::nextafter(0.04045f,0.f),0.04045f,std::nextafter(0.04045f,1.f),1.f,16.f,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),1e-38f};
 for(unsigned i=0;i<edges.size();++i) values[i]=edges[i];
 if(normalized) {
  for(unsigned i=0;i<N;++i) values[i]=float(i)/float(N-1);
  const std::array<float,10> unit_edges{0.f,1.f,0.04045f,
   std::nextafter(0.04045f,0.f),std::nextafter(0.04045f,1.f),0.35f,
   std::nextafter(0.35f,0.f),std::nextafter(0.35f,1.f),1e-38f,-0.f};
  for(unsigned i=0;i<unit_edges.size();++i) values[i]=unit_edges[i];
 }
 std::fill_n(values+N,N*4,-123.f);
 const std::array<vk::DescriptorBufferInfo,2> infos{{{buffer,0,N*4},{buffer,N*4,N*16}}};
 std::array<vk::WriteDescriptorSet,2> writes{};
 const auto set=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*descriptors},d).value[0];
 for(unsigned i=0;i<2;++i) writes[i]={.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=vk::DescriptorType::eStorageBuffer,.pBufferInfo=&infos[i]};
 device.updateDescriptorSets(writes,{},d);
 const auto cmd=scheduler.CommandBuffer();cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline,d);cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*layout,0,set,{},d);
 Shader::PushData push_data{};cmd.pushConstants(*layout,vk::ShaderStageFlagBits::eCompute,0,sizeof(push_data),&push_data,d);cmd.dispatch(N/64,1,1,d);
 const vk::MemoryBarrier2 barrier{.srcStageMask=vk::PipelineStageFlagBits2::eComputeShader,.srcAccessMask=vk::AccessFlagBits2::eShaderWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead};cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&barrier},d);scheduler.Finish();
 double max_abs=0,max_rel=0;unsigned failed=0;
 for(unsigned i=0;i<N;++i) {
  const double c=values[i];const double expected=c>double(0.04045f) ? std::pow((c+0.055)/1.055,2.4) : c/12.92;
  for(unsigned k=0;k<3;++k) {
   const float actual=values[N+4*i+k];
   bool ok=std::isnan(expected) ? std::isnan(actual) : std::isinf(expected) ? actual==expected : std::abs(double(actual)-expected)<=(normalized ? 5e-7 : 2e-6)*std::max(1.,std::abs(expected));
   if(!ok && failed++<5)std::fprintf(stderr,"gamma input %.9g got %.9g expected %.17g\n",c,actual,expected);
   if(std::isfinite(expected)){max_abs=std::max(max_abs,std::abs(double(actual)-expected));if(c<=1)max_rel=std::max(max_rel,std::abs(double(actual)-expected));}
  }
  if(std::bit_cast<unsigned>(values[N+4*i+3])!=std::bit_cast<unsigned>(0.33333334f)) ++failed;
 }
 vmaDestroyBuffer(instance.GetAllocator(),buffer,allocation);
 std::printf("sRGB GPU reference: %u inputs, normalized max abs %.9g, full-range max abs %.9g, failures %u\n",N,max_rel,max_abs,failed);
 std::printf("sRGB path: %s\n",normalized ? "bounded normalized texture" : "full-range reference");
 assert(!failed);std::puts(normalized ? "PASS: bounded normalized sRGB curve on GPU, thresholds and exact alpha" : "PASS: full-range sRGB curve on GPU: HDR/negative/threshold/special values and exact alpha");
}
