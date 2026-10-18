// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/debug.h"
#include "common/elf_info.h"
#include "common/io_file.h"
#include "common/path_util.h"
#include "common/singleton.h"
#include "core/debug_state.h"
#include "core/emulator_settings.h"
#include "sdl_window.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderdoc.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "gow3_overlay.h"
#include "gow3_settings.h"
#include "gow3_graphics.h"
#include "video_core/renderer_vulkan/vk_temporal_upscaler.h"
#include "video_core/renderer_vulkan/vk_presenter.h"
#include "video_core/renderer_vulkan/vk_warmup_inbox.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/texture_cache/image.h"
#include <thread>
#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#endif

#include <miniz.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csetjmp>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <system_error>
#include <vector>
#include <vk_mem_alloc.h>
#ifdef MemoryBarrier
#undef MemoryBarrier // gow3: winnt.h macro (through fmt), clashes with vk::MemoryBarrier
#endif

namespace Vulkan {

bool CanBlitToSwapchain(const vk::PhysicalDevice physical_device, vk::Format format) {
    const vk::FormatProperties props{physical_device.getFormatProperties(format)};
    return static_cast<bool>(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst);
}

[[nodiscard]] vk::ImageSubresourceLayers MakeImageSubresourceLayers() {
    return vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

[[nodiscard]] vk::ImageBlit MakeImageBlit(s32 frame_width, s32 frame_height, s32 dst_width,
                                          s32 dst_height, s32 offset_x, s32 offset_y) {
    return vk::ImageBlit{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffsets =
            std::array{
                vk::Offset3D{
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                vk::Offset3D{
                    .x = frame_width,
                    .y = frame_height,
                    .z = 1,
                },
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffsets =
            std::array{
                vk::Offset3D{
                    .x = offset_x,
                    .y = offset_y,
                    .z = 0,
                },
                vk::Offset3D{
                    .x = offset_x + dst_width,
                    .y = offset_y + dst_height,
                    .z = 1,
                },
            },
    };
}

[[nodiscard]] vk::ImageBlit MakeImageBlitStretch(s32 frame_width, s32 frame_height,
                                                 s32 swapchain_width, s32 swapchain_height) {
    return MakeImageBlit(frame_width, frame_height, swapchain_width, swapchain_height, 0, 0);
}

static vk::Rect2D FitImage(s32 frame_width, s32 frame_height, s32 swapchain_width,
                           s32 swapchain_height) {
    float frame_aspect = static_cast<float>(frame_width) / frame_height;
    float swapchain_aspect = static_cast<float>(swapchain_width) / swapchain_height;

    u32 dst_width = swapchain_width;
    u32 dst_height = swapchain_height;

    if (frame_aspect > swapchain_aspect) {
        dst_height = static_cast<s32>(swapchain_width / frame_aspect);
    } else {
        dst_width = static_cast<s32>(swapchain_height * frame_aspect);
    }

    const s32 offset_x = (swapchain_width - dst_width) / 2;
    const s32 offset_y = (swapchain_height - dst_height) / 2;

    return vk::Rect2D{{offset_x, offset_y}, {dst_width, dst_height}};
}

[[nodiscard]] vk::ImageBlit MakeImageBlitFit(s32 frame_width, s32 frame_height, s32 swapchain_width,
                                             s32 swapchain_height) {
    const auto& dst_rect = FitImage(frame_width, frame_height, swapchain_width, swapchain_height);

    return MakeImageBlit(frame_width, frame_height, dst_rect.extent.width, dst_rect.extent.height,
                         dst_rect.offset.x, dst_rect.offset.y);
}

// gow3: F12 screenshots of the game's frame (without the overlay), saved as PNG to the user
// folder's screenshots/ once the GPU is done; the conversion follows upstream shadPS4.
namespace {

float PqToNits(float encoded) {
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;
    const float vp = std::pow(std::clamp(encoded, 0.0f, 1.0f), 1.0f / m2);
    return 10000.0f * std::pow(std::max(vp - c1, 0.0f) / std::max(c2 - c3 * vp, 1e-6f), 1.0f / m1);
}

u8 HdrToSrgb8(float nits) {
    const float x = std::max(nits, 0.0f) / 100.0f; // 100-nit SDR white
    const float linear = std::clamp(2.0f * x / (1.0f + x), 0.0f, 1.0f);
    const float srgb = linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return u8(std::clamp(srgb, 0.0f, 1.0f) * 255.0f + 0.5f);
}

bool ScreenshotToRgba8(const u8* src, u32 width, u32 height, vk::Format format, bool hdr,
                       std::vector<u8>& rgba) {
    const size_t pixels = size_t(width) * height;
    rgba.resize(pixels * 4);
    switch (format) {
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
        for (size_t i = 0; i < pixels; ++i) {
            std::memcpy(&rgba[i * 4], &src[i * 4], 3);
            rgba[i * 4 + 3] = 255;
        }
        return true;
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eB8G8R8A8Srgb:
        for (size_t i = 0; i < pixels; ++i) {
            rgba[i * 4 + 0] = src[i * 4 + 2];
            rgba[i * 4 + 1] = src[i * 4 + 1];
            rgba[i * 4 + 2] = src[i * 4 + 0];
            rgba[i * 4 + 3] = 255;
        }
        return true;
    case vk::Format::eA2B10G10R10UnormPack32: {
        static const auto nits = [] {
            std::array<float, 1024> lut{};
            for (size_t i = 0; i < lut.size(); ++i) {
                lut[i] = PqToNits(float(i) / 1023.0f);
            }
            return lut;
        }();
        for (size_t i = 0; i < pixels; ++i) {
            u32 packed;
            std::memcpy(&packed, &src[i * 4], 4);
            const u32 r = packed & 0x3FF, g = (packed >> 10) & 0x3FF, b = (packed >> 20) & 0x3FF;
            if (hdr) {
                // Rec.2020 PQ to SDR Rec.709.
                const float r2020 = nits[r], g2020 = nits[g], b2020 = nits[b];
                rgba[i * 4 + 0] = HdrToSrgb8(1.6605f * r2020 - 0.5876f * g2020 - 0.0728f * b2020);
                rgba[i * 4 + 1] = HdrToSrgb8(-0.1246f * r2020 + 1.1329f * g2020 - 0.0083f * b2020);
                rgba[i * 4 + 2] = HdrToSrgb8(-0.0182f * r2020 - 0.1006f * g2020 + 1.1187f * b2020);
            } else {
                rgba[i * 4 + 0] = u8((r * 255 + 511) / 1023);
                rgba[i * 4 + 1] = u8((g * 255 + 511) / 1023);
                rgba[i * 4 + 2] = u8((b * 255 + 511) / 1023);
            }
            rgba[i * 4 + 3] = 255;
        }
        return true;
    }
    default:
        return false;
    }
}

std::filesystem::path ScreenshotPath() {
    const auto& dir = Common::FS::GetUserPath(Common::FS::PathType::ScreenshotsDir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string serial{Common::ElfInfo::Instance().GameSerial()};
    if (serial.empty()) {
        serial = "gow3";
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now_time);
#else
    localtime_r(&now_time, &local);
#endif
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    char name[96];
    std::snprintf(name, sizeof(name), "%s_%04d%02d%02d_%02d%02d%02d_%03d.png", serial.c_str(),
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
                  local.tm_sec, int(ms));
    return dir / name;
}

/// Records the copy of `image` (layout TransferSrcOptimal) and saves it once the GPU is done.
void CaptureScreenshot(const Instance& instance, Scheduler& scheduler, vk::CommandBuffer cmdbuf,
                       const Frame& frame) {
    const VkBufferCreateInfo buffer_ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                       .size = VkDeviceSize(frame.width) * frame.height * 4,
                                       .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    const VmaAllocationCreateInfo alloc_ci{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
    VkBuffer buffer{};
    VmaAllocation allocation{};
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(instance.GetAllocator(), &buffer_ci, &alloc_ci, &buffer, &allocation, &info) !=
        VK_SUCCESS) {
        LOG_ERROR(Render_Vulkan, "Screenshot: no memory for the readback");
        return;
    }
    cmdbuf.copyImageToBuffer(frame.image, vk::ImageLayout::eTransferSrcOptimal, buffer,
                             vk::BufferImageCopy{.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                                 .imageExtent = {frame.width, frame.height, 1}});
    const vk::MemoryBarrier to_host{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eHostRead,
    };
    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
                           vk::DependencyFlags{}, to_host, {}, {});
    scheduler.DeferPriorityOperation([allocator = instance.GetAllocator(), buffer, allocation, info,
                                      width = frame.width, height = frame.height, format = frame.format,
                                      hdr = frame.is_hdr, path = ScreenshotPath()] {
        vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
        std::vector<u8> rgba;
        const bool converted =
            ScreenshotToRgba8(static_cast<const u8*>(info.pMappedData), width, height, format, hdr, rgba);
        vmaDestroyBuffer(allocator, buffer, allocation);
        if (!converted) {
            LOG_ERROR(Render_Vulkan, "Screenshot: unsupported format {}", vk::to_string(format));
            Gow3Overlay::ShowNotice("Screenshot failed: unsupported image format");
            return;
        }
        size_t png_size = 0;
        void* png = tdefl_write_image_to_png_file_in_memory_ex(rgba.data(), int(width), int(height), 4,
                                                               &png_size, 6, MZ_FALSE);
        bool saved = false;
        if (png) {
#ifdef _WIN32
            FILE* f = _wfopen(path.wstring().c_str(), L"wb"); // the user folder may not be ASCII
#else
            FILE* f = std::fopen(path.c_str(), "wb");
#endif
            if (f) {
                saved = std::fwrite(png, 1, png_size, f) == png_size;
                saved = std::fclose(f) == 0 && saved;
            }
            mz_free(png);
        }
        if (saved) {
            std::printf("Screenshot: %s (%ux%u)\n", Common::FS::PathToUTF8String(path).c_str(), width, height);
            Gow3Overlay::ShowNotice("Screenshot saved: " + Common::FS::PathToUTF8String(path.filename()));
        } else {
            LOG_ERROR(Render_Vulkan, "Screenshot: cannot write {}", Common::FS::PathToUTF8String(path));
            Gow3Overlay::ShowNotice("Screenshot failed: cannot write the file");
        }
    });
}

} // namespace

Presenter::Presenter(Frontend::WindowSDL& window_, AmdGpu::Liverpool* liverpool_)
    : window{window_}, liverpool{liverpool_},
      instance{window, EmulatorSettings.GetGpuId(), EmulatorSettings.IsVkValidationEnabled(),
               EmulatorSettings.IsVkCrashDiagnosticEnabled()},
      draw_scheduler{instance, true}, present_scheduler{instance}, flip_scheduler{instance},
      swapchain{instance, window}, runtime{instance, draw_scheduler},
      rasterizer{std::make_unique<Rasterizer>(instance, draw_scheduler, runtime, liverpool)},
      texture_cache{rasterizer->GetTextureCache()} {
    const u32 num_images = swapchain.GetImageCount();
    const vk::Device device = instance.GetDevice();

    // Create presentation frames.
    present_frames.resize(num_images);
    for (u32 i = 0; i < num_images; i++) {
        Frame& frame = present_frames[i];
        frame.id = i;
        auto fence = Check<"create present done fence">(
            device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));
        frame.present_done = fence;
        free_queue.push(&frame);
    }

#ifdef __APPLE__
    if(FrameGen::Requested()){
        frame_interpolator=std::make_unique<FrameInterpolator>(instance.GetInstance(),device,VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr);
        std::printf("Framegen: requested native MetalFX, available=%d problem=%s\n",frame_interpolator->Available(),frame_interpolator->Problem()?frame_interpolator->Problem():"none");
    }
    if (const char* mode = std::getenv("GOW3_METALFX_MODE")) {
        if (std::strcmp(mode,"spatial") == 0) {
            metalfx = std::make_unique<MetalFx>(instance.GetInstance(),
                device,VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr);
            ASSERT_MSG(Gow3Settings::Get().upscaler == Gow3Settings::UpscalerOff,
                "Set the legacy upscaler to none when using MetalFX spatial");
            metalfx_float_output = EmulatorSettingsImpl::Flag("GOW3_METALFX_FLOAT_OUTPUT", false);
            ASSERT_MSG(metalfx->Available(),"MetalFX native GPU interop is unavailable");
            if (const char* size = std::getenv("GOW3_METALFX_OUTPUT")) {
                ASSERT_MSG(std::sscanf(size,"%ux%u",&metalfx_width,&metalfx_height)==2 &&
                    metalfx_width>=640 && metalfx_height>=360 && metalfx_width<=7680 && metalfx_height<=4320,
                    "Invalid MetalFX output resolution");
            }
        } else {
            ASSERT_MSG(false,"Only MetalFX spatial is integrated; temporal camera data is not calibrated");
        }
    }
#endif
    fsr_pass.Create(device, instance.GetAllocator(), num_images);
    spatial_aa.Create(device,instance.GetAllocator());
    pp_pass.Create(device, swapchain.GetSurfaceFormat().format);
    Gow3Overlay::Init(instance, swapchain.GetSurfaceFormat().format, num_images);

    const bool binary_cache = EmulatorSettingsImpl::Flag("GOW3_PIPELINE_BINARIES", true) &&
                              EmulatorSettings.IsPipelineCacheEnabled();
    const bool capture_only = EmulatorSettingsImpl::Flag("GOW3_PIPELINE_BINARY_ISOLATED", false) &&
                              EmulatorSettingsImpl::Flag("GOW3_PIPELINE_CAPTURE_ONLY", false);
    if (UseOnDemandPipelines(binary_cache, instance.GetPipelineBinaryGlobalKey().keySize != 0,
                            capture_only)) {
        rasterizer->GetPipelineCache().StartOnDemand();
        return;
    }

    // gow3: by default the cached pipelines load while the game runs (a corner note shows the
    // progress). Otherwise rebuilding them takes up to minutes before the first game frame:
    // show what is happening instead of a black window.
    const char* background_warmup = std::getenv("GOW3_BACKGROUND_WARMUP");
    if ((!background_warmup || *background_warmup != '0') &&
        rasterizer->GetPipelineCache().StartBackgroundWarmUp()) {
        return;
    }
    Gow3Overlay::SetLoading(true, 0, 0);
    PresentLoadingFrame();
    auto last = std::chrono::steady_clock::now();
    rasterizer->GetPipelineCache().WarmUp([&](u32 done, u32 total) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::milliseconds(250)) {
            return;
        }
        last = now;
        Gow3Overlay::SetLoading(true, done, total);
        PresentLoadingFrame();
    });
    Gow3Overlay::SetLoading(false, 0, 0);

}

Presenter::~Presenter() {

    draw_scheduler.Finish();
    present_scheduler.Finish();
    flip_scheduler.Finish();
    Check(draw_scheduler.CommandBuffer().reset());
    Check(present_scheduler.CommandBuffer().reset());
    Check(flip_scheduler.CommandBuffer().reset());

#ifdef __APPLE__
    interpolation_linear.reset();interpolation_view.reset();
    interpolation_output=VideoCore::UniqueImage{};frame_interpolator.reset();
    metalfx_view.reset();
    metalfx_output = VideoCore::UniqueImage{};
    metalfx.reset();
#endif
    const vk::Device device = instance.GetDevice();
    for (auto& frame : present_frames) {
        if(frame.generated){auto& g=*frame.generated;vmaDestroyImage(instance.GetAllocator(),g.image,g.allocation);device.destroyImageView(g.image_view);device.destroyFence(g.present_done);}
        vmaDestroyImage(instance.GetAllocator(), frame.image, frame.allocation);
        device.destroyImageView(frame.image_view);
        device.destroyFence(frame.present_done);
    }
}

bool Presenter::IsVideoOutSurface(const AmdGpu::ColorBuffer& color_buffer) const {
    return std::ranges::find(vo_buffers_addr, color_buffer.Address()) != vo_buffers_addr.cend();
}

void Presenter::RecreateFrame(Frame* frame, u32 width, u32 height) {
    const vk::Device device = instance.GetDevice();
    if (frame->image_view) {
        device.destroyImageView(frame->image_view);
    }
    if (frame->image) {
        vmaDestroyImage(instance.GetAllocator(), frame->image, frame->allocation);
    }

    const vk::Format format = swapchain.GetSurfaceFormat().format;
    const vk::ImageCreateInfo image_info = {
        .flags = vk::ImageCreateFlagBits::eMutableFormat,
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst |
                 vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
    };

    const VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    VkImage unsafe_image{};
    VkImageCreateInfo unsafe_image_info = static_cast<VkImageCreateInfo>(image_info);

    VkResult result = vmaCreateImage(instance.GetAllocator(), &unsafe_image_info, &alloc_info,
                                     &unsafe_image, &frame->allocation, nullptr);
    if (result != VK_SUCCESS) [[unlikely]] {
        LOG_CRITICAL(Render_Vulkan, "Failed allocating texture with error {}",
                     vk::to_string(vk::Result{result}));
        UNREACHABLE();
    }
    frame->image = vk::Image{unsafe_image};
    SetObjectName(device, frame->image, "Frame image #{}", frame->id);

    const vk::ImageViewCreateInfo view_info = {
        .image = frame->image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    auto view = Check<"create frame image view">(device.createImageView(view_info));
    frame->image_view = view;
    frame->width = width;
    frame->height = height;

    frame->is_hdr = swapchain.GetHDR();
    frame->format = format;
}

Frame* Presenter::PrepareLastFrame() {
    if (last_submit_frame == nullptr) {
        return nullptr;
    }

    Frame* frame = last_submit_frame;

    while (true) {
        vk::Result result = instance.GetDevice().waitForFences(frame->present_done, false,
                                                               std::numeric_limits<u64>::max());
        if (result == vk::Result::eSuccess) {
            break;
        }
        if (result == vk::Result::eTimeout) {
            continue;
        }
        ASSERT_MSG(result != vk::Result::eErrorDeviceLost,
                   "Device lost during waiting for a frame");
    }

    auto& scheduler = flip_scheduler;
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();

    const auto frame_subresources = vk::ImageSubresourceRange{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };

    const auto pre_barrier =
        vk::ImageMemoryBarrier2{.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentRead,
                                .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                                .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                .newLayout = vk::ImageLayout::eGeneral,
                                .image = frame->image,
                                .subresourceRange{frame_subresources}};

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &pre_barrier,
    });

