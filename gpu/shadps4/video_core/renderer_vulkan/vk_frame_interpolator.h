// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/renderer_vulkan/vk_common.h"
#include <memory>

namespace Vulkan {
// Native MetalFX interpolation with explicit, caller-owned temporal inputs.
// The owner drains its scheduler before reconfiguration or destruction.
class FrameInterpolator {
  public:
    struct Inputs {
        VkImageView color{}, previous_color{}, depth{}, motion{}, ui{};
    };
    struct Parameters {
        float delta_seconds{1.f / 60.f};
        float near_plane{}, far_plane{}, vertical_fov_degrees{}, aspect{};
        float motion_scale_x{1}, motion_scale_y{1}; // current -> previous, pixels
        bool reversed_depth{}, reset{}, composited_ui{};
    };
    FrameInterpolator(VkInstance instance, VkDevice device, PFN_vkGetInstanceProcAddr proc);
    ~FrameInterpolator();
    bool Available() const;
    const char* Problem() const;
    bool Configure(unsigned input_width, unsigned input_height, unsigned output_width,
                   unsigned output_height, const Inputs& inputs, VkImage output);
    VkSemaphore ReadySemaphore() const;
    VkSemaphore DoneSemaphore() const;
    unsigned long long NextValue();
    bool Encode(const Inputs& inputs, VkImage output, const Parameters& parameters,
                unsigned long long value);
    bool OutputInterpolated() const;
    void ResetHistory();

  private:
    struct Impl;
    std::shared_ptr<Impl> impl;
};
} // namespace Vulkan
