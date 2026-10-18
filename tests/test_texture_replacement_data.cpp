// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include "video_core/texture_cache/replacement_data.h"
using namespace VideoCore::ReplacementData;
int main() {
    // Parser rejects damaged lengths, oversized dimensions, incomplete mips and wrong versions.
    std::vector<uint8_t> bytes(40+32*32/2+64*64*4+32*32*4+16*16*4+8*8*4+4*4*4+2*2*4+4);
    std::memcpy(bytes.data(),"G3TXv001",8);
    const auto put=[&](unsigned offset,uint32_t v) {
        for (unsigned b=0;b<4;++b) bytes[offset+b]=uint8_t(v>>(8*b));
    };
    put(8,32);put(12,32);put(16,8);put(20,512);put(24,64);put(28,64);put(32,7);put(36,1);
    Pack p; assert(Decode(bytes,p)); assert(p.original.size()==512 && p.levels==7);
    bytes.push_back(0); assert(!Decode(bytes,p));bytes.pop_back();
    auto good=bytes;
    for (auto off:{0u,8u,12u,16u,20u,24u,28u,32u,36u}) {
        bytes=good; bytes[off]^=1;assert(!Decode(bytes,p));
    }
    bytes=good;
    for (size_t n=0;n<bytes.size();n+=97) assert(!Decode(std::span(bytes).first(n),p));
    for (const auto block:{4u,8u,16u}) for (const auto layers:{1u,6u}) {
        Pack expected;expected.out_width=64;expected.out_height=64;expected.layers=layers;expected.encoding=2;
        uint64_t payload=0;
        for (uint32_t w=64;w;w/=2) payload+=MipBytes(expected,w,w);
        const uint32_t original=(block==4?32*32*4:8*8*block)*layers;
        bytes.assign(64+original+payload,0);std::memcpy(bytes.data(),"G3TXv002",8);
        put(8,32);put(12,32);put(16,block);put(20,layers);put(24,original);
        put(32,64);put(36,64);put(40,7);put(44,2);put(48,payload);
        assert(Decode(bytes,p));assert(p.layers==layers && p.encoding==2 && p.rgba.size()==payload);
        assert(DecodeHeader(std::span(bytes).first(64),bytes.size(),p));assert(p.rgba.empty());
        good=bytes;
        for (const auto off:{0u,20u,24u,28u,32u,36u,40u,44u,48u,52u,56u,60u}) {
            bytes=good;bytes[off]^=1;assert(!Decode(bytes,p));
        }
        bytes=good;assert(!Decode(std::span(bytes).first(bytes.size()-1),p));
    }
    std::puts("Texture replacement format, dimensions, complete mips and truncated/corrupt header rejection: PASS");
}
