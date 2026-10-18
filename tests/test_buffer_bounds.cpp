// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the same guard emitted for guest buffers on the actual Vulkan device.
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_null_resources.h"
#include <vk_mem_alloc.h>
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/page_manager.h"
#include "video_core/buffer_cache/buffer_cache.h"

namespace VideoCore {
struct BufferCacheTestAccess {
    static const Buffer* Resident(BufferCache& cache, VAddr address, u64 size) {
        const u64 first=address>>cache.block_shift, last=(address+size-1)>>cache.block_shift;
        const Buffer* arena=cache.GetArena(first,last);
        cache.EnsureResident(arena,first,last);
        return arena;
    }
};
struct CacheFixture {
    PageManager tracker{nullptr};
    BufferCache cache;
    TextureCache textures;
    CacheFixture(Vulkan::Instance& instance, Vulkan::Scheduler& scheduler, Vulkan::Runtime& runtime)
        : cache(instance,scheduler,runtime,nullptr,textures,tracker),
          textures(instance,scheduler,runtime,nullptr,cache,tracker) {}
};
static void TestOrdinaryArena(Vulkan::Instance& instance, Vulkan::Scheduler& scheduler) {
    if(instance.IsSparseBufferSupported()) return;
    Vulkan::Runtime runtime(instance,scheduler);
    CacheFixture fixture(instance,scheduler,runtime);
    constexpr VAddr address=0x8000010000ull;
    constexpr u64 size=3*65536;
    const auto* arena=BufferCacheTestAccess::Resident(fixture.cache,address,size);
    assert(BufferCacheTestAccess::Resident(fixture.cache,address+65536,65536)==arena);
    Buffer readback(instance,0,size+32,MemoryType::HostCached);
    runtime.FillBuffer(arena,arena->Offset(address),size,0x12345678);
    const vk::BufferCopy data{arena->Offset(address),0,size};
    runtime.CopyBuffer(arena,&readback,std::span{&data,1});
    const auto shift=fixture.cache.GetSparsePageShift();
    assert(shift==16);
    const vk::BufferCopy table{(address>>shift)*8,size,24};
    runtime.CopyBuffer(fixture.cache.GetBdaPageTableBuffer(),&readback,std::span{&table,1});
    runtime.FlushBarriers();
    scheduler.Finish();
    readback.Invalidate(0,size+24);
    const auto* words=reinterpret_cast<const u32*>(readback.mapped_data.data());
    for(u64 i=0;i<size/4;++i) assert(words[i]==0x12345678);
    const auto* addresses=reinterpret_cast<const u64*>(readback.mapped_data.data()+size);
    for(u64 i=0;i<3;++i) assert(addresses[i]==arena->BufferDeviceAddress()+arena->Offset(address)+i*65536);
    std::puts("PASS: non-sparse arena fill/readback and GPU address-table entries; repeated range retains its buffer");
}
} // namespace VideoCore

