// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_scene_resolution.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/host_shaders/depth_resample_frag.h"
#include "video_core/host_shaders/color_resample_frag.h"
#include "video_core/host_shaders/depth_stencil_resample_frag.h"
#include "video_core/host_shaders/depth_stencil_bits_frag.h"
#include "video_core/host_shaders/fs_tri_vert.h"
#include "gow3_toggles.h"
#include "gow3_runtime_control.h"
#include <cstdio>
#include <cstdlib>
#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#endif

namespace Vulkan {
SceneTargets::SceneTargets(const Instance& i, Scheduler& s, Runtime& r,
                           VideoCore::TextureCache& t)
    : SceneTargets(i, s, r, [&t](VideoCore::ImageId id, u64 uid) {
        return t.TryGetImage(id, uid);
    }) {}
SceneTargets::SceneTargets(const Instance& i, Scheduler& s, Runtime& r, Lookup get)
    : instance{i}, scheduler{s}, runtime{r}, lookup{std::move(get)} {
    runtime.scene_targets = this;
    if (Gow3Runtime::Enabled()) { base={3840,2160}; size=base; }
    if (const char* logical = std::getenv("GOW3_LOGICAL_RENDER_SIZE")) {
        u32 w{}, h{};
        if (std::sscanf(logical, "%ux%u", &w, &h) == 2 && w >= 320 && h >= 180 &&
            w <= 7680 && h <= 4320) {
            base = {w,h}; size = base;
        }
    }
    std::printf("Scene logical base: %ux%u\n", base.width, base.height);
    const char* force = std::getenv("GOW3_SCENE_STENCIL_BITS");
    force_stencil_bits = force && force[0] == '1';
    CreateResampleResources();
}
SceneTargets::~SceneTargets() {
    scheduler.Finish();
    runtime.scene_targets = nullptr;
}
vk::FormatFeatureFlags SceneTargets::Features(vk::Format format) const {
    // Called for every attachment of every draw: the driver query was ~5% of the GPU thread.
    // Core formats index a flat table; extension formats (large enum values) use the map.
    const auto index = u32(format);
    if (index < format_table.size() && format_known[index]) return format_table[index];
    if (index >= format_table.size()) {
        if (const auto it = format_features.find(format); it != format_features.end()) {
            return it->second;
        }
    }
    const auto features =
        instance.GetPhysicalDevice().getFormatProperties(format).optimalTilingFeatures;
    if (index < format_table.size()) {
        format_table[index] = features;
        format_known[index] = true;
    } else {
        format_features.emplace(format, features);
    }
    return features;
}
bool SceneTargets::Blittable(vk::Format format) const {
    const auto features = Features(format);
    const auto required = vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst;
    return (features & required) == required;
}
bool SceneTargets::ShaderResampled(const VideoCore::Image& image) const {
    const auto format = image.backing->image.image_ci.format;
    static const bool discard_resample = [] {
        const char* value = std::getenv("GOW3_SCENE_DISCARD_RESAMPLE");
        return value && value[0] == '1';
    }();
    if (discard_resample && !Gow3Toggle::Disabled(Gow3Toggle::DiscardSceneResample)) {
        const auto features = Features(format);
        if (image.info.props.is_depth) {
            const auto required = vk::FormatFeatureFlagBits::eSampledImage |
                                  vk::FormatFeatureFlagBits::eDepthStencilAttachment;
            if ((features & required) == required &&
                (!(image.aspect_mask & vk::ImageAspectFlagBits::eStencil) ||
                 instance.IsShaderStencilExportSupported())) return true;
        } else {
            // Integer and uncommon formats retain the existing exact blit path.
            const bool supported = format == vk::Format::eR8G8B8A8Unorm ||
                format == vk::Format::eR8G8B8A8Srgb || format == vk::Format::eR16G16Sfloat ||
                format == vk::Format::eR16G16B16A16Sfloat || format == vk::Format::eR32Sfloat;
            const auto required = vk::FormatFeatureFlagBits::eSampledImage |
                                  vk::FormatFeatureFlagBits::eColorAttachment;
            if (supported && (features & required) == required) return true;
        }
    }
    if (!image.info.props.is_depth || (Blittable(format) && !force_stencil_bits)) return false;
    const auto features = Features(format);
    const auto required = vk::FormatFeatureFlagBits::eSampledImage |
                          vk::FormatFeatureFlagBits::eDepthStencilAttachment;
    // Without shader stencil export (e.g. Pascal) depth/stencil stays native: the eight-draw
    // stencil resampler costs too much on such GPUs. GOW3_SCENE_STENCIL_BITS=1 forces it (tests).
    return (features & required) == required &&
           (!(image.aspect_mask & vk::ImageAspectFlagBits::eStencil) ||
            instance.IsShaderStencilExportSupported() || force_stencil_bits);
}
// 1 for the scene size, 2 for half resolution (the game's post-processing chain), else 0.
// Half resolution needs a scene size that halves exactly: the proxy keeps the scene's factor,
// so the shaders' FragCoord conversion (1920 / scene width) stays correct for it.
static u32 SizeDivisor(const VideoCore::ImageInfo& i, SceneResolution::Size scene, SceneResolution::Size base) {
    if (i.size.width == base.width && i.size.height == base.height) return 1;
    if (i.size.width == base.width/2 && i.size.height == base.height/2 && scene.width % 2 == 0 &&
        scene.height % 2 == 0 && !Gow3Toggle::Disabled(Gow3Toggle::SceneHalfRes)) {
        return 2;
    }
    return 0;
}
bool SceneTargets::Eligible(const VideoCore::Image& image) const {
    const auto& i = image.info;
    const u32 div = SizeDivisor(i, size, base);
    if (!div || i.size.depth != 1 || i.resources.layers != 1 || i.num_samples != 1 ||
        i.props.is_block || !image.backing || image.backing->num_samples != 1) {
        return false;
    }
    if (i.resources.levels != 1) {
        // Mip chains (the bloom pyramid at half resolution): one blitted proxy per level.
        return div == 2 && i.resources.levels <= 16 && !i.props.is_depth &&
               Blittable(image.backing->image.image_ci.format);
    }
    return Blittable(image.backing->image.image_ci.format) || ShaderResampled(image);
}
bool SceneTargets::EligibleScene(const VideoCore::Image& image) const {
    return image.info.size.width == base.width && image.info.size.height == base.height && Eligible(image);
}
SceneResolution::Size SceneTargets::ProxySize(const VideoCore::Image& image, u32 level) const {
    const u32 div = std::max(1u, SizeDivisor(image.info, size, base));
    return {std::max(1u, size.width / div >> level), std::max(1u, size.height / div >> level)};
}
bool SceneTargets::SetSize(SceneResolution::Size next) {
    if (next == size) return false;
    ResolveAll();
    scheduler.Finish();
    // Native descriptor memos must become eligible again after the proxies are
    // destroyed. Leaving this flag set made later native draws miss those memos.
    for (const auto& [key, entry] : entries) {
        if (auto* image = lookup(entry->source, entry->uid)) image->scene_proxy = false;
    }
    entries.clear(); // no command buffer can still reference these images/views
    tracked.clear();
    ++generation;
    recent = {};
    size = next;
    std::printf("Scene resolution: raster %ux%u, half-resolution targets %ux%u, UI 1920x1080 "
                "(live)\n", size.width, size.height, size.width / 2, size.height / 2);
    return true;
}
void SceneTargets::ResolveAll() {
    for (auto& [key, entry] : entries) {
        if (entry->state.dirty) {
            if (auto* image = lookup(entry->source, entry->uid)) Copy(*entry, *image, true);
        }
    }
}
bool SceneTargets::CopyAlias(VideoCore::Image& src, VideoCore::Image& dst, vk::Extent3D extent) {
    if (Gow3Toggle::Disabled(Gow3Toggle::SceneAliasProxy) || !ProxyCurrent(src) ||
        src.info.props.is_depth || dst.info.props.is_depth ||
        src.info.resources.levels != 1 || dst.info.resources.levels != 1 ||
        src.info.resources.layers != 1 || dst.info.resources.layers != 1 ||
        src.info.size.depth != 1 || dst.info.size.depth != 1 ||
        src.info.type != AmdGpu::ImageType::Color2D || dst.info.type != AmdGpu::ImageType::Color2D ||
        src.info.num_samples != 1 || dst.info.num_samples != 1 ||
        src.backing->num_samples != 1 || dst.backing->num_samples != 1 ||
        src.GetImage() == dst.GetImage() || extent.depth != 1 ||
        extent.width != dst.info.size.width || extent.height != dst.info.size.height ||
        extent.width > src.info.size.width || extent.height > src.info.size.height ||
        src.info.pixel_format != dst.info.pixel_format ||
        src.backing->image.image_ci.format != src.info.pixel_format ||
        dst.backing->image.image_ci.format != src.info.pixel_format ||
        !(dst.backing->image.image_ci.usage & vk::ImageUsageFlagBits::eColorAttachment) ||
        !ShaderResampled(src)) {
        return false;
    }
    auto& entry = *entries.find(Key(src.image_uid, 0))->second;
    if (!entry.state.dirty) return false; // the ordinary native copy is already exact and cheaper
    copying = true;
    // The entire destination is overwritten. Its old proxy need not be resolved.
    if (auto it = entries.find(Key(dst.image_uid, 0)); it != entries.end())
        it->second->state.NativeWrite();
    runtime.Transit(&dst, vk::ImageLayout::eColorAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentWrite);
    runtime.FlushBarriers();
    Transition(entry, src.aspect_mask, vk::ImageLayout::eShaderReadOnlyOptimal,
               vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
    // A logical-source-sized viewport clips the exact same pixel centers as the
    // original full resolve followed by a top-left copy. The smaller render area
    // writes only the requested destination, including non-integer scale ratios.
    Resample(entry.image, dst.GetImage(), dst, {extent.width, extent.height}, 0, 0,
             {src.info.size.width, src.info.size.height});
    copying = false;
    if (debug) std::printf("Scene alias proxy: %ux%u -> %ux%u (source remains current)\n",
        src.info.size.width, src.info.size.height, extent.width, extent.height);
    return true;
}
void SceneTargets::NativeAccess(VideoCore::Image& image, vk::AccessFlags2 access,
                                const char* caller, u32 line) {
    if (copying || !image.scene_proxy) return; // most images never had a proxy
    constexpr auto writes = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eTransferWrite |
        vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
        vk::AccessFlagBits2::eMemoryWrite;
    const u32 levels = std::min(16u, image.info.resources.levels);
    for (u32 level = 0; level < levels; ++level) {
        const auto it = entries.find(Key(image.image_uid, level));
        if (it == entries.end()) continue;
        auto& entry = *it->second;
        if (debug && (entry.state.dirty || (entry.state.valid && (access & writes)))) {
            std::printf("Scene access: uid %llu address %llx %s %ux%u level %u dirty %d valid %d access %llx at %s:%u\n",
                (unsigned long long)image.image_uid, (unsigned long long)image.info.guest_address,
                vk::to_string(image.info.pixel_format).c_str(), image.info.size.width,
                image.info.size.height, level, int(entry.state.dirty), int(entry.state.valid),
                (unsigned long long)VkAccessFlags2(access), caller, line);
        }
        if (entry.state.dirty) Copy(entry, image, true);
        if (access & writes) entry.state.NativeWrite();
    }
}
void SceneTargets::Transition(Entry& e, vk::ImageAspectFlags aspect, vk::ImageLayout layout,
                               vk::PipelineStageFlags2 stages, vk::AccessFlags2 access) {
    scheduler.EndRendering();
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = stages, .dstAccessMask = access,
        .oldLayout = e.layout, .newLayout = layout,
        .image = e.image, .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    scheduler.Record([barrier](vk::CommandBuffer cmd) {
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    });
    e.layout = layout;
}
void SceneTargets::Copy(Entry& e, VideoCore::Image& original, bool to_native) {
    copying = true;
    scheduler.EndRendering();
    const Runtime::TransferMark mark{runtime, to_native ? "scene proxy resolve" : "scene proxy fill",
                                     original};
    if (debug) {
        std::printf("Scene %s: %s %ux%u level %u\n", to_native ? "resolve" : "fill",
                    vk::to_string(original.info.pixel_format).c_str(), original.info.size.width,
                    original.info.size.height, e.level);
    }
    const vk::Image src = to_native ? vk::Image(e.image) : original.GetImage();
    const vk::Image dst = to_native ? original.GetImage() : vk::Image(e.image);
    if (ShaderResampled(original)) {
        const bool depth = original.info.props.is_depth;
        constexpr auto read_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        const auto write_layout = depth ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                                        : vk::ImageLayout::eColorAttachmentOptimal;
        constexpr auto read_stage = vk::PipelineStageFlagBits2::eFragmentShader;
        const auto write_stage = depth ? vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                         vk::PipelineStageFlagBits2::eLateFragmentTests
                                       : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eColorAttachmentOutput};
        constexpr auto read_access = vk::AccessFlagBits2::eShaderSampledRead;
        const auto write_access = depth ? vk::AccessFlags2{vk::AccessFlagBits2::eDepthStencilAttachmentWrite}
                                        : vk::AccessFlags2{vk::AccessFlagBits2::eColorAttachmentWrite};
        const VideoCore::SubresourceRange range{.base = {e.level, 0}, .extent = {1, 1}};
        runtime.Transit(&original, to_native ? write_layout : read_layout,
                        to_native ? write_stage : read_stage,
                        to_native ? write_access : read_access, range);
        runtime.FlushBarriers();
        Transition(e, original.aspect_mask, to_native ? read_layout : write_layout,
                   to_native ? read_stage : write_stage, to_native ? read_access : write_access);
        const auto& proxy = e.image.image_ci.extent;
        Resample(src, dst, original,
                 to_native ? vk::Extent2D{std::max(1u, original.info.size.width >> e.level),
                                         std::max(1u, original.info.size.height >> e.level)}
                           : vk::Extent2D{proxy.width, proxy.height},
                 to_native ? 0u : e.level, to_native ? e.level : 0u);
        // (Depth proxies are single-level: Eligible.)
        if (to_native) e.state.Resolved(); else e.state.CopiedToProxy();
        copying = false;
        return;
    }
    const VideoCore::SubresourceRange level_range{.base = {e.level, 0}, .extent = {1, 1}};
    runtime.Transit(&original, to_native ? vk::ImageLayout::eTransferDstOptimal
                                       : vk::ImageLayout::eTransferSrcOptimal,
                    vk::PipelineStageFlagBits2::eTransfer,
                    to_native ? vk::AccessFlagBits2::eTransferWrite : vk::AccessFlagBits2::eTransferRead,
                    level_range);
    runtime.FlushBarriers();
    Transition(e, original.aspect_mask,
               to_native ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eTransferDstOptimal,
               vk::PipelineStageFlagBits2::eTransfer,
               to_native ? vk::AccessFlagBits2::eTransferRead : vk::AccessFlagBits2::eTransferWrite);
    const vk::Offset3D native{s32(std::max(1u, original.info.size.width >> e.level)),
                              s32(std::max(1u, original.info.size.height >> e.level)), 1};
    const vk::Offset3D reduced{s32(e.image.image_ci.extent.width),
                               s32(e.image.image_ci.extent.height), 1};
    for (auto aspect : {vk::ImageAspectFlagBits::eColor, vk::ImageAspectFlagBits::eDepth,
                        vk::ImageAspectFlagBits::eStencil}) {
        if (!(original.aspect_mask & aspect)) continue;
        const vk::ImageBlit region{
            .srcSubresource = {aspect, to_native ? 0u : e.level, 0, 1},
            .srcOffsets = std::array{vk::Offset3D{}, to_native ? reduced : native},
            .dstSubresource = {aspect, to_native ? e.level : 0u, 0, 1},
            .dstOffsets = std::array{vk::Offset3D{}, to_native ? native : reduced},
        };
        // Nearest preserves depth/stencil and avoids assuming linear blit support for
        // integer G-buffer formats. FSR reconstructs color at the HDR scene boundary.
        scheduler.Record([src,dst,region](vk::CommandBuffer cmd) {
            cmd.blitImage(src, vk::ImageLayout::eTransferSrcOptimal, dst,
                          vk::ImageLayout::eTransferDstOptimal, region, vk::Filter::eNearest);
        });
    }
    if (to_native) e.state.Resolved(); else e.state.CopiedToProxy();
    copying = false;
}
void SceneTargets::Resample(vk::Image src, vk::Image dst, const VideoCore::Image& original,
                            vk::Extent2D dst_size, u32 src_level, u32 dst_level,
                            vk::Extent2D viewport_size) {
    if (!viewport_size.width) viewport_size = dst_size;
    const auto device = instance.GetDevice();
    const auto format = original.backing->image.image_ci.format;
    const bool color = !original.info.props.is_depth;
    const bool stencil = bool(original.aspect_mask & vk::ImageAspectFlagBits::eStencil);
    const bool stencil_bits = stencil && !depth_stencil_frag;
    const auto make_view = [&](vk::Image image, vk::ImageAspectFlags aspect,
                               vk::ImageUsageFlags usage, u32 level) {
        const vk::ImageViewUsageCreateInfo usage_ci{.usage = usage};
        return Check(device.createImageView({
            .pNext = &usage_ci, .image = image, .viewType = vk::ImageViewType::e2D,
            .format = format, .subresourceRange = {aspect, level, 1, 0, 1},
        }));
    };
    const auto depth_view = make_view(src, color ? vk::ImageAspectFlagBits::eColor : vk::ImageAspectFlagBits::eDepth,
                                      vk::ImageUsageFlagBits::eSampled, src_level);
    const auto stencil_view = stencil ? make_view(src, vk::ImageAspectFlagBits::eStencil,
                                                  vk::ImageUsageFlagBits::eSampled, src_level)
                                      : vk::ImageView{};
    const auto target_view = make_view(dst, original.aspect_mask,
                                       color ? vk::ImageUsageFlagBits::eColorAttachment : vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                       dst_level);
    scheduler.DeferOperation([device, depth_view, stencil_view, target_view] {
        device.destroyImageView(depth_view);
        if (stencil_view) device.destroyImageView(stencil_view);
        device.destroyImageView(target_view);
    });
    const auto pipeline = ResamplePipeline(format, stencil, color);
    const auto layout = *resample_layout;
    scheduler.Record([=](vk::CommandBuffer cmd) {
        const vk::RenderingAttachmentInfo attachment{
            .imageView = target_view,
            .imageLayout = color ? vk::ImageLayout::eColorAttachmentOptimal : vk::ImageLayout::eDepthStencilAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eDontCare,
            .storeOp = vk::AttachmentStoreOp::eStore,
        };
        auto stencil_attachment = attachment;
        if (stencil_bits) {
            stencil_attachment.loadOp = vk::AttachmentLoadOp::eClear;
            stencil_attachment.clearValue.depthStencil = vk::ClearDepthStencilValue{0.f, 0};
        }
        cmd.beginRendering({
            .renderArea = {{0, 0}, dst_size}, .layerCount = 1,
            .colorAttachmentCount = color ? 1u : 0u,
            .pColorAttachments = color ? &attachment : nullptr,
            .pDepthAttachment = color ? nullptr : &attachment,
            .pStencilAttachment = stencil ? &stencil_attachment : nullptr,
        });
        const std::array infos{
            vk::DescriptorImageInfo{{}, depth_view, vk::ImageLayout::eShaderReadOnlyOptimal},
            vk::DescriptorImageInfo{{}, stencil_view, vk::ImageLayout::eShaderReadOnlyOptimal},
        };
        const std::array writes{
            vk::WriteDescriptorSet{.dstBinding = 0, .descriptorCount = 1,
                                   .descriptorType = vk::DescriptorType::eSampledImage,
                                   .pImageInfo = &infos[0]},
            vk::WriteDescriptorSet{.dstBinding = 1, .descriptorCount = 1,
                                   .descriptorType = vk::DescriptorType::eSampledImage,
                                   .pImageInfo = &infos[1]},
        };
        cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, layout, 0,
                                 vk::ArrayProxy<const vk::WriteDescriptorSet>(
                                     stencil ? 2u : 1u, writes.data()));
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cmd.setViewportWithCount(vk::Viewport{0.f, 0.f, float(viewport_size.width),
                                              float(viewport_size.height), 0.f, 1.f});
        cmd.setScissorWithCount(vk::Rect2D{{0, 0}, dst_size});
        if (stencil_bits) {
            // Copy depth first, then reconstruct all eight stencil bits without needing
            // VK_EXT_shader_stencil_export (e.g. Pascal). Discard keeps unset bits clear.
            for (u32 bit : {0u, 1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u}) {
                cmd.pushConstants(layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(bit), &bit);
                cmd.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack, bit);
                cmd.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, bit);
                cmd.draw(3, 1, 0, 0);
            }
        } else {
            cmd.draw(3, 1, 0, 0);
        }
        cmd.endRendering();
    });
    // The guest pipelines' dynamic state must be emitted again after this pipeline.
    scheduler.GetDynamicState().Invalidate();
}
void SceneTargets::CreateResampleResources() {
    const auto device = instance.GetDevice();
    fs_tri_vert = vk::UniqueShaderModule(CompileSPV(FS_TRI_VERT, device), device);
    depth_frag = vk::UniqueShaderModule(CompileSPV(DEPTH_RESAMPLE_FRAG, device), device);
    color_frag = vk::UniqueShaderModule(CompileSPV(COLOR_RESAMPLE_FRAG, device), device);
    if (instance.IsShaderStencilExportSupported() && !force_stencil_bits) {
        depth_stencil_frag =
            vk::UniqueShaderModule(CompileSPV(DEPTH_STENCIL_RESAMPLE_FRAG, device), device);
    }
    if (!depth_stencil_frag) {
        stencil_bits_frag =
            vk::UniqueShaderModule(CompileSPV(DEPTH_STENCIL_BITS_FRAG, device), device);
    }
    const std::array bindings{
        vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eSampledImage, 1,
                                       vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eSampledImage, 1,
                                       vk::ShaderStageFlagBits::eFragment},
    };
    resample_set_layout = Check(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = u32(bindings.size()), .pBindings = bindings.data(),
    }));
    const auto set_layout = *resample_set_layout;
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eFragment, 0, sizeof(u32)};
    resample_layout = Check(device.createPipelineLayoutUnique({
        .setLayoutCount = 1, .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push,
    }));
}
vk::Pipeline SceneTargets::ResamplePipeline(vk::Format format, bool stencil, bool color) {
    const bool stencil_bits = stencil && !depth_stencil_frag;
    const std::pair key{format, stencil};
    for (const auto& [k, pipeline] : resample_pipelines) if (k == key) return *pipeline;
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    const vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    // Reference, compare and write masks are static; the exported value replaces the reference.
    const vk::StencilOpState stencil_op{
        .failOp = vk::StencilOp::eReplace, .passOp = vk::StencilOp::eReplace,
        .depthFailOp = vk::StencilOp::eReplace, .compareOp = vk::CompareOp::eAlways,
        .compareMask = 0xff, .writeMask = 0xff, .reference = 0,
    };
    const vk::PipelineDepthStencilStateCreateInfo depth_state{
        .depthTestEnable = !color, .depthWriteEnable = !color,
        .depthCompareOp = vk::CompareOp::eAlways,
        .stencilTestEnable = stencil, .front = stencil_op, .back = stencil_op,
    };
    const std::array dynamic_states{vk::DynamicState::eViewportWithCount,
                                    vk::DynamicState::eScissorWithCount,
                                    vk::DynamicState::eStencilWriteMask,
                                    vk::DynamicState::eStencilReference};
    const vk::PipelineDynamicStateCreateInfo dynamic_info{
        .dynamicStateCount = stencil_bits ? 4u : 2u, .pDynamicStates = dynamic_states.data(),
    };
    const std::array stages{
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex,
                                          .module = *fs_tri_vert, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment,
                                          .module = color ? *color_frag : stencil ? (stencil_bits ? *stencil_bits_frag :
                                                             *depth_stencil_frag) : *depth_frag,
                                          .pName = "main"},
    };
    const vk::PipelineRenderingCreateInfo rendering{
        .colorAttachmentCount = color ? 1u : 0u,
        .pColorAttachmentFormats = color ? &format : nullptr,
        .depthAttachmentFormat = color ? vk::Format::eUndefined : format,
        .stencilAttachmentFormat = stencil ? format : vk::Format::eUndefined,
    };
    const vk::PipelineColorBlendAttachmentState color_output{
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const vk::PipelineColorBlendStateCreateInfo color_blending{
        .attachmentCount = color ? 1u : 0u,
        .pAttachments = color ? &color_output : nullptr,
    };
    const vk::PipelineViewportStateCreateInfo viewport_info{};
    const vk::PipelineVertexInputStateCreateInfo vertex_input{};
    const vk::PipelineRasterizationStateCreateInfo raster{.lineWidth = 1.f};
    auto pipeline = Check(instance.GetDevice().createGraphicsPipelineUnique(VK_NULL_HANDLE, {
        .pNext = &rendering, .stageCount = u32(stages.size()), .pStages = stages.data(),
        .pVertexInputState = &vertex_input, .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_info, .pRasterizationState = &raster,
        .pMultisampleState = &multisampling, .pDepthStencilState = &depth_state,
        .pColorBlendState = &color_blending, .pDynamicState = &dynamic_info,
        .layout = *resample_layout,
    }));
    const auto result = *pipeline;
    SetObjectName(instance.GetDevice(), result, "Scene resample discard {}", vk::to_string(format));
    resample_pipelines.emplace_back(key, std::move(pipeline));
    return result;
}
SceneTargets::Entry& SceneTargets::Get(VideoCore::ImageId id, u32 level) {
    auto& original = *lookup(id, 0);
    const u64 key = Key(original.image_uid, level);
    // A draw has up to six targets, mostly the same as the previous draw's.
    for (const auto& [recent_key, recent_entry] : recent) {
        if (recent_entry && recent_key == key) {
            if (!recent_entry->state.valid) Copy(*recent_entry, original, false);
            return *recent_entry;
        }
    }
    auto& entry = entries[key];
    original.scene_proxy = true;
    if (!entry) {
        entry = std::make_unique<Entry>();
        entry->source = id;
        entry->uid = original.image_uid;
        entry->level = level;
        entry->image = VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
        auto ci = original.backing->image.image_ci;
        ci.pNext = nullptr;
        const auto proxy = ProxySize(original, level);
        ci.extent = vk::Extent3D{proxy.width, proxy.height, 1};
        ci.mipLevels = 1;
        entry->image.Create(ci);
        tracked.insert(original.image_uid);
    }
    recent[recent_next++ % recent.size()] = {key, entry.get()};
    if (!entry->state.valid) Copy(*entry, original, false);
    return *entry;
}
vk::ImageView SceneTargets::View(Entry& e, const VideoCore::Image& original,
                                 const VideoCore::ImageViewInfo& info) {
    for (const auto& [key,view] : e.views) if (key == info) return *view;
    const auto aspect = original.info.props.is_depth
        ? (info.is_storage ? original.aspect_mask : vk::ImageAspectFlags(vk::ImageAspectFlagBits::eDepth))
        : vk::ImageAspectFlags(vk::ImageAspectFlagBits::eColor);
    const auto format = original.info.props.is_depth ? original.backing->image.image_ci.format : info.format;
    const void* next = nullptr;
#ifdef __APPLE__
    VkExportMetalObjectCreateInfoEXT metal_export{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
    if (std::getenv("GOW3_METALFX_MODE")) {
        metal_export.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;
        next = &metal_export;
    }
#endif
    auto view = Check(instance.GetDevice().createImageViewUnique({
        .pNext = next,
        .image = e.image, .viewType = vk::ImageViewType::e2D, .format = format,
        .components = info.mapping, .subresourceRange = {aspect,0,1,0,1},
    }));
    const auto result = *view;
    e.views.emplace_back(info, std::move(view));
    return result;
}
SceneTargets::Target SceneTargets::Attachment(VideoCore::ImageId id,
                                               const VideoCore::ImageViewInfo& info) {
    auto& original = *lookup(id, 0);
    auto& e = Get(id, info.range.base.level);
    const bool depth = original.info.props.is_depth;
    const auto layout = depth ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                              : vk::ImageLayout::eColorAttachmentOptimal;
    // Consecutive draws can stay in the same render pass.
    if (e.layout != layout) {
        Transition(e, original.aspect_mask, layout,
            depth ? vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests
                  : vk::PipelineStageFlags2(vk::PipelineStageFlagBits2::eColorAttachmentOutput),
            depth ? vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite
                  : vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite);
    }
    e.state.ProxyWrite();
    auto attachment_info = info;
    attachment_info.is_storage = true; // include stencil in attachment views
    return {e.image, View(e, original, attachment_info), e.layout, e.image.image_ci.usage};
}
bool SceneTargets::ProxyCurrent(const VideoCore::Image& image, u32 level, u32 levels) const {
    if (copying || !image.scene_proxy || levels != 1) {
        return false;
    }
    const auto it = entries.find(Key(image.image_uid, level));
    return it != entries.end() && it->second->state.valid;
}
vk::ImageLayout SceneTargets::PrepareSample(const VideoCore::Image& image, u32 level,
                                           vk::ImageLayout layout) {
    auto& e = *entries.find(Key(image.image_uid, level))->second;
    if (e.layout != layout) {
        Transition(e, image.aspect_mask, layout,
                   vk::PipelineStageFlagBits2::eVertexShader |
                       vk::PipelineStageFlagBits2::eFragmentShader |
                       vk::PipelineStageFlagBits2::eComputeShader,
                   vk::AccessFlagBits2::eShaderSampledRead);
    }
    return layout;
}
std::optional<SceneTargets::Target> SceneTargets::SampleProxy(
    const VideoCore::Image& image, const VideoCore::ImageViewInfo& info) {
    const u32 level = info.range.base.level;
    if (!ProxyCurrent(image, level, info.range.extent.levels)) {
        return std::nullopt;
    }
    auto& e = *entries.find(Key(image.image_uid, level))->second;
    const auto layout = PrepareSample(image, level);
    return Target{e.image, View(e, image, info), layout, e.image.image_ci.usage};
}
std::optional<SceneTargets::Target> SceneTargets::SnapshotProxy(
    const VideoCore::Image& image, const VideoCore::ImageViewInfo& info) {
    if (image.info.props.is_depth || !ProxyCurrent(image, info.range.base.level,
                                                  info.range.extent.levels)) return std::nullopt;
    auto& source = *entries.find(Key(image.image_uid, info.range.base.level))->second;
    if (!source.feedback) {
        source.feedback = std::make_unique<Entry>();
        source.feedback->image = VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
        auto ci = source.image.image_ci;
        ci.pNext = nullptr;
        source.feedback->image.Create(ci);
    }
    auto& snapshot = *source.feedback;
    Transition(source, image.aspect_mask, vk::ImageLayout::eTransferSrcOptimal,
               vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
    Transition(snapshot, image.aspect_mask, vk::ImageLayout::eTransferDstOptimal,
               vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
    const vk::ImageCopy region{.srcSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},
        .dstSubresource={vk::ImageAspectFlagBits::eColor,0,0,1}, .extent=source.image.image_ci.extent};
    scheduler.Record([src=vk::Image(source.image), dst=vk::Image(snapshot.image), region](vk::CommandBuffer cmd) {
        cmd.copyImage(src,vk::ImageLayout::eTransferSrcOptimal,dst,vk::ImageLayout::eTransferDstOptimal,region);
    });
    Transition(snapshot, image.aspect_mask, vk::ImageLayout::eShaderReadOnlyOptimal,
               vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
    if (debug) std::printf("Scene feedback snapshot: %ux%u at %ux%u\n",
        image.info.size.width,image.info.size.height,region.extent.width,region.extent.height);
    return Target{snapshot.image,View(snapshot,image,info),snapshot.layout,snapshot.image.image_ci.usage};
}
SceneTargets::Target SceneTargets::Read(VideoCore::ImageId id,
                                       const VideoCore::ImageViewInfo& info,
                                       vk::PipelineStageFlags2 stages, vk::AccessFlags2 access) {
    auto& original = *lookup(id, 0);
    auto& e = Get(id);
    Transition(e, original.aspect_mask, vk::ImageLayout::eGeneral, stages, access);
    return {e.image, View(e, original, info), e.layout, e.image.image_ci.usage};
}
} // namespace Vulkan
