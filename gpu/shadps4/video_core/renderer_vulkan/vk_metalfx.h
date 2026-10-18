// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
// Shared images and timeline events order Vulkan -> native MetalFX -> Vulkan.
// No CPU readback or per-frame host wait is needed in the production path.
class MetalFx {
public:
    MetalFx(VkInstance instance, VkDevice device, PFN_vkGetInstanceProcAddr get_proc);
    ~MetalFx();
    bool Available() const;
    bool Configure(unsigned width, unsigned height, unsigned output_width,
                   unsigned output_height, VkImageView perceptual_color, VkImage output);
    VkSemaphore ReadySemaphore() const;
    VkSemaphore DoneSemaphore() const;
    unsigned long long NextValue();
    // Export the presentation view, not its backing image: RGBA/BGRA aliases
    // and component mappings are part of the displayed color interpretation.
    bool Encode(VkImageView perceptual_color, VkImage output, unsigned long long value);
private:
    struct Impl;
    std::shared_ptr<Impl> impl;
};
}
