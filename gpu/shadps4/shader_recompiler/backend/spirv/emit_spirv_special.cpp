// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "shader_recompiler/ir/debug_print.h"
#include "shader_recompiler/ir/microinstruction.h"

namespace Shader::Backend::SPIRV {

void EmitPrologue(EmitContext& ctx) {
    if (ctx.hw_stage == HwStage::Fragment) {
        ctx.DefineAmdPerVertexAttribs();
        if (ctx.reconstruct_linear_barycentrics) {
            // n_i = perspective_i * FragCoord.w = lambda_i / clip_w_i is affine.
            // grad_x(n) x grad_y(n) is orthogonal to both gradients and therefore
            // proportional to the three original clip W values. Evaluate before
            // guest branches/discards so derivatives see a complete fragment quad.
            const Id q = ctx.OpCompositeExtract(ctx.F32[1], ctx.OpLoad(ctx.F32[4], ctx.frag_coord), 3u);
            const Id q3 = ctx.OpCompositeConstruct(ctx.F32[3], std::array<Id, 3>{q,q,q});
            const Id n = ctx.OpFMul(ctx.F32[3], ctx.OpLoad(ctx.F32[3], ctx.bary_coord), q3);
            ctx.bary_weight_scale = ctx.OpCross(ctx.F32[3],
                ctx.OpDPdxFine(ctx.F32[3], n), ctx.OpDPdyFine(ctx.F32[3], n));
        }
    }
    if (ctx.info.loads.Get(IR::Attribute::WorkgroupIndex)) {
        ctx.DefineWorkgroupIndex();
    }
    ctx.DefineBufferProperties();
}

void ConvertDepthMode(EmitContext& ctx) {
    const Id type{ctx.F32[1]};
    const Id position{ctx.OpLoad(ctx.F32[4], ctx.output_position)};
    const Id z{ctx.OpCompositeExtract(type, position, 2u)};
    const Id w{ctx.OpCompositeExtract(type, position, 3u)};
    const Id screen_depth{ctx.OpFMul(type, ctx.OpFAdd(type, z, w), ctx.Constant(type, 0.5f))};
    const Id vector{ctx.OpCompositeInsert(ctx.F32[4], screen_depth, position, 2u)};
    ctx.OpStore(ctx.output_position, vector);
}

void ConvertPositionToClipSpace(EmitContext& ctx) {
    ASSERT_MSG(!ctx.info.stores.GetAny(IR::Attribute::ViewportIndex),
               "Multi-viewport with shader clip space conversion not yet implemented.");

    const Id type{ctx.F32[1]};
    Id position{ctx.OpLoad(ctx.F32[4], ctx.output_position)};
    const Id x{ctx.OpCompositeExtract(type, position, 0u)};
    const Id y{ctx.OpCompositeExtract(type, position, 1u)};
    const Id z{ctx.OpCompositeExtract(type, position, 2u)};
    const Id w{ctx.OpCompositeExtract(type, position, 3u)};
    const Id xoffset_ptr{ctx.OpAccessChain(ctx.TypePointer(spv::StorageClass::PushConstant, type),
                                           ctx.push_data_block,
                                           ctx.ConstU32(PushData::XOffsetIndex))};
    const Id xoffset{ctx.OpLoad(type, xoffset_ptr)};
    const Id yoffset_ptr{ctx.OpAccessChain(ctx.TypePointer(spv::StorageClass::PushConstant, type),
                                           ctx.push_data_block,
                                           ctx.ConstU32(PushData::YOffsetIndex))};
    const Id yoffset{ctx.OpLoad(type, yoffset_ptr)};
    const Id xscale_ptr{ctx.OpAccessChain(ctx.TypePointer(spv::StorageClass::PushConstant, type),
                                          ctx.push_data_block,
                                          ctx.ConstU32(PushData::XScaleIndex))};
    const Id xscale{ctx.OpLoad(type, xscale_ptr)};
    const Id yscale_ptr{ctx.OpAccessChain(ctx.TypePointer(spv::StorageClass::PushConstant, type),
                                          ctx.push_data_block,
                                          ctx.ConstU32(PushData::YScaleIndex))};
    const Id yscale{ctx.OpLoad(type, yscale_ptr)};
    const Id vport_w =
        ctx.Constant(type, float(std::min<u32>(ctx.profile.max_viewport_width / 2, 8_KB)));
    const Id wnd_x = ctx.OpFAdd(type, ctx.OpFMul(type, x, xscale), xoffset);
    const Id ndc_x = ctx.OpFSub(type, ctx.OpFDiv(type, wnd_x, vport_w), ctx.Constant(type, 1.f));
    const Id vport_h =
        ctx.Constant(type, float(std::min<u32>(ctx.profile.max_viewport_height / 2, 8_KB)));
    const Id wnd_y = ctx.OpFAdd(type, ctx.OpFMul(type, y, yscale), yoffset);
    const Id ndc_y = ctx.OpFSub(type, ctx.OpFDiv(type, wnd_y, vport_h), ctx.Constant(type, 1.f));
    const Id vector{ctx.OpCompositeConstruct(ctx.F32[4], std::array<Id, 4>({ndc_x, ndc_y, z, w}))};
    ctx.OpStore(ctx.output_position, vector);
}

// gow3: object motion vectors (runtime_info.h, MotionVectors). The vertex shader stores its
// clip position for this frame and loads the one of the previous frame (same draw, same
// vertex), through two explicit storage bindings. Disabled accesses are branched around: a shared
// scratch element would race between all the inactive vertex invocations.
static void EmitVertexMotion(EmitContext& ctx) {
    const Id u32_type = ctx.U32[1];
    const Id position = ctx.OpLoad(ctx.F32[4], ctx.output_position);
    const Id param_ptr =
        ctx.OpAccessChain(ctx.TypePointer(spv::StorageClass::PushConstant, u32_type),
                          ctx.push_data_block, ctx.ConstU32(PushData::MotionParamIndex));
    const Id param_index = ctx.OpLoad(u32_type, param_ptr);
    const Id scalar_ptr = ctx.TypePointer(spv::StorageClass::StorageBuffer, u32_type);
    const auto parameter = [&](u32 offset) {
        const Id at = ctx.OpIAdd(u32_type, ctx.OpIMul(u32_type, param_index, ctx.ConstU32(8u)),
                                 ctx.ConstU32(offset));
        return ctx.OpLoad(u32_type,
                          ctx.OpAccessChain(scalar_ptr, ctx.motion_params, ctx.u32_zero_value, at));
    };
    const Id store_base = parameter(0), load_base = parameter(1), vertices = parameter(2),
             flags = parameter(3);
    const Id first_vertex = parameter(4), first_instance = parameter(5), instances = parameter(6);
    const Id vertex = ctx.OpISub(u32_type, ctx.OpLoad(u32_type, ctx.vertex_index), first_vertex);
    const Id instance = ctx.OpISub(u32_type, ctx.OpLoad(u32_type, ctx.instance_id), first_instance);
    const Id bool_type = ctx.U1[1];
    const Id in_range = ctx.OpLogicalAnd(bool_type, ctx.OpULessThan(bool_type, vertex, vertices),
                                         ctx.OpULessThan(bool_type, instance, instances));
    const Id slot = ctx.OpIAdd(u32_type, vertex, ctx.OpIMul(u32_type, instance, vertices));
    const auto flag = [&](u32 bit) {
        return ctx.OpLogicalAnd(
            bool_type, in_range,
            ctx.OpINotEqual(bool_type, ctx.OpBitwiseAnd(u32_type, flags, ctx.ConstU32(bit)),
                            ctx.u32_zero_value));
    };
    const Id do_store = flag(MotionVectors::FlagStore);
    const Id do_load = flag(MotionVectors::FlagLoad);
    const Id store_label = ctx.OpLabel();
    const Id store_merge = ctx.OpLabel();
    ctx.OpSelectionMerge(store_merge, spv::SelectionControlMask::MaskNone);
    ctx.OpBranchConditional(do_store, store_label, store_merge);
    ctx.AddLabel(store_label);
    const auto position_ptr = [&](Id base, u32 component) {
        const Id at = ctx.OpIAdd(
            u32_type, ctx.OpIMul(u32_type, ctx.OpIAdd(u32_type, base, slot), ctx.ConstU32(4u)),
            ctx.ConstU32(component));
        return ctx.OpAccessChain(scalar_ptr, ctx.motion_positions, ctx.u32_zero_value, at);
    };
    // Duplicate indexed invocations store identical positions; keep atomic writes.
    const Id scope = ctx.ConstU32(static_cast<u32>(spv::Scope::Device));
    for (u32 i = 0; i < 4; ++i) {
        // Previous Z is replaced by validity below. Only X, Y and W are consumed.
        if (i == 2)
            continue;
        const Id ptr = position_ptr(store_base, i);
        const Id bits = ctx.OpBitcast(u32_type, ctx.OpCompositeExtract(ctx.F32[1], position, i));
        ctx.OpAtomicExchange(u32_type, ptr, scope, ctx.u32_zero_value, bits);
    }
    ctx.OpBranch(store_merge);
    ctx.AddLabel(store_merge);

    const Id load_label = ctx.OpLabel();
    const Id load_merge = ctx.OpLabel();
    ctx.OpSelectionMerge(load_merge, spv::SelectionControlMask::MaskNone);
    ctx.OpBranchConditional(do_load, load_label, load_merge);
    ctx.AddLabel(load_label);
    std::array<Id, 4> components;
    for (u32 i = 0; i < 4; ++i)
        components[i] =
            i == 2 ? ctx.Constant(ctx.F32[1], 0.f)
                   : ctx.OpBitcast(ctx.F32[1], ctx.OpLoad(u32_type, position_ptr(load_base, i)));
    const Id loaded = ctx.OpCompositeConstruct(ctx.F32[4], components);
    ctx.OpBranch(load_merge);
    ctx.AddLabel(load_merge);
    Id previous = ctx.OpPhi(ctx.F32[4], position, store_merge, loaded, load_label);
    const Id valid = ctx.OpSelect(ctx.F32[1], do_load, ctx.Constant(ctx.F32[1], 1.0f),
                                  ctx.Constant(ctx.F32[1], 0.0f));
    previous = ctx.OpCompositeInsert(ctx.F32[4], valid, previous, 2u);
    ctx.OpStore(ctx.motion_out_cur, position);
    ctx.OpStore(ctx.motion_out_prev, previous);
}

// Screen-space motion (previous minus current, pixels) through the viewport scale; z = valid.
static void EmitFragmentMotion(EmitContext& ctx) {
    const Id f32_type = ctx.F32[1];
    const Id current = ctx.OpLoad(ctx.F32[4], ctx.motion_in_cur);
    const Id previous = ctx.OpLoad(ctx.F32[4], ctx.motion_in_prev);
    const auto push = [&](u32 index) {
        return ctx.OpLoad(f32_type, ctx.OpAccessChain(ctx.TypePointer(spv::StorageClass::PushConstant,
                                                                 f32_type),
                                                 ctx.push_data_block, ctx.ConstU32(index)));
    };
    const Id xscale = push(PushData::XScaleIndex);
    const Id yscale = push(PushData::YScaleIndex);
    const auto ndc = [&](Id clip, u32 component) {
        return ctx.OpFDiv(f32_type, ctx.OpCompositeExtract(f32_type, clip, component),
                          ctx.OpCompositeExtract(f32_type, clip, 3u));
    };
    const Id dx = ctx.OpFMul(f32_type, ctx.OpFSub(f32_type, ndc(previous, 0u), ndc(current, 0u)), xscale);
    const Id dy = ctx.OpFMul(f32_type, ctx.OpFSub(f32_type, ndc(previous, 1u), ndc(current, 1u)), yscale);
    const Id zero = ctx.Constant(f32_type, 0.0f);
    const Id epsilon = ctx.Constant(f32_type, 1e-5f);
    const Id front = ctx.OpLogicalAnd(ctx.U1[1],
        ctx.OpFOrdGreaterThan(ctx.U1[1], ctx.OpCompositeExtract(f32_type, previous, 3u), epsilon),
        ctx.OpFOrdGreaterThan(ctx.U1[1], ctx.OpCompositeExtract(f32_type, current, 3u), epsilon));
    const Id valid = ctx.OpSelect(f32_type, front,
                                  ctx.OpCompositeExtract(f32_type, previous, 2u), zero);
    // Resolve interpolated history validity before storing motion. A compact RG16
    // attachment can then represent invalid history as (0,0), exactly matching
    // the compositor's previous validity-channel threshold without storing Z/W.
    const Id rounded_valid = ctx.OpQuantizeToF16(f32_type, valid);
    const Id usable = ctx.OpFOrdGreaterThan(ctx.U1[1], rounded_valid, ctx.Constant(f32_type, 0.99f));
    // Uninstrumented draws can overwrite the final scene depth after this pixel was
    // written. Preserve depth so the compose pass can reject an occluded vector.
    const Id depth = ctx.OpCompositeExtract(f32_type,
                                            ctx.OpLoad(ctx.F32[4], ctx.frag_coord), 2u);
    ctx.OpStore(ctx.motion_frag_out,
                ctx.OpCompositeConstruct(ctx.F32[4], std::array<Id, 4>{
                    ctx.OpSelect(f32_type, usable, dx, zero),
                    ctx.OpSelect(f32_type, usable, dy, zero), valid, depth}));
}

void EmitEpilogue(EmitContext& ctx) {
    if (Sirit::ValidId(ctx.motion_out_cur)) {
        EmitVertexMotion(ctx);
    }
    if (Sirit::ValidId(ctx.motion_frag_out)) {
        EmitFragmentMotion(ctx);
    }
    if (ctx.hw_stage == HwStage::Vertex &&
        ctx.runtime_info.hw.vs.emulate_depth_negative_one_to_one) {
        ConvertDepthMode(ctx);
    }
    if (ctx.hw_stage == HwStage::Vertex && ctx.runtime_info.hw.vs.clip_disable) {
        ConvertPositionToClipSpace(ctx);
    }
}

void EmitDiscard(EmitContext& ctx) {
    ctx.OpDemoteToHelperInvocationEXT();
}

void EmitDiscardCond(EmitContext& ctx, Id condition) {
    const Id kill_label{ctx.OpLabel()};
    const Id merge_label{ctx.OpLabel()};
    ctx.OpSelectionMerge(merge_label, spv::SelectionControlMask::MaskNone);
    ctx.OpBranchConditional(condition, kill_label, merge_label);
    ctx.AddLabel(kill_label);
    ctx.OpDemoteToHelperInvocationEXT();
    ctx.OpBranch(merge_label);
    ctx.AddLabel(merge_label);
}

void EmitEmitVertex(EmitContext& ctx) {
    ctx.OpEmitVertex();
}

void EmitEmitPrimitive(EmitContext& ctx) {
    ctx.OpEndPrimitive();
}

void EmitEmitVertex(EmitContext& ctx, const IR::Value& stream) {
    UNREACHABLE_MSG("Geometry streams");
}

void EmitEndPrimitive(EmitContext& ctx, const IR::Value& stream) {
    UNREACHABLE_MSG("Geometry streams");
}

void EmitDebugPrint(EmitContext& ctx, IR::Inst* inst, Id fmt, Id arg0, Id arg1, Id arg2, Id arg3) {
    IR::DebugPrintFlags flags = inst->Flags<IR::DebugPrintFlags>();
    std::array<Id, IR::DEBUGPRINT_NUM_FORMAT_ARGS> fmt_args = {arg0, arg1, arg2, arg3};
    auto fmt_args_span = std::span<Id>(fmt_args.begin(), fmt_args.begin() + flags.num_args);
    ctx.OpDebugPrintf(fmt, fmt_args_span);
}

Id EmitMemtime(EmitContext& ctx) {
    if (ctx.profile.supports_shader_subgroup_clock) {
        return ctx.OpReadClockKHR(ctx.U64, ctx.ConstU32(std::to_underlying(spv::Scope::Subgroup)));
    } else {
        return ctx.Constant(ctx.U64, 1U);
    }
}

} // namespace Shader::Backend::SPIRV
