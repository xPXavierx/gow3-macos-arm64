// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <sirit/sirit.h>

namespace Shader::Backend::SPIRV {
// Preserve an invalid index when descriptor offsets or vector components overflow.
// Runtime array lengths are uint32; UINT32_MAX can never name an element.
template<class Context>
Sirit::Id BufferIndexAdd(Context& ctx, Sirit::Id index, Sirit::Id offset) {
    const auto sum=ctx.OpIAdd(ctx.U32[1],index,offset);
    if (!ctx.profile.manual_buffer_bounds) return sum;
    return ctx.OpSelect(ctx.U32[1],ctx.OpULessThan(ctx.TypeBool(),sum,index),
                        ctx.ConstU32(0xffffffffu),sum);
}
// Branch around the memory operation itself: selecting its result would still execute OOB.
template<class Context, class Read>
Sirit::Id GuardedBufferRead(Context& ctx, bool enabled, Sirit::Id type,
                           Sirit::Id buffer, Sirit::Id index, Read read) {
    if (!enabled) return read();
    const auto in_bounds = ctx.OpULessThan(ctx.TypeBool(), index,
        ctx.OpArrayLength(ctx.TypeInt(32, false), buffer, 0u));
    const auto previous = ctx.last_label;
    const auto body = ctx.OpLabel(), merge = ctx.OpLabel();
    ctx.OpSelectionMerge(merge, spv::SelectionControlMask::MaskNone);
    ctx.OpBranchConditional(in_bounds, body, merge);
    ctx.AddLabel(body);
    const auto value = read();
    const auto value_label = ctx.last_label;
    ctx.OpBranch(merge);
    ctx.AddLabel(merge);
    return ctx.OpPhi(type, ctx.ConstantNull(type), previous, value, value_label);
}
template<class Context, class Write>
void GuardedBufferWrite(Context& ctx, bool enabled, Sirit::Id buffer,
                        Sirit::Id index, Write write) {
    if (!enabled) { write(); return; }
    const auto in_bounds = ctx.OpULessThan(ctx.TypeBool(), index,
        ctx.OpArrayLength(ctx.TypeInt(32, false), buffer, 0u));
    const auto body = ctx.OpLabel(), merge = ctx.OpLabel();
    ctx.OpSelectionMerge(merge, spv::SelectionControlMask::MaskNone);
    ctx.OpBranchConditional(in_bounds, body, merge);
    ctx.AddLabel(body);
    write();
    ctx.OpBranch(merge);
    ctx.AddLabel(merge);
}
} // namespace Shader::Backend::SPIRV
