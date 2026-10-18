// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from bloodborne-runtime 9f809002d247e663c92e46cc96b17b1056a38415.
// bbport: stand-ins for null descriptors on drivers without VK_EXT_robustness2's nullDescriptor
// (MoltenVK). A binding the game leaves empty gets a zeroed buffer, or a zeroed 1x1 image whose
// view matches what the shader recompiler declares for it (dimension, float or integer), kept in
// the general layout. Typed reads see zero; image-size queries still see the stand-in's
// dimensions. Writable/depth null images need additional handling and are rejected.

#pragma once

#include <array>
#include "common/types.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/renderer_vulkan/vk_common.h"

struct VmaAllocation_T;

namespace Vulkan {

class Instance;
class Scheduler;

class NullResources {
public:
    static constexpr u64 BufferSize = 16384;

    NullResources(const Instance& instance, Scheduler& scheduler);
    ~NullResources();

    NullResources(const NullResources&) = delete;
    NullResources& operator=(const NullResources&) = delete;

    vk::Buffer Buffer() const {
        return buffer;
    }
    /// A view of the given recompiler view type (AmdGpu::Image::GetViewType).
    vk::ImageView ImageView(AmdGpu::ImageType type, AmdGpu::NumberFormat format) const;

private:
    enum Shape : u32 { Shape1D, Shape2D, Shape2DMsaa, Shape3D, NumShapes };
    enum ViewKind : u32 { View1D, View1DArray, View2D, View2DArray, View2DMsaa, View2DMsaaArray, View3D, NumViews };

    const Instance& instance;
    vk::Buffer buffer{};
    VmaAllocation_T* buffer_allocation{};
    std::array<std::array<vk::Image, NumShapes>, 3> images{}; ///< [integer][shape]
    std::array<std::array<VmaAllocation_T*, NumShapes>, 3> image_allocations{};
    std::array<std::array<vk::ImageView, NumViews>, 3> views{}; ///< [integer][view kind]
};

} // namespace Vulkan