    // Flush frame creation commands.
    frame->ready_semaphore = scheduler.GetWorkSemaphore()->Handle();
    frame->ready_tick = scheduler.CurrentTick();
    SubmitInfo info{};
    scheduler.Flush(info);
    return frame;
}

static vk::Format GetFrameViewFormat(const Libraries::VideoOut::PixelFormat format) {
    switch (format) {
    case Libraries::VideoOut::PixelFormat::A8B8G8R8Srgb:
        return vk::Format::eR8G8B8A8Srgb;
    case Libraries::VideoOut::PixelFormat::A8R8G8B8Srgb:
        return vk::Format::eB8G8R8A8Srgb;
    case Libraries::VideoOut::PixelFormat::A2R10G10B10:
    case Libraries::VideoOut::PixelFormat::A2R10G10B10Srgb:
    case Libraries::VideoOut::PixelFormat::A2R10G10B10Bt2020Pq:
        return vk::Format::eA2R10G10B10UnormPack32;
    default:
        break;
    }
    UNREACHABLE_MSG("Unknown format={}", static_cast<u32>(format));
    return {};
}

// gow3: raw copy of an image for GOW3_FRAME_DUMP_TRIGGER, written once the GPU is done.
static void DumpRaw(const Instance& instance, Scheduler& scheduler, vk::CommandBuffer cmdbuf,
                    vk::Image image, vk::ImageLayout layout, u32 w, u32 h, const char* name,
                    int index, u32 bytes_per_pixel = 4) {
    const VkBufferCreateInfo buffer_ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                       .size = VkDeviceSize(w) * h * bytes_per_pixel,
                                       .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    const VmaAllocationCreateInfo alloc_ci{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
    VkBuffer buffer{};
    VmaAllocation allocation{};
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(instance.GetAllocator(), &buffer_ci, &alloc_ci, &buffer, &allocation,
                        &info) != VK_SUCCESS) {
        return;
    }
    cmdbuf.copyImageToBuffer(image, layout, buffer,
                             vk::BufferImageCopy{.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                                 .imageExtent = {w, h, 1}});
    const char* dir = std::getenv("GOW3_DUMP_DIR");
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s_%03d_%ux%u.raw", dir && *dir ? dir : "out/dump", name,
                  index, w, h);
    scheduler.DeferPriorityOperation([allocator = instance.GetAllocator(), buffer, allocation, info,
                                      size = buffer_ci.size, file = std::string{path}] {
        vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
        if (FILE* f = std::fopen(file.c_str(), "wb")) {
            std::fwrite(info.pMappedData, 1, size, f);
            std::fclose(f);
        }
        vmaDestroyBuffer(allocator, buffer, allocation);
    });
}

