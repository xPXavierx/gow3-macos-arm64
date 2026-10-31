// SPDX-License-Identifier: GPL-2.0-or-later
// Exact nearest scene-proxy copy. The full destination is overwritten.
#version 450 core
#extension GL_EXT_samplerless_texture_functions : require
layout (binding = 0, set = 0) uniform texture2D source_image;
layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 color;
void main() {
    const ivec2 size = textureSize(source_image, 0);
    const ivec2 coord = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    color = texelFetch(source_image, coord, 0);
}
