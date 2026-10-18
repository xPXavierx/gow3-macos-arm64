// SPDX-License-Identifier: GPL-2.0-or-later
// The frame analyzer's buffer dump (vk_frame_capture.cpp). A binding whose bytes the host cannot
// read (data null: a constant-ring copy of the flattened user data carries guest address 0,
// issue #174) is skipped instead of read through the null pointer. No game or Vulkan device.
#include <array>
#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include "video_core/texture_cache/image_info.h"

#include "video_core/renderer_vulkan/vk_frame_capture.h"

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("gow3-frame-capture-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(directory));
#ifdef _WIN32
    assert(_putenv_s("GOW3_CAPTURE_DIR", directory.string().c_str()) == 0);
    assert(_putenv_s("GOW3_CAPTURE_DISPLAY_PASSES", "4") == 0);
#else
    assert(setenv("GOW3_CAPTURE_DIR", directory.string().c_str(), 1) == 0);
    assert(setenv("GOW3_CAPTURE_DISPLAY_PASSES", "4", 1) == 0);
#endif
    VideoCore::ImageInfo display{}, scene{};
    display.guest_address = 0x100000;
    scene.guest_address = 0x200000;
    display.pixel_format = scene.pixel_format = vk::Format::eR8G8B8A8Unorm;
    const VideoCore::ImageInfo* display_target = &display;
    const VideoCore::ImageInfo* scene_target = &scene;
    Vulkan::FrameCapture::AddDisplayBuffer(display.guest_address);
    Vulkan::FrameCapture::Request();
    Vulkan::FrameCapture::OnFlip(display.guest_address);
    Vulkan::FrameCapture::BeginPass(&display_target, 1, nullptr, "synthetic start");
    for (unsigned i = 1; i <= 4; ++i) {
        Vulkan::FrameCapture::BeginPass(&scene_target, 1, nullptr, "synthetic scene");
        Vulkan::FrameCapture::Draw(1, 2, 3, 1);
        Vulkan::FrameCapture::BeginPass(&display_target, 1, nullptr, "synthetic display");
        assert(Vulkan::FrameCapture::IsRecording() == (i < 4));
    }
    assert(!std::filesystem::is_empty(directory));
    std::filesystem::remove_all(directory);
    // A buffer with host bytes is dumped as before.
    std::array<float, 16> words{};
    Vulkan::FrameCapture::Buffer(0x1234, 0, 0x1000, words.data(), sizeof(words));
    // Flattened user data in the constant ring: host copy only, guest address 0, and here
    // nothing readable at all. Must not dereference `data`.
    Vulkan::FrameCapture::Buffer(0x1234, 1, 0, nullptr, 64);
    std::puts("frame-capture-test ok");
    return 0;
}