#ifdef __APPLE__
vk::ImageView Presenter::UpscaleMetalFx(vk::ImageView perceptual_view, vk::CommandBuffer& cmd,
        vk::Extent2D& size, bool encoded_srgb, bool dump, int index, bool generated) {
    // Real and reduced generated frames have different input dimensions. Keep
    // their native scaler objects alive instead of rebuilding one twice per flip.
    auto& scaler = generated ? metalfx_generated : metalfx;
    if (!scaler) {
        scaler = std::make_unique<MetalFx>(instance.GetInstance(), instance.GetDevice(),
            VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr);
        ASSERT_MSG(scaler->Available(), "MetalFX generated-frame scaler unavailable");
    }
    if (!metalfx_output) {
        draw_scheduler.Finish();
        cmd = draw_scheduler.CommandBuffer();
        metalfx_output = VideoCore::UniqueImage(instance.GetDevice(),instance.GetAllocator());
        const auto format = metalfx_float_output ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR8G8B8A8Unorm;
        metalfx_output.Create(vk::ImageCreateInfo{
            .flags=vk::ImageCreateFlagBits::eMutableFormat,
            .imageType=vk::ImageType::e2D, .format=format,
            .extent={metalfx_width,metalfx_height,1}, .mipLevels=1, .arrayLayers=1,
            .samples=vk::SampleCountFlagBits::e1, .tiling=vk::ImageTiling::eOptimal,
            .usage=vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout=vk::ImageLayout::eUndefined});
        metalfx_view=Check(instance.GetDevice().createImageViewUnique({
            .image=vk::Image(metalfx_output), .viewType=vk::ImageViewType::e2D,
            .format=metalfx_float_output ? format : encoded_srgb ? vk::Format::eR8G8B8A8Srgb : format,
            .components={vk::ComponentSwizzle::eIdentity,vk::ComponentSwizzle::eIdentity,vk::ComponentSwizzle::eIdentity,vk::ComponentSwizzle::eOne},
            .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}}));
    }
    ASSERT_MSG(scaler->Configure(size.width,size.height,metalfx_width,metalfx_height,
        perceptual_view,vk::Image(metalfx_output)),"MetalFX spatial scaler configuration failed");
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask=vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask=vk::AccessFlagBits2::eMemoryWrite,
        .oldLayout=metalfx_initialized?vk::ImageLayout::eShaderReadOnlyOptimal:vk::ImageLayout::eUndefined,
        .newLayout=vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=vk::Image(metalfx_output),.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
    cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&barrier});
    const u64 value=scaler->NextValue();
    SubmitInfo ready{};ready.AddSignal(scaler->ReadySemaphore(),value);
    draw_scheduler.Flush(ready);
    ASSERT_MSG(scaler->Encode(perceptual_view,vk::Image(metalfx_output),value),"MetalFX spatial GPU encode failed");
    draw_scheduler.WaitExternal(scaler->DoneSemaphore(),value);
    cmd=draw_scheduler.CommandBuffer();
    barrier.oldLayout=vk::ImageLayout::eGeneral;
    barrier.srcAccessMask=vk::AccessFlagBits2::eMemoryWrite;
    barrier.dstAccessMask=vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead;
    barrier.newLayout=dump?vk::ImageLayout::eGeneral:vk::ImageLayout::eShaderReadOnlyOptimal;
    cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&barrier});
    metalfx_initialized=true;
    if(dump) {
        DumpRaw(instance,draw_scheduler,cmd,vk::Image(metalfx_output),vk::ImageLayout::eGeneral,
            metalfx_width,metalfx_height,metalfx_float_output?"metalfx-perceptual-f16":"metalfx-perceptual-rgba8",index,metalfx_float_output?8:4);
        barrier.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite;
        barrier.dstAccessMask=vk::AccessFlagBits2::eShaderRead;
        barrier.newLayout=vk::ImageLayout::eShaderReadOnlyOptimal;
        cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&barrier});
    }
    static bool reported=false;
    if(!reported){
        std::printf("MetalFX: spatial scaler input=%ux%u output=%ux%u perceptual=%d includes_game_HUD=1 (interpolation is a separate stage)\n",
            size.width,size.height,metalfx_width,metalfx_height,encoded_srgb);
        reported=true;
    }
    size={metalfx_width,metalfx_height};
    return *metalfx_view;
}
#endif

