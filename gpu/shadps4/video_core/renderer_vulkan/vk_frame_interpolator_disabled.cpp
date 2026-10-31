// SPDX-License-Identifier: GPL-2.0-or-later
// Compatibility boundary for the withdrawn experiment. No MetalFX interpolator
// or temporal GPU resources are created in this build.
#include "video_core/renderer_vulkan/vk_frame_interpolator.h"

namespace Vulkan {
FrameInterpolator::FrameInterpolator(VkInstance, VkDevice, PFN_vkGetInstanceProcAddr) {}
FrameInterpolator::~FrameInterpolator() = default;
bool FrameInterpolator::Available() const { return false; }
const char* FrameInterpolator::Problem() const { return "Frame generation is disabled in this build"; }
bool FrameInterpolator::Configure(unsigned, unsigned, unsigned, unsigned, const Inputs&, VkImage) { return false; }
VkSemaphore FrameInterpolator::ReadySemaphore() const { return VK_NULL_HANDLE; }
VkSemaphore FrameInterpolator::DoneSemaphore() const { return VK_NULL_HANDLE; }
unsigned long long FrameInterpolator::NextValue() { return 0; }
bool FrameInterpolator::Encode(const Inputs&, VkImage, const Parameters&, unsigned long long) { return false; }
bool FrameInterpolator::OutputInterpolated() const { return false; }
void FrameInterpolator::ResetHistory() {}
} // namespace Vulkan
