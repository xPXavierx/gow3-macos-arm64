// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace VideoCore::ReplacementData {
// Private, local texture packs. Exact original BC blocks identify the asset, never an address.
// RGBA8 replacement data includes a complete mip pyramid; only 2x color maps are accepted.
struct Pack {
    uint32_t width{}, height{}, block{}, out_width{}, out_height{}, levels{};
    std::vector<uint8_t> original, rgba;
};
inline bool Decode(std::span<const uint8_t> bytes, Pack& output) {
    constexpr size_t header = 40;
    if (bytes.size() < header || std::memcmp(bytes.data(), "G3TXv001", 8)) return false;
    const auto word = [&](size_t p) {
        return uint32_t(bytes[p]) | uint32_t(bytes[p+1])<<8 |
               uint32_t(bytes[p+2])<<16 | uint32_t(bytes[p+3])<<24;
    };
    Pack p;
    p.width=word(8); p.height=word(12); p.block=word(16);
    const uint32_t original_size=word(20);
    p.out_width=word(24); p.out_height=word(28); p.levels=word(32);
    if (word(36)!=1 || p.width<32 || p.height<32 || p.width>2048 || p.height>2048 ||
        (p.block!=8 && p.block!=16) || p.out_width!=p.width*2 || p.out_height!=p.height*2 ||
        original_size!=size_t((p.width+3)/4)*((p.height+3)/4)*p.block) return false;
    size_t total=0; uint32_t levels=0;
    for (uint32_t w=p.out_width,h=p.out_height;;w=std::max(w/2,1u),h=std::max(h/2,1u)) {
        total+=size_t(w)*h*4; ++levels;
        if (w==1 && h==1) break;
    }
    if (p.levels!=levels || bytes.size()!=header+original_size+total) return false;
    p.original.assign(bytes.begin()+header, bytes.begin()+header+original_size);
    p.rgba.assign(bytes.begin()+header+original_size,bytes.end());
    output=std::move(p); return true;
}

} // namespace VideoCore::ReplacementData
