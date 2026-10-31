// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include "video_core/renderer_vulkan/vk_framegen_inputs.h"
#ifdef __APPLE__
extern "C" void Gow3SetMetalHud(void*, bool);
extern "C" unsigned long long Gow3MetalPresentationTimeNs();
#endif

int main() {
    setenv("GOW3_FRAMEGEN", "1", 1);
    setenv("GOW3_FRAMEGEN_MODE", "3", 1);
    setenv("GOW3_RUNTIME_CONTROLS", "1", 1);
    assert(!Vulkan::FrameGen::Requested());
    assert(Gow3Runtime::Initial().interpolation == 0);
    const auto path = std::filesystem::temp_directory_path() /
        ("gow3-shortcut-test-" + std::to_string(getpid()) + ".json");
    setenv("GOW3_RUNTIME_STATE", path.c_str(), 1);
    auto read = [&] {
        std::ifstream file(path);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    assert(!Gow3Runtime::Shortcut(5));
    assert(Gow3Runtime::Shortcut(2)); // MetalFX
    assert(read().find("\"scaling\":1") != std::string::npos);
    assert(Gow3Runtime::Shortcut(3)); // 4K -> 720p
    assert(read().find("\"resolution\":0") != std::string::npos);
    assert(Gow3Runtime::Shortcut(4)); // 60 FPS -> Unlimited
    assert(read().find("\"cap\":0") != std::string::npos);
    assert(read().find("\"interpolation\":0") != std::string::npos);
    assert(Gow3Runtime::Shortcut(4));
    assert(read().find("\"cap\":60") != std::string::npos);
    std::filesystem::remove(path);
    // An old caller cannot instantiate a working interpolator, even directly.
    Vulkan::FrameInterpolator effect(VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr);
    assert(!effect.Available());
    assert(!effect.Configure(1920, 1080, 3840, 2160, {}, VK_NULL_HANDLE));
    assert(!effect.ReadySemaphore() && !effect.DoneSemaphore());
    assert(!effect.Encode({}, VK_NULL_HANDLE, {}, 1));
    assert(!effect.OutputInterpolated());
#ifdef __APPLE__
    // Shared presentation/HUD helpers must survive removal of the interpolator.
    Gow3SetMetalHud(nullptr, false);
    assert(Gow3MetalPresentationTimeNs() > 0);
#endif
    std::puts("PASS: frame generation excluded, old settings/shortcut cannot activate it");
}
