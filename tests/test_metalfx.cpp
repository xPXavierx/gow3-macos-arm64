// SPDX-License-Identifier: GPL-2.0-or-later
// Production Vulkan -> MetalFX -> Vulkan path, including sRGB aliases and
// deliberately zero input alpha. Synthetic pixels only; no game data.
#include <cassert>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_metalfx.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_metal.h>

int main() {
    setenv("GOW3_METALFX_MODE", "spatial", 1);
    Vulkan::Instance instance(0, std::getenv("GOW3_VK_VALIDATION") != nullptr);
    vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance()); d.init(instance.GetDevice());
    Vulkan::Scheduler scheduler(instance);
    Vulkan::MetalFx fx(instance.GetInstance(), instance.GetDevice(),
                       loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    assert(fx.Available());
    unsigned checked = 0, max_quantization_difference = 0;
    std::vector<float> reference;
    for (const auto format : {vk::Format::eR8G8B8A8Unorm, vk::Format::eR8G8B8A8Srgb,
                              vk::Format::eB8G8R8A8Unorm, vk::Format::eB8G8R8A8Srgb}) {
        for (const auto view_format : {vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8A8Unorm})
        for (bool swapped : {false, true})
        for (const auto width : {128u, 192u}) for (bool floating : {true,false}) {
            const unsigned height = 96, ow = width * 2, oh = height * 2;
            // Store bytes for the *view's* interpretation, not the base format.
            // The game uses an RGBA backing image with a BGRA presentation view.
            const bool bgra = (view_format == vk::Format::eB8G8R8A8Unorm) != swapped;
            const size_t input_size = width * height * 4;
            const size_t output_bpp = floating ? 8 : 4;
            const VkBufferCreateInfo bc{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size=input_size+ow*oh*output_bpp, .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
            const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                .usage=VMA_MEMORY_USAGE_AUTO, .requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
            VkBuffer raw{}; VmaAllocation allocation{}; VmaAllocationInfo mapped{};
            assert(vmaCreateBuffer(instance.GetAllocator(),&bc,&ac,&raw,&allocation,&mapped)==VK_SUCCESS);
            auto* pixels=static_cast<unsigned char*>(mapped.pMappedData);
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                auto* p=pixels+(y*width+x)*4;
                p[bgra?2:0]=32+(x/32)*32; p[1]=24+(y/32)*80; p[bgra?0:2]=128; p[3]=0;
            }
            VideoCore::UniqueImage input(instance.GetDevice(),instance.GetAllocator());
            VideoCore::UniqueImage output(instance.GetDevice(),instance.GetAllocator());
            input.Create({.flags=vk::ImageCreateFlagBits::eMutableFormat,
                .imageType=vk::ImageType::e2D,.format=format,.extent={width,height,1},
                .mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,
                .tiling=vk::ImageTiling::eOptimal,
                .usage=vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled});
            VkExportMetalObjectCreateInfoEXT metal_export{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
            metal_export.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;
            auto [view_result, input_view] = instance.GetDevice().createImageViewUnique({
                .pNext=&metal_export,.image=vk::Image(input),.viewType=vk::ImageViewType::e2D,
                .format=view_format,
                .components={swapped?vk::ComponentSwizzle::eB:vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    swapped?vk::ComponentSwizzle::eR:vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eOne},
                .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}},nullptr,d);
            assert(view_result == vk::Result::eSuccess);
            output.Create({.imageType=vk::ImageType::e2D,.format=floating?vk::Format::eR16G16B16A16Sfloat:vk::Format::eR8G8B8A8Unorm,
                .extent={ow,oh,1},.mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,
                .tiling=vk::ImageTiling::eOptimal,
                .usage=vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|
                       vk::ImageUsageFlagBits::eColorAttachment|vk::ImageUsageFlagBits::eTransferSrc});
            auto cmd=scheduler.CommandBuffer();
            std::array<vk::ImageMemoryBarrier2,2> barriers{};
            for(unsigned i=0;i<2;++i)barriers[i]={
                .srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                .dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask=vk::AccessFlagBits2::eMemoryWrite,
                .oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
                .image=i?vk::Image(output):vk::Image(input),
                .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
            cmd.pipelineBarrier2({.imageMemoryBarrierCount=2,.pImageMemoryBarriers=barriers.data()},d);
            const vk::BufferImageCopy copy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={width,height,1}};
            cmd.copyBufferToImage(raw,vk::Image(input),vk::ImageLayout::eGeneral,copy,d);
            barriers[0].oldLayout=vk::ImageLayout::eGeneral;
            barriers[0].srcAccessMask=vk::AccessFlagBits2::eTransferWrite;
            barriers[0].dstAccessMask=vk::AccessFlagBits2::eShaderRead;
            cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=barriers.data()},d);
            assert(fx.Configure(width,height,ow,oh,*input_view,vk::Image(output)));
            const auto value=fx.NextValue();
            Vulkan::SubmitInfo ready{};ready.AddSignal(fx.ReadySemaphore(),value);scheduler.Flush(ready);
            assert(fx.Encode(*input_view,vk::Image(output),value));
            scheduler.WaitExternal(fx.DoneSemaphore(),value);cmd=scheduler.CommandBuffer();
            barriers[1].oldLayout=vk::ImageLayout::eGeneral;
            barriers[1].srcAccessMask=vk::AccessFlagBits2::eMemoryWrite;
            barriers[1].dstAccessMask=vk::AccessFlagBits2::eTransferRead;
            cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&barriers[1]},d);
            cmd.copyImageToBuffer(vk::Image(output),vk::ImageLayout::eGeneral,raw,
                vk::BufferImageCopy{.bufferOffset=input_size,.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={ow,oh,1}},d);
            scheduler.Finish();
            const auto* result=reinterpret_cast<const _Float16*>(pixels+input_size);
            const auto* result8=pixels+input_size;
            for(unsigned y=16;y<height;y+=32)for(unsigned x=16;x<width;x+=32) {
                const auto* p=result+((y*2)*ow+x*2)*4;
                const float expected[3]={float(32+(x/32)*32)/255,float(24+(y/32)*80)/255,128.f/255};
                for(unsigned c=0;c<3;++c){const float value=floating?float(p[c]):float(result8[((y*2)*ow+x*2)*4+c])/255;assert(std::isfinite(value));assert(std::abs(value-expected[c])<0.006f);++checked;}
            }
            if(floating) {
                reference.assign(result,result+ow*oh*4);
            } else {
                for(size_t i=0;i<ow*oh*4;++i) {
                    const int expected=int(std::lround(std::clamp(reference[i],0.f,1.f)*255.f));
                    const unsigned difference=std::abs(expected-int(result8[i]));
                    max_quantization_difference=std::max(max_quantization_difference,difference);
                    assert(difference<=2);
                }
            }
            vmaDestroyBuffer(instance.GetAllocator(),raw,allocation);
        }
    }
    std::printf("SDR versus quantized float output: maximum %u/255 (all pixels)\n",max_quantization_difference);
    std::printf("PASS: MetalFX interop, four backing formats, RGBA/BGRA presentation aliases, component swizzles, float/SDR output, resize, zero-alpha input; %u color samples\n",checked);
}
