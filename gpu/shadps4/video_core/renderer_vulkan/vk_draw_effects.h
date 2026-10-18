// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <span>
#include "shader_recompiler/info.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {
// With no fragments, attachments, memory writes or active queries, transformed
// vertices have no observable consumer. Keep every stage that can publish data.
inline bool CanElideVertexOnlyDraw(const RenderState& state,
                                  std::span<const Shader::Info* const> stages,
                                  bool active_query) {
    if (stages.size() != Shader::MaxStageTypes - 1 ||
        active_query || HasRenderAttachments(state) ||
        stages[u32(Shader::SwStage::Fragment)]) return false;
    for (const auto* info : stages) {
        if (!info) continue;
        if (info->translation_failed || info->uses_dma || info->has_storage_images ||
            info->uses_buffer_int64_atomics || info->uses_shared_int64_atomics ||
            info->uses_buffer_atomic_float_min_max || info->uses_image_atomic_float_min_max ||
            std::ranges::any_of(info->images, [](const auto& image) {
                // Resource descriptors persist in the shader cache; the summary
                // flags above may be absent after loading cached metadata.
                return image.is_written || image.is_atomic;
            }) ||
            std::ranges::any_of(info->buffers, [](const auto& b) {
                return b.is_written || b.buffer_type == Shader::BufferType::GdsBuffer ||
                       b.buffer_type == Shader::BufferType::SharedMemory ||
                       b.buffer_type == Shader::BufferType::FaultBuffer;
            }))
            return false;
    }
    return true;
}
} // namespace Vulkan
