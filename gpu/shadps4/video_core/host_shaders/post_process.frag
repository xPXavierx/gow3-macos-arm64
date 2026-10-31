// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 color;

layout (binding = 0) uniform sampler2D texSampler;

layout (push_constant) uniform settings {
    float gamma;
    bool hdr;
    bool srgb_input;
    float rcas_attenuation;
    vec4 picture; // gow3: x0, y0, x1, y1 of the part shown; the rest is black (cutscene bars)
} pp;

#ifdef FUSED_RCAS
#extension GL_GOOGLE_include_directive : require
#define A_GPU 1
#define A_GLSL 1
#define A_HALF
#include "fsr/ffx_a.h"
#define FSR_RCAS_H
AH4 FsrRcasLoadH(ASW2 p) {
    return AH4(texelFetch(texSampler, ASU2(p), 0));
}
void FsrRcasInputH(inout AH1 r, inout AH1 g, inout AH1 b) {}
#include "fsr/ffx_fsr1.h"
#endif

const float cutoff = 0.0031308, a = 1.055, b = 0.055, d = 12.92;
vec3 gamma(vec3 rgb) {
    return mix(
        a * pow(rgb, vec3(1.0 / (2.4 + 1.0 - pp.gamma))) - b,
        d * rgb / pp.gamma,
        lessThan(rgb, vec3(cutoff))
    );
}

// Exact inverse of gamma() at unit gamma, for buffers that are sRGB encoded but must be sampled
// through a UNORM view because Vulkan has no sRGB variant of the 10-bit format.
vec3 degamma(vec3 rgb) {
    return mix(
        pow(max(rgb + b, 0.0) / a, vec3(2.4)),
        rgb / d,
        lessThan(rgb, vec3(d * cutoff))
    );
}

void main() {
    if (any(lessThan(uv, pp.picture.xy)) || any(greaterThan(uv, pp.picture.zw))) {
        color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec4 color_linear;
#ifdef FUSED_RCAS
    if (pp.rcas_attenuation >= 0.0) {
        AU4 con;
        FsrRcasCon(con, pp.rcas_attenuation);
        AH3 c;
        FsrRcasH(c.r, c.g, c.b, AU2(gl_FragCoord.xy), con);
        // Same half-precision RCAS as the compute pass, immediately consumed
        // by gamma/output encoding instead of storing and sampling RGBA16F.
        color_linear = vec4(vec3(c), 1.0);
    } else
#endif
        color_linear = texture(texSampler, uv);
    if (pp.hdr) {
        color = color_linear;
    } else {
        if (pp.srgb_input) color_linear.rgb = degamma(color_linear.rgb);
        color = vec4(gamma(color_linear.rgb), color_linear.a);
    }
}
