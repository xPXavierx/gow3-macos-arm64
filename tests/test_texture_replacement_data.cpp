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
    std::puts("Texture replacement format, dimensions, complete mips and truncated/corrupt header rejection: PASS");
}
