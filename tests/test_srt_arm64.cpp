// SPDX-License-Identifier: GPL-2.0-or-later
// Hand-authored shader resource-table programs; no game data or x86 execution.
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include "shader_recompiler/ir/passes/srt.h"

int main() {
    assert(Shader::UsePortableSrtWalker());
    std::array<u32, 4> table{10,20,0x12345678,40};
    std::array<u32, 16> user{};
    const uintptr_t pointer=reinterpret_cast<uintptr_t>(table.data());
    std::memcpy(user.data(),&pointer,sizeof(pointer));
    user[2]=1;
    std::array<u32, 32> flat{};
    std::copy(user.begin(),user.end(),flat.begin());
    // Push pointer at user_data[0]; copy pointed dword 2 to flat[16]; pop; end.
    const u32 immediate[]={0x42545253,1,0,4,2,16,3,0};
    auto walker=Shader::RegisterWalkerCode(reinterpret_cast<const u8*>(immediate),sizeof(immediate));
    assert(walker);
    Shader::CallSrtWalker(walker,user.data(),flat.data());
    assert(flat[16]==table[2]);
    // The dynamic offset is flat[2] + 1, evaluated by ARM64 C++ table operations.
    const u32 dynamic[]={0x42545253,1,0,6,2,8,1,9,5,17,3,0};
    walker=Shader::RegisterWalkerCode(reinterpret_cast<const u8*>(dynamic),sizeof(dynamic));
    assert(walker);
    Shader::CallSrtWalker(walker,user.data(),flat.data());
    assert(flat[17]==table[2]);
    // Legal 32-bit extraction must retain every bit; (1u << (32 & 31))-1 is zero.
    for (u32 width = 0; width <= 32; ++width) for (u32 value = 0; value < 4; ++value) {
        user[2] = value;
        std::copy(user.begin(), user.end(), flat.begin());
        const u32 extract[] = {0x42545253,1,0,6,2,8,0,8,width,20,5,18,3,0};
        walker = Shader::RegisterWalkerCode(reinterpret_cast<const u8*>(extract), sizeof(extract));
        Shader::CallSrtWalker(walker, user.data(), flat.data());
        const u32 mask = width == 32 ? 0xffffffffu : (1u << width) - 1;
        if (flat[18] != table[value & mask]) {
            std::cerr << "extract width=" << width << " value=" << value
                      << " got=" << flat[18] << " expected=" << table[value & mask] << "\n";
            return 1;
        }
    }
    const u8 x86_cache[]={0x48,0x89,0xf8,0xc3};
    assert(!Shader::RegisterWalkerCode(x86_cache,sizeof(x86_cache)));
    assert(!Shader::RegisterWalkerCode(reinterpret_cast<const u8*>(immediate),4));
    std::cout << "PASS: portable ARM64 SRT pointer/dynamic-offset reads; x86 cache rejected\n";
}
