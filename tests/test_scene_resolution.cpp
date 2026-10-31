// SPDX-License-Identifier: GPL-2.0-or-later
// Exercises the production target/resolve path on Vulkan (Lavapipe works, no game/window).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "common/slot_vector.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_scene_resolution.h"
#include "video_core/texture_cache/blit_helper.h"
#include <vk_mem_alloc.h>

int main() {
    using namespace Vulkan;
    Instance instance(0, false);
    // Calls emitted by this executable use a local dispatcher. libgow3gpu.so initializes
    // its own default dispatcher while constructing Instance.
    static vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic dispatch;
    dispatch.init(
        loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    dispatch.init(instance.GetInstance());
    dispatch.init(instance.GetDevice());
    Scheduler scheduler(instance);
    Runtime runtime(instance, scheduler);
    Common::SlotVector<VideoCore::ImageView> views;
    Common::SlotVector<VideoCore::Image> images;
    VideoCore::ImageInfo ci;
    ci.size = {1920,1080,1};
    ci.resources = {1,1};
    ci.type = AmdGpu::ImageType::Color2D;
    ci.pixel_format = vk::Format::eR8G8B8A8Unorm;
    ci.num_bits = 32;
    const auto color_id = images.insert(instance, runtime, views, ci);
    auto di = ci;
    di.pixel_format = vk::Format::eD32SfloatS8Uint;
    di.props.is_depth = di.props.has_stencil = true;
    const auto depth_id = images.insert(instance, runtime, views, di);
    SceneTargets targets(instance, scheduler, runtime, [&](VideoCore::ImageId id, u64 uid) {
        if (!images.is_allocated(id)) return static_cast<VideoCore::Image*>(nullptr);
        return !uid || images[id].image_uid == uid ? &images[id] : nullptr;
    });
    const bool color_eligible = targets.Eligible(images[color_id]);
    const bool depth_eligible = targets.Eligible(images[depth_id]);
    std::fprintf(stderr, "Scene target eligibility: color=%d depth=%d\n",
                 color_eligible, depth_eligible);
    if (!color_eligible || !depth_eligible) {
        std::puts("Scene targets: unsupported format blit, live scaling skipped");
        return 0;
    }

    VkBuffer readback{};
    VmaAllocation allocation{};
    VmaAllocationInfo ai{};
    const VkBufferCreateInfo bi{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size=16, .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT |
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT, .usage=VMA_MEMORY_USAGE_AUTO,
        .requiredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    assert(vmaCreateBuffer(instance.GetAllocator(), &bi, &ac, &readback, &allocation, &ai) == VK_SUCCESS);
    const auto pixel = [&](vk::Image image, vk::ImageAspectFlagBits aspect,
                           vk::ImageLayout layout, u32 w, u32 h) {
        scheduler.EndRendering();
        const auto cmd = scheduler.CommandBuffer();
        const vk::MemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask=vk::AccessFlagBits2::eTransferRead};
        cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&b}, dispatch);
        const vk::BufferImageCopy region{.imageSubresource={aspect,0,0,1},
            .imageOffset={s32(w-1),s32(h-1),0},.imageExtent={1,1,1}};
        const bool transition = layout != vk::ImageLayout::eGeneral && layout != vk::ImageLayout::eTransferSrcOptimal;
        const vk::ImageMemoryBarrier2 readable{
            .srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask=vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask=vk::AccessFlagBits2::eTransferRead,
            .oldLayout=layout,.newLayout=vk::ImageLayout::eTransferSrcOptimal,
            .image=image,.subresourceRange={aspect,0,1,0,1}};
        if (transition) cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&readable},dispatch);
        cmd.copyImageToBuffer(image, transition ? vk::ImageLayout::eTransferSrcOptimal : layout, readback, region, dispatch);
        if (transition) {
            auto restored=readable;
            restored.oldLayout=readable.newLayout;restored.newLayout=layout;
            restored.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;
            restored.srcAccessMask=vk::AccessFlagBits2::eTransferRead;
            restored.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands;
            restored.dstAccessMask=vk::AccessFlagBits2::eMemoryRead;
            cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&restored},dispatch);
        }
        const vk::MemoryBarrier2 host{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask=vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eHost,
            .dstAccessMask=vk::AccessFlagBits2::eHostRead};
        cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&host}, dispatch);
        scheduler.Finish();
        u32 result;
        std::memcpy(&result, ai.pMappedData, 4);
        return result;
    };
    VideoCore::ImageViewInfo cv;
    cv.format = ci.pixel_format;
    auto dv = cv;
    dv.format = di.pixel_format;
    const auto clear_native = [&](u32 rgba, u32 stencil = 7) {
        vk::ClearValue clear{};
        clear.color.float32 = std::array{float(rgba & 255)/255, float((rgba>>8)&255)/255,
                                        float((rgba>>16)&255)/255, 1.0f};
        runtime.ClearImage(&images[color_id], {}, clear);
        runtime.Transit(&images[depth_id], vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        runtime.FlushBarriers();
        scheduler.CommandBuffer().clearDepthStencilImage(
            images[depth_id].GetImage(), vk::ImageLayout::eTransferDstOptimal,
            vk::ClearDepthStencilValue{0.25f, stencil},
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth |
                vk::ImageAspectFlagBits::eStencil, 0, 1, 0, 1}, dispatch);
    };
    constexpr u32 stencil_values[] = {0, 255, 128, 85, 170, 7, 13, 255, 0, 170};
    u32 stencil_index = 0;
    for (int preset : {3,1,4,2,0,3,0,1,0,3}) {
        const u32 copied_stencil = stencil_values[stencil_index++];
        const u32 resolved_stencil = copied_stencil ^ 255;
        const auto output = stencil_index > 7 ? SceneResolution::Size{3840,2160}
                                             : SceneResolution::Size{};
        const auto size = SceneResolution::ForPreset(preset, output);
        if (targets.SetSize(size)) {
            assert(!images[color_id].scene_proxy && !images[depth_id].scene_proxy);
        }
        assert(SceneResolution::Unpack(SceneResolution::Pack(size)) == size);
        clear_native(0xffff0000, copied_stencil); // blue
        auto c = targets.Read(color_id,cv);
        assert(pixel(c.image,vk::ImageAspectFlagBits::eColor,c.layout,size.width,size.height)==0xffff0000);
        auto d = targets.Read(depth_id,dv);
        const u32 depth_bits = pixel(d.image,vk::ImageAspectFlagBits::eDepth,d.layout,size.width,size.height);
        assert(std::bit_cast<float>(depth_bits)==0.25f);
        assert((pixel(d.image,vk::ImageAspectFlagBits::eStencil,d.layout,size.width,size.height)&255)==copied_stencil);

        c = targets.Attachment(color_id,cv);
        d = targets.Attachment(depth_id,dv);
        auto cmd = scheduler.CommandBuffer();
        const vk::RenderingAttachmentInfo color{.imageView=c.view,.imageLayout=c.layout,
            .loadOp=vk::AttachmentLoadOp::eClear,.storeOp=vk::AttachmentStoreOp::eStore,
            .clearValue=vk::ClearValue{.color={.float32=std::array{1.f,0.f,0.f,1.f}}}};
        const vk::RenderingAttachmentInfo depth{.imageView=d.view,.imageLayout=d.layout,
            .loadOp=vk::AttachmentLoadOp::eClear,.storeOp=vk::AttachmentStoreOp::eStore,
            .clearValue=vk::ClearValue{.depthStencil={0.75f,resolved_stencil}}};
        cmd.beginRendering({.renderArea={{0,0},{size.width,size.height}},.layerCount=1,
            .colorAttachmentCount=1,.pColorAttachments=&color,
            .pDepthAttachment=&depth,.pStencilAttachment=&depth}, dispatch);
        cmd.endRendering(dispatch);
        // Presentation must consume the physical target, including when a live
        // change made it larger than the logical image. Exporting another view
        // and preparing GENERAL for MetalFX must not resolve it back down first.
        const auto sampled = targets.SampleProxy(images[color_id], cv);
        assert(sampled && sampled->image == c.image);
        assert(targets.ProxySize(images[color_id]) == size);
        assert(pixel(sampled->image, vk::ImageAspectFlagBits::eColor,
                     sampled->layout, size.width, size.height) == 0xff0000ff);
        assert(targets.PrepareSample(images[color_id],0,vk::ImageLayout::eGeneral)
               == vk::ImageLayout::eGeneral);
        assert(targets.ProxyCurrent(images[color_id]));
        const auto sampled_depth = targets.SampleProxy(images[depth_id],dv);
        assert(sampled_depth && sampled_depth->image == d.image);
        assert(std::bit_cast<float>(pixel(sampled_depth->image,vk::ImageAspectFlagBits::eDepth,
                                         sampled_depth->layout,size.width,size.height)) == .75f);
        const auto feedback = targets.SnapshotProxy(images[color_id],cv);
        assert(feedback && feedback->image != c.image);
        assert(pixel(feedback->image,vk::ImageAspectFlagBits::eColor,feedback->layout,size.width,size.height)==0xff0000ff);
        assert(targets.ProxyCurrent(images[color_id]));
        runtime.Transit(&images[color_id],vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits2::eTransfer,vk::AccessFlagBits2::eTransferRead);
        runtime.FlushBarriers();
        assert(pixel(images[color_id].GetImage(),vk::ImageAspectFlagBits::eColor,
                     vk::ImageLayout::eTransferSrcOptimal,1920,1080)==0xff0000ff);
        runtime.Transit(&images[depth_id],vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits2::eTransfer,vk::AccessFlagBits2::eTransferRead);
        runtime.FlushBarriers();
        assert(std::bit_cast<float>(pixel(images[depth_id].GetImage(),vk::ImageAspectFlagBits::eDepth,
                     vk::ImageLayout::eTransferSrcOptimal,1920,1080))==0.75f);
        assert((pixel(images[depth_id].GetImage(),vk::ImageAspectFlagBits::eStencil,
                     vk::ImageLayout::eTransferSrcOptimal,1920,1080)&255)==resolved_stencil);
        clear_native(0xff00ff00); // overwrite after reduced render: proxy must be invalidated
        c=targets.Read(color_id,cv);
        assert(pixel(c.image,vk::ImageAspectFlagBits::eColor,c.layout,size.width,size.height)==0xff00ff00);
        std::printf("Scene targets: %ux%u color/depth/stencil roundtrip PASS\n",size.width,size.height);
    }

    // A non-uniform pattern verifies nearest pixel-center mapping and full
    // coverage on both sides of the native/proxy boundary, not just clear values.
    targets.SetSize({640,360});
    const auto split = [&](vk::ImageView view, vk::ImageLayout layout, u32 w, u32 h) {
        auto cmd = scheduler.CommandBuffer();
        const vk::RenderingAttachmentInfo color{.imageView=view,.imageLayout=layout,
            .loadOp=vk::AttachmentLoadOp::eClear,.storeOp=vk::AttachmentStoreOp::eStore,
            .clearValue=vk::ClearValue{.color={.float32=std::array{0.f,0.f,1.f,1.f}}}};
        cmd.beginRendering({.renderArea={{0,0},{w,h}},.layerCount=1,
            .colorAttachmentCount=1,.pColorAttachments=&color},dispatch);
        const vk::ClearAttachment red{.aspectMask=vk::ImageAspectFlagBits::eColor,
            .colorAttachment=0,.clearValue=vk::ClearValue{.color={.float32=std::array{1.f,0.f,0.f,1.f}}}};
        const vk::ClearRect left{.rect={{0,0},{w/2,h}},.baseArrayLayer=0,.layerCount=1};
        cmd.clearAttachments(red,left,dispatch);cmd.endRendering(dispatch);
    };
    runtime.Transit(&images[color_id],vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,vk::AccessFlagBits2::eColorAttachmentWrite);
    runtime.FlushBarriers();
    split(*images[color_id].FindView(cv).image_view,vk::ImageLayout::eColorAttachmentOptimal,1920,1080);
    auto pattern=targets.Read(color_id,cv);
    assert(pixel(pattern.image,vk::ImageAspectFlagBits::eColor,pattern.layout,320,360)==0xff0000ff);
    assert(pixel(pattern.image,vk::ImageAspectFlagBits::eColor,pattern.layout,321,360)==0xffff0000);
    pattern=targets.Attachment(color_id,cv);split(pattern.view,pattern.layout,640,360);
    runtime.Transit(&images[color_id],vk::ImageLayout::eTransferSrcOptimal,
        vk::PipelineStageFlagBits2::eTransfer,vk::AccessFlagBits2::eTransferRead);
    runtime.FlushBarriers();
    assert(pixel(images[color_id].GetImage(),vk::ImageAspectFlagBits::eColor,
                 vk::ImageLayout::eTransferSrcOptimal,960,1080)==0xff0000ff);
    assert(pixel(images[color_id].GetImage(),vk::ImageAspectFlagBits::eColor,
                 vk::ImageLayout::eTransferSrcOptimal,961,1080)==0xffff0000);
    std::puts("Scene targets: nearest split-pattern boundaries PASS");

    // Alias copies crop logical coordinates; they must not scale the whole
    // source into the smaller destination or resolve an entire scene first.
    auto alias_info = ci;
    alias_info.size = {1280,720,1};
    const auto alias_id = images.insert(instance,runtime,views,alias_info);
    auto half_alias_info = ci;
    half_alias_info.size = {960,540,1};
    const auto half_alias_id = images.insert(instance,runtime,views,half_alias_info);
    for (auto physical : {SceneResolution::Size{640,360}, SceneResolution::Size{1130,636}}) {
        targets.SetSize(physical);
        auto input = targets.Attachment(color_id,cv);
        split(input.view,input.layout,physical.width,physical.height);
        assert(!targets.CopyAlias(images[color_id],images[alias_id],{640,360,1}));
        assert(targets.CopyAlias(images[color_id],images[alias_id],{1280,720,1}));
        assert(targets.ProxyCurrent(images[color_id]));
        runtime.Transit(&images[alias_id],vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits2::eTransfer,vk::AccessFlagBits2::eTransferRead);
        runtime.FlushBarriers();
        const auto left = pixel(images[alias_id].GetImage(),vk::ImageAspectFlagBits::eColor,
                                vk::ImageLayout::eTransferSrcOptimal,960,720);
        const auto right = pixel(images[alias_id].GetImage(),vk::ImageAspectFlagBits::eColor,
                                 vk::ImageLayout::eTransferSrcOptimal,961,720);
        assert(left==0xff0000ff && right==0xffff0000);

        // A full overwrite invalidates an old destination proxy without copying
        // its obsolete content back over the new alias data.
        vk::ClearValue zero{};
        runtime.ClearImage(&images[half_alias_id],{},zero);
        auto half = targets.Attachment(half_alias_id,cv);
        split(half.view,half.layout,physical.width/2,physical.height/2);
        assert(targets.ProxyCurrent(images[half_alias_id]));
        assert(targets.CopyAlias(images[color_id],images[half_alias_id],{960,540,1}));
        assert(!targets.ProxyCurrent(images[half_alias_id]));

        // Resolve the reference source, then use the ordinary byte-preserving
        // native alias copy and compare the same boundary pixels.
        runtime.Transit(&images[color_id],vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits2::eTransfer,vk::AccessFlagBits2::eTransferRead);
        runtime.CopyAliasImage(&images[color_id],&images[alias_id],{1280,720,1});
        runtime.Transit(&images[alias_id],vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits2::eTransfer,vk::AccessFlagBits2::eTransferRead);
        runtime.FlushBarriers();
        assert(pixel(images[alias_id].GetImage(),vk::ImageAspectFlagBits::eColor,
                     vk::ImageLayout::eTransferSrcOptimal,960,720)==left);
        assert(pixel(images[alias_id].GetImage(),vk::ImageAspectFlagBits::eColor,
                     vk::ImageLayout::eTransferSrcOptimal,961,720)==right);
    }
    std::puts("Scene targets: clipped alias/native equivalence and destination invalidation PASS");

    // The game's four alias sources use RG16F. Check half-float channel packing
    // as well as rejecting a bit-compatible but differently interpreted format.
    auto rg_info = ci;
    rg_info.pixel_format = vk::Format::eR16G16Sfloat;
    const auto rg_id = images.insert(instance,runtime,views,rg_info);
    rg_info.size = {320,270,1};
    const auto rg_alias_id = images.insert(instance,runtime,views,rg_info);
    targets.SetSize({1130,636});
    auto rg_view = cv;
    rg_view.format = rg_info.pixel_format;
    auto rg = targets.Attachment(rg_id,rg_view);
    const vk::RenderingAttachmentInfo rg_clear{.imageView=rg.view,.imageLayout=rg.layout,
        .loadOp=vk::AttachmentLoadOp::eClear,.storeOp=vk::AttachmentStoreOp::eStore,
        .clearValue=vk::ClearValue{.color={.float32=std::array{0.125f,0.75f,0.f,1.f}}}};
    auto rg_cmd = scheduler.CommandBuffer();
    rg_cmd.beginRendering({.renderArea={{0,0},{1130,636}},.layerCount=1,
        .colorAttachmentCount=1,.pColorAttachments=&rg_clear},dispatch);
    rg_cmd.endRendering(dispatch);
    assert(!targets.CopyAlias(images[rg_id],images[alias_id],{1280,720,1}));
    assert(targets.CopyAlias(images[rg_id],images[rg_alias_id],{320,270,1}));
    runtime.Transit(&images[rg_alias_id],vk::ImageLayout::eTransferSrcOptimal,
        vk::PipelineStageFlagBits2::eTransfer,vk::AccessFlagBits2::eTransferRead);
    runtime.FlushBarriers();
    assert(pixel(images[rg_alias_id].GetImage(),vk::ImageAspectFlagBits::eColor,
                 vk::ImageLayout::eTransferSrcOptimal,320,270)==0x3a003000);
    std::puts("Scene targets: RG16F clipped alias packing and format guard PASS");

    // Half-resolution mip chain (the game's bloom pyramid): one proxy per level, scaled by the
    // scene's factor, resolved into its own native level.
    auto hi = ci;
    hi.size = {960, 540, 1};
    hi.resources = {4, 1};
    const auto half_id = images.insert(instance, runtime, views, hi);
    const auto scene = SceneResolution::ForPreset(4); // 640x360 (x3 of 1080p)
    targets.SetSize(scene);
    auto& half = images[half_id];
    assert(targets.Eligible(half) && !targets.EligibleScene(half));
    assert((targets.ProxySize(half, 0) == SceneResolution::Size{320, 180}));
    assert((targets.ProxySize(half, 2) == SceneResolution::Size{80, 45}));
    auto lv = cv;
    lv.range.base.level = 2;
    const auto proxy = targets.Attachment(half_id, lv);
    {
        auto cmd = scheduler.CommandBuffer();
        const vk::RenderingAttachmentInfo color{.imageView=proxy.view,.imageLayout=proxy.layout,
            .loadOp=vk::AttachmentLoadOp::eClear,.storeOp=vk::AttachmentStoreOp::eStore,
            .clearValue=vk::ClearValue{.color={.float32=std::array{0.f,1.f,0.f,1.f}}}};
        cmd.beginRendering({.renderArea={{0,0},{80,45}},.layerCount=1,
            .colorAttachmentCount=1,.pColorAttachments=&color}, dispatch);
        cmd.endRendering(dispatch);
    }
    assert(targets.ProxyCurrent(half, 2, 1) && !targets.ProxyCurrent(half, 0, 4));
    // A native access resolves the level: its last texel (239, 134) is the proxy's colour.
    runtime.Transit(&half, vk::ImageLayout::eTransferSrcOptimal,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
    runtime.FlushBarriers();
    {
        scheduler.EndRendering();
        const auto cmd = scheduler.CommandBuffer();
        const vk::MemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask=vk::AccessFlagBits2::eTransferRead};
        cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&b}, dispatch);
        const vk::BufferImageCopy region{.imageSubresource={vk::ImageAspectFlagBits::eColor,2,0,1},
            .imageOffset={239,134,0},.imageExtent={1,1,1}};
        cmd.copyImageToBuffer(half.GetImage(), vk::ImageLayout::eTransferSrcOptimal, readback,
                              region, dispatch);
        scheduler.Finish();
        u32 result;
        std::memcpy(&result, ai.pMappedData, 4);
        assert(result == 0xff00ff00);
    }
    std::puts("Scene targets: half-resolution mip level proxy roundtrip PASS");
    scheduler.Finish();
    vmaDestroyBuffer(instance.GetAllocator(),readback,allocation);
}