#ifdef __APPLE__
void Presenter::GenerateFrame(Frame* frame, u64 address, VideoCore::ImageId ui_id,
                              const VideoCore::ImageViewInfo& ui_view, vk::CommandBuffer& cmd,
                              bool dump, int index) {
    frame->has_generated = false;
    auto* inputs = rasterizer->GetFrameGenInputs();
    if (!inputs)
        return;
    // A menu, movie, missing scene snapshot or unsupported format must show the
    // real frame only. A reset on the next valid frame prevents cross-scene ghosts.
    if (!frame_interpolator || !frame_interpolator->Available() || frame->is_hdr || !ui_id ||
        !inputs->Ready(address) || !Gow3Settings::Get().vsync ||
        std::getenv("GOW3_FRAMEGEN_INPUTS_ONLY")) {
        if (frame_interpolator)
            frame_interpolator->ResetHistory();
        inputs->FinishFrame();
        return;
    }
    const auto size = inputs->Size();
    const auto device = instance.GetDevice();
    constexpr vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    if (interpolation_size != size) {
        draw_scheduler.Finish();
        cmd = draw_scheduler.CommandBuffer();
        interpolation_linear.reset();
        interpolation_view.reset();
        interpolation_output = VideoCore::UniqueImage(device, instance.GetAllocator());
        interpolation_output.Create({.flags = vk::ImageCreateFlagBits::eMutableFormat,
                                     .imageType = vk::ImageType::e2D,
                                     .format = vk::Format::eB8G8R8A8Unorm,
                                     .extent = {size.width, size.height, 1},
                                     .mipLevels = 1,
                                     .arrayLayers = 1,
                                     .samples = vk::SampleCountFlagBits::e1,
                                     .tiling = vk::ImageTiling::eOptimal,
                                     .usage = vk::ImageUsageFlagBits::eColorAttachment |
                                              vk::ImageUsageFlagBits::eSampled |
                                              vk::ImageUsageFlagBits::eTransferSrc});
        VkExportMetalObjectCreateInfoEXT e{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
        e.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;
        interpolation_view =
            Check(device.createImageViewUnique({.pNext = &e,
                                                .image = vk::Image(interpolation_output),
                                                .viewType = vk::ImageViewType::e2D,
                                                .format = vk::Format::eB8G8R8A8Unorm,
                                                .subresourceRange = range}));
        interpolation_linear =
            Check(device.createImageViewUnique({.image = vk::Image(interpolation_output),
                                                .viewType = vk::ImageViewType::e2D,
                                                .format = vk::Format::eB8G8R8A8Srgb,
                                                .subresourceRange = range}));
        interpolation_size = size;
        interpolation_initialized = false;
    }
    if (!frame->generated) {
        frame->generated = std::make_unique<Frame>();
        frame->generated->id = frame->id;
        frame->generated->present_done =
            Check(device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));
    }
    auto* generated = frame->generated.get();
    if (generated->width != frame->width || generated->height != frame->height || !generated->image)
        RecreateFrame(generated, frame->width, frame->height);
    auto& ui = texture_cache.GetImage(ui_id);
    auto view = ui_view;
    if (view.format == vk::Format::eR8G8B8A8Srgb)
        view.format = vk::Format::eR8G8B8A8Unorm;
    if (view.format == vk::Format::eB8G8R8A8Srgb)
        view.format = vk::Format::eB8G8R8A8Unorm;
    const auto ui_handle = *ui.FindView(view).image_view;
    runtime.Transit(&ui, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eShaderRead);
    runtime.FlushBarriers();
    const auto resources = inputs->Inputs(inputs->PrepareUI(ui_handle,cmd));
    auto parameters = inputs->Parameters();
    const auto temporal_size = inputs->TemporalSize();
    const bool configured =
        frame_interpolator->Configure(temporal_size.width, temporal_size.height, size.width,
                                      size.height, resources, vk::Image(interpolation_output));
    if (!configured) {
        frame_interpolator->ResetHistory();
        inputs->FinishFrame();
        return;
    }
    if (dump) {
        const vk::MemoryBarrier2 read{.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                                      .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
                                      .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                      .dstAccessMask = vk::AccessFlagBits2::eTransferRead};
        cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &read});
        DumpRaw(instance, draw_scheduler, cmd, inputs->ColorImage(), vk::ImageLayout::eGeneral,
                size.width, size.height, "framegen-color", index);
        DumpRaw(instance, draw_scheduler, cmd, inputs->PreviousColorImage(),
                vk::ImageLayout::eGeneral, size.width, size.height, "framegen-previous", index);
        DumpRaw(instance, draw_scheduler, cmd, inputs->DepthImage(), vk::ImageLayout::eGeneral,
                temporal_size.width, temporal_size.height, "framegen-depth", index);
        DumpRaw(instance, draw_scheduler, cmd, inputs->MotionImage(), vk::ImageLayout::eGeneral,
                temporal_size.width, temporal_size.height, "framegen-motion", index);
    }
    vk::ImageMemoryBarrier2 b{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryWrite,
        .oldLayout = interpolation_initialized ? vk::ImageLayout::eShaderReadOnlyOptimal
                                               : vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk::Image(interpolation_output),
        .subresourceRange = range};
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
    const auto value = frame_interpolator->NextValue();
    SubmitInfo ready{};
    ready.AddSignal(frame_interpolator->ReadySemaphore(), value);
    draw_scheduler.Flush(ready);
    if (!frame_interpolator->Encode(resources, vk::Image(interpolation_output), parameters,
                                    value)) {
        frame_interpolator->ResetHistory();
        interpolation_initialized = false;
        inputs->FinishFrame();
        return;
    }
    draw_scheduler.WaitExternal(frame_interpolator->DoneSemaphore(), value);
    cmd = draw_scheduler.CommandBuffer();
    if (dump && frame_interpolator->OutputInterpolated()) {
        std::printf("Framegen capture: interpolated=1 delta=%.6f color=%ux%u\n",
                    parameters.delta_seconds, size.width, size.height);
        b.oldLayout = vk::ImageLayout::eGeneral;
        b.newLayout = vk::ImageLayout::eGeneral;
        b.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
        DumpRaw(instance, draw_scheduler, cmd, vk::Image(interpolation_output),
                vk::ImageLayout::eGeneral, size.width, size.height, "framegen-intermediate-bgra",
                index);
        b.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
    }
    b.oldLayout = vk::ImageLayout::eGeneral;
    b.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    b.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
    interpolation_initialized = true;
    if (frame_interpolator->OutputInterpolated()) {
        vk::ImageView output = *interpolation_linear;
        vk::Extent2D output_size = size;
        const int aa = Gow3Settings::Get().spatial_aa;
        if (aa) {
            spatial_aa.Render(cmd, draw_scheduler, *interpolation_view, *interpolation_linear, size,
                              aa);
            output = spatial_aa.LinearView();
        }
        if (metalfx) {
            if (!aa) {
                b.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                b.newLayout = vk::ImageLayout::eGeneral;
                cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
                interpolation_initialized = false;
            }
            output = UpscaleMetalFx(aa ? spatial_aa.PerceptualView() : *interpolation_view, cmd,
                                    output_size, true, false, 0, true);
        } else if (aa)
            spatial_aa.PrepareForVulkan(cmd);
        const vk::ImageMemoryBarrier2 target{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = generated->image,
            .subresourceRange = range};
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &target});
        auto settings = pp_settings;
        settings.srgb_input = metalfx && metalfx_float_output;
        output = fsr_pass.Render(cmd, output, output_size, {generated->width, generated->height},
                                 fsr_settings, false);
        pp_pass.Render(cmd, output, output_size, *generated, settings);
        frame->has_generated = true;
    }
    inputs->FinishFrame();
}
#endif

