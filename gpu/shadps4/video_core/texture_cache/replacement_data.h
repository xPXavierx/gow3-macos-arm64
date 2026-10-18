// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace VideoCore::ReplacementData {
// Exact original GPU base bytes identify the asset. Payloads are mip-major, then layer.
struct Pack {
    uint32_t width{},height{},block{},out_width{},out_height{},levels{},layers=1,encoding=1;
    uint64_t original_size{},payload_size{},payload_offset{};
    std::vector<uint8_t> original,rgba;
};
inline uint64_t MipBytes(const Pack& p,uint32_t w,uint32_t h) {
    return p.encoding==2 ? uint64_t((w+3)/4)*((h+3)/4)*16*p.layers
                         : uint64_t(w)*h*4*p.layers;
}
inline bool DecodeHeader(std::span<const uint8_t> bytes,uint64_t file_size,Pack& output) {
    if (bytes.size()<40) return false;
    const bool v1=!std::memcmp(bytes.data(),"G3TXv001",8);
    const bool v2=!std::memcmp(bytes.data(),"G3TXv002",8);
    if (!v1 && !v2) return false;
    if (v2 && bytes.size()<64) return false;
    const auto word=[&](size_t i) {return uint32_t(bytes[i])|uint32_t(bytes[i+1])<<8|
        uint32_t(bytes[i+2])<<16|uint32_t(bytes[i+3])<<24;};
    const auto wide=[&](size_t i) {return uint64_t(word(i))|uint64_t(word(i+4))<<32;};
    Pack p;p.width=word(8);p.height=word(12);p.block=word(16);
    if (v1) {
        p.original_size=word(20);p.out_width=word(24);p.out_height=word(28);p.levels=word(32);
        if (word(36)!=1 || p.width<32 || p.height<32 || p.block==4) return false;
    } else {
        p.layers=word(20);p.original_size=wide(24);p.out_width=word(32);p.out_height=word(36);
        p.levels=word(40);p.encoding=word(44);p.payload_size=wide(48);
        if (wide(56) || (p.encoding!=1 && p.encoding!=2)) return false;
    }
    if (!p.width || !p.height || p.width>2048 || p.height>2048 ||
        (p.block!=4 && p.block!=8 && p.block!=16) || (p.layers!=1 && p.layers!=6) ||
        p.out_width!=p.width*2 || p.out_height!=p.height*2) return false;
    const auto base=p.block==4?uint64_t(p.width)*p.height*4:
        uint64_t((p.width+3)/4)*((p.height+3)/4)*p.block;
    if (p.original_size!=base*p.layers) return false;
    uint64_t total=0;uint32_t count=0;
    for (uint32_t w=p.out_width,h=p.out_height;;w=std::max(w/2,1u),h=std::max(h/2,1u)) {
        total+=MipBytes(p,w,h);++count;if (w==1 && h==1) break;
    }
    p.payload_offset=(v1?40:64)+p.original_size;
    if (p.levels!=count || (v2 && p.payload_size!=total) || file_size!=p.payload_offset+total) return false;
    p.payload_size=total;output=std::move(p);return true;
}
inline bool Decode(std::span<const uint8_t> bytes,Pack& output) {
    Pack p;if (!DecodeHeader(bytes,bytes.size(),p)) return false;
    p.original.assign(bytes.begin()+p.payload_offset-p.original_size,bytes.begin()+p.payload_offset);
    p.rgba.assign(bytes.begin()+p.payload_offset,bytes.end());output=std::move(p);return true;
}
} // namespace VideoCore::ReplacementData
