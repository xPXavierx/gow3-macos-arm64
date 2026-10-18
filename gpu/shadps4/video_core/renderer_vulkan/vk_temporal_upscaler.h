// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Compatibility boundary for shared renderer call sites. The Apple Silicon
// application uses MetalFX in Presenter and never redirects guest render targets
// through the unported FSR/DLSS/TAA stack. Keep this boundary inert and allocation
// free; source history retains the original implementations and their credits.
#include <array>
#include "video_core/texture_cache/image.h"
namespace VideoCore { class TextureCache; }
namespace Vulkan {
class Instance; class Scheduler; class Runtime; class CameraMotion; class SceneTargets;
class TemporalUpscaler {
public:
    TemporalUpscaler(const Instance&,Scheduler&,VideoCore::TextureCache&,Runtime&,CameraMotion&,SceneTargets&) {}
    bool Enabled() const noexcept { return false; }
    void OnSceneColor(VideoCore::ImageId) {}
    void OnBlendedSceneDraw() {}
    void OnSceneComposite() {}
    void OnDispatch(u64) {}
    bool OnFrameStart() { return false; }
    bool RasterScaling() const { return false; }
    float SceneMipBias() const { return 0; }
    u64 RedirectState() const noexcept { return 0; }
    std::array<float,2> Jitter() const noexcept { return {}; }
    void OnDraw(u64,VideoCore::ImageId,VideoCore::ImageId,bool) {}
    void OnColorTarget(VideoCore::ImageId) {}
    struct Target { vk::ImageView view; vk::ImageLayout layout; u32 width,height; bool native_ui=false; };
    bool RedirectColor(VideoCore::ImageId,const VideoCore::ImageViewInfo&,Target&) { return false; }
    bool RedirectDepth(VideoCore::ImageId,Target&) { return false; }
    bool RedirectSampled(VideoCore::ImageId,const VideoCore::ImageViewInfo&,vk::ImageView&,vk::ImageLayout&) { return false; }
    bool RedirectsSampled(VideoCore::ImageId) const { return false; }
    struct Display { vk::Image image; vk::Format format; u32 width,height; };
    bool DisplayOverride(VAddr,Display&) { return false; }
};
}