Frame* Presenter::PrepareFrame(const Libraries::VideoOut::BufferAttributeGroup& attribute,
                               VAddr cpu_address) {
    // gow3: GOW3_FRAME_DUMP_TRIGGER=<file>: when the file exists it is removed and this frame's
    // guest display buffer and presented image are written to GOW3_DUMP_DIR (default out/dump)
    // as raw 32-bit pixels (menus and movies included; the upscaler dumps scene frames only).
    static const char* frame_dump_trigger = std::getenv("GOW3_FRAME_DUMP_TRIGGER");
    static int frame_dump_index = 0;
    const bool frame_dump = frame_dump_trigger && std::remove(frame_dump_trigger) == 0;
    // gow3: scaled upscaler presets: the output-size display buffer drawn by the port.
    TemporalUpscaler::Display display{};
    const bool upscaled = rasterizer->GetUpscaler().DisplayOverride(cpu_address, display);
    VideoCore::ImageId image_id{};
    if (!upscaled) {
        auto desc = VideoCore::TextureCache::ImageDesc{attribute, cpu_address};
        image_id = texture_cache.FindImage(desc);
        texture_cache.UpdateImage(image_id);
    }

    Frame* frame = GetRenderFrame();

    const auto frame_subresources = vk::ImageSubresourceRange{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };

    const auto pre_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .image = frame->image,
        .subresourceRange{frame_subresources},
    };

    draw_scheduler.EndRendering();
    auto cmdbuf = draw_scheduler.CommandBuffer();
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &pre_barrier,
    });

    VideoCore::ImageViewInfo view_info{};
    view_info.format = GetFrameViewFormat(attribute.attrib.pixel_format);
    // Exclude alpha from output frame to avoid blending with UI.
    view_info.mapping.a = vk::ComponentSwizzle::eOne;

    vk::ImageView image_view{};
    vk::Extent2D image_size{};
    bool metalfx_encoded = false;
    if (upscaled) {
        const auto device = instance.GetDevice();
        image_view = Check(device.createImageView({
            .image = display.image,
            .viewType = vk::ImageViewType::e2D,
            .format = view_info.format,
            .components = {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
                           vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eOne},
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        }));
        draw_scheduler.DeferOperation([device, image_view] { device.destroyImageView(image_view); });
        image_size = vk::Extent2D{display.width, display.height};
        const vk::ImageMemoryBarrier2 to_read{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = display.image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_read});
    } else {
        auto& image = texture_cache.GetImage(image_id);
        if (frame_dump) {
            runtime.Transit(&image, vk::ImageLayout::eTransferSrcOptimal,
                            vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
            runtime.FlushBarriers();
            DumpRaw(instance, draw_scheduler, cmdbuf, image.GetImage(),
                    vk::ImageLayout::eTransferSrcOptimal, image.info.size.width,
                    image.info.size.height, "display", frame_dump_index);
        }
        image_view = *image.FindView(view_info).image_view;
        image_size = vk::Extent2D{image.info.size.width, image.info.size.height};
        runtime.Transit(&image, vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::PipelineStageFlagBits2::eFragmentShader|vk::PipelineStageFlagBits2::eComputeShader,
                        vk::AccessFlagBits2::eShaderRead);
        runtime.FlushBarriers();
    }
    const int aa_mode=Gow3Settings::Get().spatial_aa;
    const bool aa_used=aa_mode!=0&&!upscaled&&!frame->is_hdr&&
        (view_info.format==vk::Format::eR8G8B8A8Srgb||view_info.format==vk::Format::eB8G8R8A8Srgb);
    Gow3Settings::Get().spatial_aa_applied=aa_used;
    if(aa_used) {
        auto perceptual_info=view_info;
        perceptual_info.format=view_info.format==vk::Format::eR8G8B8A8Srgb?
            vk::Format::eR8G8B8A8Unorm:vk::Format::eB8G8R8A8Unorm;
        auto perceptual=*texture_cache.GetImage(image_id).FindView(perceptual_info).image_view;
        spatial_aa.Render(cmdbuf,draw_scheduler,perceptual,image_view,image_size,aa_mode);
        if(frame_dump)DumpRaw(instance,draw_scheduler,cmdbuf,spatial_aa.Image(),vk::ImageLayout::eGeneral,
            image_size.width,image_size.height,"spatial-aa",frame_dump_index);
    }
    static int last_aa=-1;
    if(last_aa!=aa_mode) {
        std::printf("Spatial AA: %s applied=%d %ux%u before_upscaling=1 history=0 jitter=0\n",
            Gow3Graphics::SpatialAaNames[std::clamp(aa_mode,0,Gow3Graphics::SpatialAaCount-1)],
            aa_used,image_size.width,image_size.height);
        last_aa=aa_mode;
    }
    const auto hud_input_size = image_size;
    bool hud_metalfx_applied = false;
#ifdef __APPLE__
    if (metalfx && !upscaled) {
        static unsigned color_trace = 0;
        if (color_trace++ < 3) std::printf("MetalFX display: base=%s view=%s presentation=%s pixel_format=%u\n",
            vk::to_string(texture_cache.GetImage(image_id).info.pixel_format).c_str(),
            vk::to_string(view_info.format).c_str(), vk::to_string(frame->format).c_str(),
            static_cast<unsigned>(attribute.attrib.pixel_format));
        ASSERT_MSG(!frame->is_hdr, "MetalFX spatial currently supports SDR output only");
        metalfx_encoded = view_info.format == vk::Format::eR8G8B8A8Srgb ||
            view_info.format == vk::Format::eB8G8R8A8Srgb ||
            attribute.attrib.pixel_format == Libraries::VideoOut::PixelFormat::A2R10G10B10Srgb;
        if (frame_dump) {
            // Same-frame color reference through the original sampled display
            // view. Only the explicit diagnostic trigger adds this extra pass.
            auto reference_settings = pp_settings;
            reference_settings.srgb_input = attribute.attrib.pixel_format ==
                Libraries::VideoOut::PixelFormat::A2R10G10B10Srgb;
            reference_settings.picture = MovieBars::Cutscenes().Bars(
                image_size.width, image_size.height, MovieBars::NowMs());
            pp_pass.Render(cmdbuf,image_view,image_size,*frame,reference_settings);
            const vk::MemoryBarrier2 readable{
                .srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                .srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,
                .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,
                .dstAccessMask=vk::AccessFlagBits2::eTransferRead};
            cmdbuf.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&readable});
            DumpRaw(instance,draw_scheduler,cmdbuf,frame->image,vk::ImageLayout::eGeneral,
                frame->width,frame->height,"reference-presentation",frame_dump_index);
            const vk::ImageMemoryBarrier2 reuse{
                .srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                .srcAccessMask=vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                .dstStageMask=vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .dstAccessMask=vk::AccessFlagBits2::eColorAttachmentWrite,
                .oldLayout=vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eColorAttachmentOptimal,
                .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
                .image=frame->image,.subresourceRange=frame_subresources};
            cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&reuse});
        }
        // Preserve the original display's format and component mapping. MetalFX
        // perceptual mode consumes encoded RGB; only remove the sRGB decode.
        auto perceptual_info = view_info;
        if (perceptual_info.format == vk::Format::eR8G8B8A8Srgb)
            perceptual_info.format = vk::Format::eR8G8B8A8Unorm;
        if (perceptual_info.format == vk::Format::eB8G8R8A8Srgb)
            perceptual_info.format = vk::Format::eB8G8R8A8Unorm;
        const auto perceptual_view = aa_used?spatial_aa.PerceptualView():
            *texture_cache.GetImage(image_id).FindView(perceptual_info).image_view;
        if(!aa_used) {
            runtime.Transit(&texture_cache.GetImage(image_id),vk::ImageLayout::eGeneral,
                vk::PipelineStageFlagBits2::eAllCommands,vk::AccessFlagBits2::eShaderRead);
            runtime.FlushBarriers();
        }
        image_view = UpscaleMetalFx(perceptual_view,cmdbuf,image_size,
                                   metalfx_encoded,frame_dump,frame_dump_index);
        hud_metalfx_applied = true;
        // The SDR output uses an sRGB sampling view, matching the original display.
        if (!metalfx_float_output) metalfx_encoded = false;
    }
    else