int main(int argc, char** argv) {
    using namespace Shader;
    Info info{};
    info.hw_stage=HwStage::Compute; info.sw_stage=SwStage::Compute;
    for(unsigned i=0;i<2;++i) info.buffers.push_back({
        .used_types=IR::Type::U32,.buffer_type=BufferType::Guest,.is_written=true});
    info.buffers[0].used_types |= IR::Type::U8 | IR::Type::U16;
    RuntimeInfo runtime_info{};
    runtime_info.Initialize(info.hw_stage,info.sw_stage);
    runtime_info.hw.cs.workgroup_size={1,1,1};
    Common::ObjectPool<IR::Inst> instructions;
    IR::Block block(instructions);
    IR::IREmitter ir(block);
    ir.Prologue();
    const std::array<unsigned,4> indices{0,3,4,0xffffffffu};
    for(unsigned i=0;i<indices.size();++i) {
        const auto index=ir.Imm32(indices[i]);
        const auto read=ir.LoadBufferU32(1,ir.Imm32(0u),index,{});
        ir.StoreBufferU32(1,ir.Imm32(1u),ir.Imm32(i),read,{});
        ir.StoreBufferU32(1,ir.Imm32(0u),index,ir.Imm32(42u),{});
        const auto atomic=ir.BufferAtomicIAdd(ir.Imm32(0u),index,ir.Imm32(5u),{});
        ir.StoreBufferU32(1,ir.Imm32(1u),ir.Imm32(i+4),atomic,{});
    }
    // The second component must not wrap UINT32_MAX back to valid element zero.
    const auto wrapping=ir.LoadBufferU32(2,ir.Imm32(0u),ir.Imm32(0xffffffffu),{});
    ir.StoreBufferU32(2,ir.Imm32(1u),ir.Imm32(8u),wrapping,{});
    ir.StoreBufferU32(2,ir.Imm32(0u),ir.Imm32(0xffffffffu),
                     ir.CompositeConstruct(ir.Imm32(99u),ir.Imm32(99u)),{});
    // Scalar constant loads must honor the same descriptor range as vector loads.
    // Keep the first invalid index within the physical allocation to expose a leak
    // without relying on an unmapped GPU address.
    for (unsigned i = 0; i < 4; ++i) {
        const auto constant = ir.ReadConstBuffer(ir.Imm32(0u), ir.Imm32(indices[i]), {});
        ir.StoreBufferU32(1, ir.Imm32(1u), ir.Imm32(10u + i), constant, {});
    }
    ir.DeviceMemoryBarrier();
    ir.StoreBufferU32(1,ir.Imm32(1u),ir.Imm32(14u),
        IR::U32{ir.UConvert(32,ir.LoadBufferU8(ir.Imm32(0u),ir.Imm32(0u),{}))},{});
    ir.StoreBufferU32(1,ir.Imm32(1u),ir.Imm32(15u),
        IR::U32{ir.UConvert(32,ir.LoadBufferU16(ir.Imm32(0u),ir.Imm32(0u),{}))},{});
    IR::Program program(info);
    program.blocks.push_back(&block);
    program.syntax_list.push_back({.data={.block=&block},.type=IR::AbstractSyntaxNode::Type::Block});
    program.syntax_list.push_back({.type=IR::AbstractSyntaxNode::Type::Return});
    Profile profile{};
    profile.supported_spirv=0x00010600;
    profile.manual_buffer_bounds=true;
    profile.needs_buffer_offsets=true;
    Backend::Bindings shader_bindings{};
    const auto code=Backend::SPIRV::EmitSPIRV(profile,runtime_info,program,shader_bindings);
    if (argc > 1) {
        std::ofstream f(argv[1], std::ios::binary);
        f.write(reinterpret_cast<const char*>(code.data()), code.size() * 4);
        assert(f);
    }
    Vulkan::Instance instance(0, false);
    vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance()); d.init(instance.GetDevice());
    const auto device = instance.GetDevice();
    Vulkan::Scheduler scheduler(instance);
    Vulkan::NullResources null_resources(instance, scheduler);
    const VkBufferCreateInfo bi{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=512,
        .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT |
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT, .usage=VMA_MEMORY_USAGE_AUTO,
        .requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    VkBuffer buffer; VmaAllocation allocation; VmaAllocationInfo ai;
    assert(vmaCreateBuffer(instance.GetAllocator(), &bi, &ac, &buffer, &allocation, &ai)==VK_SUCCESS);
    auto* values=static_cast<unsigned*>(ai.pMappedData);
    std::fill_n(values,128,0xdeadbeefu);
    for(unsigned i=0;i<4;++i) values[i]=(i+1)*10;
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
    const auto run=[&](bool empty, unsigned source_offset = 0) {
        std::fill_n(values,128,0xdeadbeefu);
        for(unsigned i=0;i<4;++i) values[i]=(i+1)*10;
        // range=1 yields zero u32 elements, also exercising the null-buffer stand-in.
        const std::array<vk::DescriptorBufferInfo,2> infos{{
            {empty ? null_resources.Buffer() : vk::Buffer{buffer},0,empty ? 1u : 16u},
            {buffer,256,64}}};
        std::array<vk::WriteDescriptorSet,2> writes{};
        for(unsigned i=0;i<2;++i) writes[i]={.dstBinding=i,.descriptorCount=1,
            .descriptorType=vk::DescriptorType::eStorageBuffer,.pBufferInfo=&infos[i]};
        const auto set=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,
            .pSetLayouts=&*descriptors},d).value[0];
        for(auto& write:writes) write.dstSet=set;
        device.updateDescriptorSets(writes,{},d);
        const auto cmd=scheduler.CommandBuffer();
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline,d);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*layout,0,set,{},d);
        Shader::PushData data{};
        data.AddOffset(0, source_offset);
        cmd.pushConstants(*layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(data), &data, d);
        cmd.dispatch(1,1,1,d);
        const vk::MemoryBarrier2 barrier{.srcStageMask=vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask=vk::AccessFlagBits2::eShaderWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,
            .dstAccessMask=vk::AccessFlagBits2::eHostRead};
        cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&barrier},d);
        scheduler.Finish();
        const std::array<unsigned,14> expected=empty ? std::array<unsigned,14>{}
            : source_offset ? std::array<unsigned,14>{20,0,0,0,42,0,0,0,0,0,47,0,0,0}
                            : std::array<unsigned,14>{10,40,0,0,42,42,0,0,0,0,47,47,0,0};
        for(unsigned i=0;i<14;++i) {
            if(values[64+i]!=expected[i]) { std::fprintf(stderr,"bounds result %u: got %u expected %u\n",i,values[64+i],expected[i]); return false; }
        }
        for(unsigned i=14;i<16;++i) {
            const unsigned expected_alias=empty ? 0 : 47;
            if(values[64+i]!=expected_alias) {std::fprintf(stderr,"mixed buffer alias %u: got %u expected %u\n",i,values[64+i],expected_alias);return false;}
        }
        return true;
    };
    assert(run(false));
    assert(values[0]==47 && values[1]==20 && values[2]==30 && values[3]==47);
    for(unsigned i=4;i<64;++i) assert(values[i]==0xdeadbeefu);
    assert(run(false,4));
    assert(values[0]==10 && values[1]==47 && values[2]==30 && values[3]==40);
    assert(run(true));
    vmaDestroyBuffer(instance.GetAllocator(),buffer,allocation);
    VideoCore::TestOrdinaryArena(instance,scheduler);
    std::puts("PASS: MoltenVK buffer bounds: in-range and out-of-range loads, stores, atomics, vector/constant index overflow, nonzero descriptor offsets, scalar constant loads, mixed-width aliases, null u32 buffer");
}
