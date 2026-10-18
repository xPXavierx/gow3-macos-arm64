// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "shader_recompiler/ir/ir_emitter.h"
#include "video_core/amdgpu/pixel_format.h"

namespace Shader::IR {

/// Applies a component swizzle to a vec4.
inline Value ApplySwizzle(IREmitter& ir, const Value& vector, const AmdGpu::CompMapping& swizzle) {
    // Constants are indexed as 0 and 1, and components are 4-7. Thus we can apply a swizzle
    // using two vectors and a shuffle, using one vector of constants and one of the components.
    const auto zero = ir.Imm32(0.f);
    const auto one = ir.Imm32(1.f);
    const auto constants_vec = ir.CompositeConstruct(zero, one, zero, zero);
    const auto swizzled =
        ir.CompositeShuffle(constants_vec, vector, size_t(swizzle.r), size_t(swizzle.g),
                            size_t(swizzle.b), size_t(swizzle.a));
    return swizzled;
}

/// Converts gamma corrected value to linear space
inline F32 ApplyGammaToLinear(IREmitter& ir, const F32& c) {
    // The nonlinear branch only contributes for c > 0.04045, so its base is
    // positive. Use the native log2/exp2 operations instead of general pow;
    // preserve the post-filter conversion, threshold, linear branch and alpha.
    const F32 base = ir.FPMul(ir.FPAdd(c, ir.Imm32(0.055f)), ir.Imm32(1.0f / 1.055f));
    const F32 a = ir.FPExp2(ir.FPMul(ir.FPLog2(base), ir.Imm32(2.4f)));
    const F32 b = ir.FPMul(c, ir.Imm32(1.0f / 12.92f));
    return IR::F32{ir.Select(ir.FPGreaterThan(c, ir.Imm32(0.04045f)), a, b)};
}

// Bounded post-filter sRGB conversion for normalized texture samples only. Two
// degree-six fits on [0.04045, 0.35] and [0.35, 1] replace the transcendental
// operations with FMAs. The linear branch and alpha remain exact. Never use this
// for float/HDR textures: their existing full-range conversion stays unchanged.
inline F32 ApplyUnormGammaToLinear(IREmitter& ir, const F32& c) {
    constexpr std::array<float, 7> low{0.000841916597f, 0.0358769633f, 0.476204336f,
        0.928877831f, -1.09857428f, 1.29599965f, -0.761302233f};
    constexpr std::array<float, 7> high{0.00188418047f, 0.0210191794f, 0.572105467f,
        0.562709272f, -0.224387035f, 0.0809520110f, -0.0142832091f};
    const auto lower = ir.FPLessThan(c, ir.Imm32(0.35f));
    const auto coefficient = [&](size_t i) {
        return F32{ir.Select(lower, ir.Imm32(low[i]), ir.Imm32(high[i]))};
    };
    F32 nonlinear = coefficient(6);
    for (int i = 5; i >= 0; --i) {
        nonlinear = ir.FPFma(nonlinear, c, coefficient(i));
    }
    const F32 linear = ir.FPMul(c, ir.Imm32(1.0f / 12.92f));
    return F32{ir.Select(ir.FPGreaterThan(c, ir.Imm32(0.04045f)), nonlinear, linear)};
}

inline Value ApplyForceDegamma(IREmitter& ir, const Value& value, bool normalized = false) {
    auto x = F32{ir.CompositeExtract(value, 0)};
    auto y = F32{ir.CompositeExtract(value, 1)};
    auto z = F32{ir.CompositeExtract(value, 2)};
    auto w = F32{ir.CompositeExtract(value, 3)};
    // Gamma correction is only applied to RGB components
    x = normalized ? ApplyUnormGammaToLinear(ir, x) : ApplyGammaToLinear(ir, x);
    y = normalized ? ApplyUnormGammaToLinear(ir, y) : ApplyGammaToLinear(ir, y);
    z = normalized ? ApplyUnormGammaToLinear(ir, z) : ApplyGammaToLinear(ir, z);
    return ir.CompositeConstruct(x, y, z, w);
}

/// Applies a number conversion in the read direction.
inline F32 ApplyReadNumberConversion(IREmitter& ir, const F32& value,
                                     const AmdGpu::NumberConversion& conversion) {
    switch (conversion) {
    case AmdGpu::NumberConversion::None:
        return value;
    case AmdGpu::NumberConversion::UintToUscaled:
        return ir.ConvertUToF(32, 32, ir.BitCast<U32>(value));
    case AmdGpu::NumberConversion::SintToSscaled:
        return ir.ConvertSToF(32, 32, ir.BitCast<U32>(value));
    case AmdGpu::NumberConversion::UnormToUbnorm:
        // Convert 0...1 to -1...1
        return ir.FPSub(ir.FPMul(value, ir.Imm32(2.f)), ir.Imm32(1.f));
    case AmdGpu::NumberConversion::Sint8ToSnormNz: {
        const IR::U32 additon = ir.IAdd(ir.IMul(ir.BitCast<U32>(value), ir.Imm32(2)), ir.Imm32(1));
        const IR::F32 left = ir.ConvertSToF(32, 32, additon);
        const IR::F32 max = ir.Imm32(float(std::numeric_limits<u8>::max()));
        return ir.FPDiv(left, max);
    }
    case AmdGpu::NumberConversion::Sint16ToSnormNz: {
        const IR::U32 additon = ir.IAdd(ir.IMul(ir.BitCast<U32>(value), ir.Imm32(2)), ir.Imm32(1));
        const IR::F32 left = ir.ConvertSToF(32, 32, additon);
        const IR::F32 max = ir.Imm32(float(std::numeric_limits<u16>::max()));
        return ir.FPDiv(left, max);
    }
    case AmdGpu::NumberConversion::Uint32ToUnorm: {
        const auto float_val = ir.ConvertUToF(32, 32, ir.BitCast<U32>(value));
        return ir.FPDiv(float_val, ir.Imm32(static_cast<float>(std::numeric_limits<u32>::max())));
    }
    case AmdGpu::NumberConversion::SrgbToNorm: {
        return ApplyGammaToLinear(ir, value);
    }
    default:
        UNREACHABLE();
    }
}

inline Value ApplyReadNumberConversionVec4(IREmitter& ir, const Value& value,
                                           const AmdGpu::NumberConversion& conversion) {
    if (conversion == AmdGpu::NumberConversion::None) {
        return value;
    }
    const auto x = ApplyReadNumberConversion(ir, F32{ir.CompositeExtract(value, 0)}, conversion);
    const auto y = ApplyReadNumberConversion(ir, F32{ir.CompositeExtract(value, 1)}, conversion);
    const auto z = ApplyReadNumberConversion(ir, F32{ir.CompositeExtract(value, 2)}, conversion);
    const auto w = ApplyReadNumberConversion(ir, F32{ir.CompositeExtract(value, 3)}, conversion);
    return ir.CompositeConstruct(x, y, z, w);
}

/// Applies a number conversion in the write direction.
inline F32 ApplyWriteNumberConversion(IREmitter& ir, const F32& value,
                                      const AmdGpu::NumberConversion& conversion) {
    switch (conversion) {
    case AmdGpu::NumberConversion::None:
        return value;
    case AmdGpu::NumberConversion::UintToUscaled:
        // Need to return float type to maintain IR semantics.
        return ir.BitCast<F32>(U32{ir.ConvertFToU(32, value)});
    case AmdGpu::NumberConversion::SintToSscaled:
        // Need to return float type to maintain IR semantics.
        return ir.BitCast<F32>(U32{ir.ConvertFToS(32, value)});
    case AmdGpu::NumberConversion::UnormToUbnorm:
        // Convert -1...1 to 0...1
        return ir.FPDiv(ir.FPAdd(value, ir.Imm32(1.f)), ir.Imm32(2.f));
    case AmdGpu::NumberConversion::Sint8ToSnormNz: {
        const IR::F32 max = ir.Imm32(float(std::numeric_limits<u8>::max()));
        const IR::F32 mul = ir.FPMul(ir.FPClamp(value, ir.Imm32(-1.f), ir.Imm32(1.f)), max);
        const IR::F32 left = ir.FPSub(mul, ir.Imm32(1.f));
        const IR::U32 raw = ir.ConvertFToS(32, ir.FPDiv(left, ir.Imm32(2.f)));
        return ir.BitCast<F32>(raw);
    }
    case AmdGpu::NumberConversion::Sint16ToSnormNz: {
        const IR::F32 max = ir.Imm32(float(std::numeric_limits<u16>::max()));
        const IR::F32 mul = ir.FPMul(ir.FPClamp(value, ir.Imm32(-1.f), ir.Imm32(1.f)), max);
        const IR::F32 left = ir.FPSub(mul, ir.Imm32(1.f));
        const IR::U32 raw = ir.ConvertFToS(32, ir.FPDiv(left, ir.Imm32(2.f)));
        return ir.BitCast<F32>(raw);
    }
    case AmdGpu::NumberConversion::Uint32ToUnorm: {
        const auto clamped = ir.FPClamp(value, ir.Imm32(0.f), ir.Imm32(1.f));
        const auto unnormalized =
            ir.FPMul(clamped, ir.Imm32(static_cast<float>(std::numeric_limits<u32>::max())));
        return ir.BitCast<F32>(U32{ir.ConvertFToU(32, unnormalized)});
    }
    default:
        UNREACHABLE();
    }
}

inline Value ApplyWriteNumberConversionVec4(IREmitter& ir, const Value& value,
                                            const AmdGpu::NumberConversion& conversion) {
    if (conversion == AmdGpu::NumberConversion::None) {
        return value;
    }
    const auto x = ApplyWriteNumberConversion(ir, F32{ir.CompositeExtract(value, 0)}, conversion);
    const auto y = ApplyWriteNumberConversion(ir, F32{ir.CompositeExtract(value, 1)}, conversion);
    const auto z = ApplyWriteNumberConversion(ir, F32{ir.CompositeExtract(value, 2)}, conversion);
    const auto w = ApplyWriteNumberConversion(ir, F32{ir.CompositeExtract(value, 3)}, conversion);
    return ir.CompositeConstruct(x, y, z, w);
}

} // namespace Shader::IR