#endif
    if(aa_used) {
        spatial_aa.PrepareForVulkan(cmdbuf);
        image_view=spatial_aa.LinearView();
    }
    Gow3Overlay::SetDisplayInfo(hud_input_size.width, hud_input_size.height,
                                image_size.width, image_size.height, hud_metalfx_applied);
    expected_ratio = static_cast<float>(image_size.width) / static_cast<float>(image_size.height);

    // gow3: FSR 1, RCAS and its strength come from the in-game menu, every frame.
    const auto& graphics = Gow3Settings::Get();
    fsr_settings.enable = graphics.fsr1;
    fsr_settings.use_rcas = graphics.rcas;
    fsr_settings.rcas_attenuation = Gow3Graphics::RcasAttenuation(graphics.rcas_strength);
    // Opt-in resolution evidence separates guest rendering from window presentation.
    static const bool trace_resolution = EmulatorSettingsImpl::Flag("GOW3_TRACE_RESOLUTION", false);
    static std::array<int, 6> last_resolution{};
    const std::array<int, 6> resolution{graphics.active_render_width.load(),
        graphics.active_render_height.load(), int(image_size.width), int(image_size.height),
        int(frame->width), int(frame->height)};
    if (trace_resolution && resolution != last_resolution) {
        std::printf("Resolution: proxy_setting=%dx%d display=%dx%d presentation=%dx%d\n",
                    resolution[0], resolution[1], resolution[2], resolution[3],
                    resolution[4], resolution[5]);
        last_resolution = resolution;
    }
    Gow3Settings::Get().image_width = int(image_size.width);
    Gow3Settings::Get().image_height = int(image_size.height);
    Gow3Settings::Get().window_width = int(frame->width);
    Gow3Settings::Get().window_height = int(frame->height);
    Gow3Settings::Get().rcas_applied =
        fsr_settings.use_rcas &&
        Gow3Graphics::FsrPassRuns(fsr_settings.enable, true, int(image_size.width), int(image_size.height),
                                  int(frame->width), int(frame->height));
    image_view = fsr_pass.Render(cmdbuf, image_view, image_size, {frame->width, frame->height},
                                 fsr_settings, frame->is_hdr);

    // Vulkan has no sRGB variant of the 10-bit format, so an A2R10G10B10Srgb buffer reaches
    // the post process pass still sRGB encoded and has to be decoded there instead.
    pp_settings.srgb_input =
        metalfx_encoded || attribute.attrib.pixel_format == Libraries::VideoOut::PixelFormat::A2R10G10B10Srgb;
    // gow3: black bars around a cutscene when the render resolution is not 16:9 (movie_bars.h).
    pp_settings.picture = MovieBars::Cutscenes().Bars(image_size.width, image_size.height,
                                                      MovieBars::NowMs());
    pp_pass.Render(cmdbuf, image_view, image_size, *frame, pp_settings);
    if (frame_dump) {
        // The presented image as the swapchain blit reads it (General after the pass).
        const vk::MemoryBarrier2 done{.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                                      .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
                                      .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                      .dstAccessMask = vk::AccessFlagBits2::eTransferRead};
        cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &done});
        DumpRaw(instance, draw_scheduler, cmdbuf, frame->image, vk::ImageLayout::eGeneral,
                frame->width, frame->height, "output", frame_dump_index);
        ++frame_dump_index;
    }

#ifdef __APPLE__
    GenerateFrame(frame,cpu_address,image_id,view_info,cmdbuf,frame_dump,frame_dump_index-1);
#endif
    // Flush frame creation commands.
    frame->ready_semaphore = draw_scheduler.GetWorkSemaphore()->Handle();
    frame->ready_tick = draw_scheduler.CurrentTick();
    SubmitInfo info{};
    draw_scheduler.Flush(info);
    if(frame->has_generated){frame->generated->ready_semaphore=frame->ready_semaphore;frame->generated->ready_tick=frame->ready_tick;}

    // gow3: the GPU command thread runs at most GOW3_FRAMES_AHEAD (default 1) guest frames
    // ahead of the GPU: it waits here for the frame that many flips back. When the GPU is the
    // bottleneck it finishes frames at an even rate; without this bound the command thread ran
    // ahead and then blocked wherever a resource ran out, so flips (and the guest's frame
    // timing) came in bursts: 12.5/25 ms alternation at 80 FPS. 0 turns it off.
    // The in-game menu changes it live (Gow3Settings::frames_queued; GOW3_FRAMES_AHEAD at start).
    const u32 frames_ahead = u32(std::max(0, Gow3Settings::Get().frames_queued.load()));
    if (!frames_ahead) {
        recent_frame_ticks.clear();
    } else {
        recent_frame_ticks.push_back(frame->ready_tick);
        while (recent_frame_ticks.size() > frames_ahead) {
            const u64 tick = recent_frame_ticks.front();
            recent_frame_ticks.pop_front();
            if (recent_frame_ticks.size() == frames_ahead) {
                draw_scheduler.Wait(tick);
            }
        }
    }
    return frame;
}

Frame* Presenter::PrepareBlankFrame(bool present_thread) {
    // Request a free presentation frame.
    Frame* frame = GetRenderFrame();

    auto& scheduler = present_thread ? present_scheduler : draw_scheduler;
    scheduler.EndRendering();

    const auto cmdbuf = scheduler.CommandBuffer();

    constexpr vk::ImageSubresourceRange simple_subresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };
    const auto pre_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .image = frame->image,
        .subresourceRange = simple_subresource,
    };

    const auto post_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = frame->image,
        .subresourceRange = simple_subresource,
    };

    const vk::RenderingAttachmentInfo attachment = {
        .imageView = frame->image_view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    const vk::RenderingInfo rendering_info = {
        .renderArea =
            {
                .extent = {frame->width, frame->height},
            },
        .layerCount = 1,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &attachment,
    };

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &pre_barrier,
    });

    cmdbuf.beginRendering(rendering_info);
    cmdbuf.endRendering();

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &post_barrier,
    });

    // Flush frame creation commands.
    frame->ready_semaphore = scheduler.GetWorkSemaphore()->Handle();
    frame->ready_tick = scheduler.CurrentTick();
    SubmitInfo info{};
    scheduler.Flush(info);
    return frame;
}

void Presenter::PresentLoadingFrame() {
    if (window.GetWidth() == 0 || window.GetHeight() == 0) {
        return;
    }
    swapchain.UpdateVsync();
    if (window.GetWidth() != swapchain.GetWidth() || window.GetHeight() != swapchain.GetHeight()) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
    }
    if (!swapchain.AcquireNextImage()) {
        return;
    }
    const vk::Image image = swapchain.Image();
    const vk::Extent2D extent = swapchain.GetExtent();
    const auto cmdbuf = present_scheduler.CommandBuffer();
    const vk::ImageSubresourceRange range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };
    const auto barrier = [&](vk::ImageLayout from, vk::ImageLayout to, vk::AccessFlags src,
                             vk::AccessFlags dst) {
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                               vk::PipelineStageFlagBits::eAllCommands, {}, {}, {},
                               vk::ImageMemoryBarrier{
                                   .srcAccessMask = src,
                                   .dstAccessMask = dst,
                                   .oldLayout = from,
                                   .newLayout = to,
                                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .image = image,
                                   .subresourceRange = range,
                               });
    };
    barrier(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, {},
            vk::AccessFlagBits::eTransferWrite);
    cmdbuf.clearColorImage(image, vk::ImageLayout::eTransferDstOptimal,
                           vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}},
                           range);
    barrier(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eColorAttachmentWrite);
    Gow3Overlay::Render(cmdbuf, swapchain.ImageView(), extent);
    barrier(vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits::eColorAttachmentWrite, {});
    SubmitInfo info{};
    info.AddWait(swapchain.GetImageAcquiredSemaphore());
    info.AddSignal(swapchain.GetPresentReadySemaphore());
    present_scheduler.Flush(info);
    {
        std::scoped_lock submit_lock{Scheduler::submit_mutex};
        swapchain.Present();
    }
    // A few frames a second during start-up: waiting keeps semaphores and the swapchain simple.
    present_scheduler.Finish();
}

void Presenter::Present(Frame* frame, bool is_reusing_frame, bool is_game_frame) {
#ifdef __APPLE__
    if (is_game_frame && !is_reusing_frame && frame->has_generated) {
        // VSync/FIFO queues the midpoint before the current frame. Do not add a
        // CPU sleep: drawable acquisition already supplies presentation pacing.
        Present(frame->generated.get(), true, false);
    }
    if (frame_interpolator && is_game_frame && !is_reusing_frame) {
        const auto now = std::chrono::steady_clock::now();
        if (interpolation_stats_time.time_since_epoch().count() == 0)
            interpolation_stats_time = now;
        ++interpolation_real_count;
        if (frame->has_generated)
            ++interpolation_generated_count;
        const double seconds =
            std::chrono::duration<double>(now - interpolation_stats_time).count();
        if (seconds >= 5) {
            Gow3Overlay::SetFrameGenerationRates(float(interpolation_real_count / seconds),
                                                 float(interpolation_generated_count / seconds),
                                                 interpolation_size.width, interpolation_size.height);
            std::printf("Framegen stats: real=%.3f generated=%.3f presents=%.3f per second "
                        "(submission counters; actual presentation is in Display timing)\n",
                        interpolation_real_count / seconds, interpolation_generated_count / seconds,
                        (interpolation_real_count + interpolation_generated_count) / seconds);
            interpolation_real_count = interpolation_generated_count = 0;
            interpolation_stats_time = now;
        }
    }
#endif
    // Free the frame for reuse
    const auto free_frame = [&] {
        if (!is_reusing_frame) {
            last_submit_frame = frame;
            std::scoped_lock fl{free_mutex};
            free_queue.push(frame);
            free_cv.notify_one();
        }
    };

    // gow3: a minimized window has no pixels on Windows (0x0); keep the swapchain and skip
    // presenting until the window is restored.
    if (window.GetWidth() == 0 || window.GetHeight() == 0) {
        free_frame();
        return;
    }

    // Recreate the swapchain if the window was resized.
    swapchain.UpdateVsync();
    if (window.GetWidth() != swapchain.GetWidth() || window.GetHeight() != swapchain.GetHeight()) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
    }

    if (!swapchain.AcquireNextImage()) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
        if (!swapchain.AcquireNextImage()) {
            // User resizes the window too fast and GPU can't keep up. Skip this frame.
            LOG_WARNING(Render_Vulkan, "Skipping frame!");
            free_frame();
            return;
        }
    }

    // Reset fence for queue submission. Do it here instead of GetRenderFrame() because we may
    // skip frame because of slow swapchain recreation. If a frame skip occurs, we skip signal
    // the frame's present fence and future GetRenderFrame() call will hang waiting for this frame.
    const auto reset_result = instance.GetDevice().resetFences(frame->present_done);
    ASSERT_MSG(reset_result == vk::Result::eSuccess,
               "Unexpected error resetting present done fence: {}", vk::to_string(reset_result));

    // gow3: the game frame is blitted (letterboxed) straight into the swapchain image.
    const vk::Image swapchain_image = swapchain.Image();
    auto& scheduler = present_scheduler;
    const auto cmdbuf = scheduler.CommandBuffer();

    if (EmulatorSettings.IsVkHostMarkersEnabled()) {
        cmdbuf.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{
            .pLabelName = "Present",
        });
    }

    {
        const vk::Extent2D extent = swapchain.GetExtent();
        SetExpectedGameSize(s32(extent.width), s32(extent.height));
        const vk::ImageSubresourceRange color_range{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS,
        };
        const std::array pre_barriers{
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eNone,
                .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_image,
                .subresourceRange = color_range,
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = frame->image,
                .subresourceRange = color_range,
            },
        };
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                               vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion,
                               {}, {}, pre_barriers);
        const vk::ClearColorValue black{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
        cmdbuf.clearColorImage(swapchain_image, vk::ImageLayout::eTransferDstOptimal, black, color_range);
        const vk::MemoryBarrier clear_done{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
        };
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, clear_done, {}, {});
        cmdbuf.blitImage(frame->image, vk::ImageLayout::eTransferSrcOptimal, swapchain_image,
                         vk::ImageLayout::eTransferDstOptimal,
                         MakeImageBlitFit(frame->width, frame->height, extent.width, extent.height),
                         vk::Filter::eLinear);
        // Capture a specified presented game frame for unattended port verification.
        // This reads the game's render target, never the desktop or other applications.
        static const u64 capture_frame = [] {
            const char* value = std::getenv("GOW3_CAPTURE_FRAME");
            return value ? std::strtoull(value, nullptr, 10) : 0ull;
        }();
        static const u64 capture_every = [] {
            const char* value = std::getenv("GOW3_CAPTURE_EVERY");
            return value ? std::strtoull(value, nullptr, 10) : 0ull;
        }();
        static u64 presented_frames = 0;
        const bool new_game_frame = is_game_frame && !is_reusing_frame;
        if (new_game_frame) ++presented_frames;
        if (is_game_frame && (VideoCore::ConsumeGameOnlyScreenshotRequests() != 0 ||
            (new_game_frame && ((capture_frame && presented_frames == capture_frame) ||
                                (capture_every && presented_frames % capture_every == 0))))) {
            CaptureScreenshot(instance, scheduler, cmdbuf, *frame);
        }
        // gow3: the settings menu / FPS counter over the frame, at display resolution.
        const bool overlay = Gow3Overlay::Visible();
        const std::array post_barriers{
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = overlay ? vk::AccessFlagBits::eColorAttachmentRead |
                                               vk::AccessFlagBits::eColorAttachmentWrite
                                         : vk::AccessFlagBits::eNone,
                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = overlay ? vk::ImageLayout::eColorAttachmentOptimal
                                     : vk::ImageLayout::ePresentSrcKHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_image,
                .subresourceRange = color_range,
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eTransferRead,
                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = frame->image,
                .subresourceRange = color_range,
            },
        };
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                               vk::PipelineStageFlagBits::eAllCommands,
                               vk::DependencyFlagBits::eByRegion, {}, {}, post_barriers);
        if (overlay) {
            Gow3Overlay::Render(cmdbuf, swapchain.ImageView(), extent);
            const vk::ImageMemoryBarrier to_present{
                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                .dstAccessMask = vk::AccessFlagBits::eNone,
                .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .newLayout = vk::ImageLayout::ePresentSrcKHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_image,
                .subresourceRange = color_range,
            };
            cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                   vk::PipelineStageFlagBits::eBottomOfPipe,
                                   vk::DependencyFlagBits::eByRegion, {}, {}, to_present);
        }
        // Explicit diagnostics can include our in-game HUD. Read only this
        // swapchain image; the desktop and Apple's system overlay are excluded.
        if (is_game_frame && VideoCore::ConsumeWithOverlaysScreenshotRequests() != 0) {
            vk::ImageMemoryBarrier capture_barrier{
                .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::ePresentSrcKHR,
                .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_image, .subresourceRange = color_range};
            cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, capture_barrier);
            const Frame snapshot{.width = extent.width, .height = extent.height,
                .image = swapchain_image, .format = swapchain.GetSurfaceFormat().format};
            CaptureScreenshot(instance, scheduler, cmdbuf, snapshot);
            capture_barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
            capture_barrier.dstAccessMask = {};
            capture_barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
            capture_barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
            cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {}, capture_barrier);
        }
    }
    if (EmulatorSettings.IsVkHostMarkersEnabled()) {
        cmdbuf.endDebugUtilsLabelEXT();
    }

    // Flush vulkan commands.

    SubmitInfo info{};
    info.AddWait(swapchain.GetImageAcquiredSemaphore());
    info.AddWait(frame->ready_semaphore, frame->ready_tick);
    info.AddSignal(swapchain.GetPresentReadySemaphore());
    info.AddSignal(frame->present_done);
    scheduler.Flush(info);

    // Present to swapchain.
    {
        std::scoped_lock submit_lock{Scheduler::submit_mutex};
        if (!swapchain.Present(!is_game_frame && is_reusing_frame) && window.GetWidth() != 0 && window.GetHeight() != 0) {
            swapchain.Recreate(window.GetWidth(), window.GetHeight());
        }
    }

    free_frame();
    if (!is_reusing_frame && is_game_frame) {
        DebugState.IncFlipFrameNum();
    }
}

Frame* Presenter::GetRenderFrame() {
    // Wait for free presentation frames
    Frame* frame;
    {
        std::unique_lock lock{free_mutex};
        free_cv.wait(lock, [this] { return !free_queue.empty(); });
        LOG_DEBUG(Render_Vulkan, "Got render frame, remaining {}", free_queue.size() - 1);

        // Take the frame from the queue
        frame = free_queue.front();
        free_queue.pop();
    }

    const vk::Device device = instance.GetDevice();
    vk::Result result{};

    const auto wait = [&]() {
        result = device.waitForFences(frame->present_done, false, std::numeric_limits<u64>::max());
        return result;
    };

    // Wait for the presentation to be finished so all frame resources are free
    while (wait() != vk::Result::eSuccess) {
        ASSERT_MSG(result != vk::Result::eErrorDeviceLost,
                   "Device lost during waiting for a frame");
        // Retry if the waiting times out
        if (result == vk::Result::eTimeout) {
            continue;
        }
    }

    if (frame->width != expected_frame_width || frame->height != expected_frame_height ||
        frame->is_hdr != swapchain.GetHDR()) {
        RecreateFrame(frame, expected_frame_width, expected_frame_height);
    }

    frame->has_generated=false;
    return frame;
}

void Presenter::SetExpectedGameSize(s32 width, s32 height) {
    if (width <= 0 || height <= 0) {
        return; // no surface (minimized window): keep the last frame size
    }
    const float ratio = (float)width / (float)height;

    expected_frame_height = height;
    expected_frame_width = width;
    if (ratio > expected_ratio) {
        expected_frame_width = static_cast<s32>(height * expected_ratio);
    } else {
        expected_frame_height = static_cast<s32>(width / expected_ratio);
    }
}

} // namespace Vulkan
