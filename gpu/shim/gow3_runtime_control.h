// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <utility>
namespace Gow3Runtime {
struct State {
    unsigned resolution=3, scaling=0, interpolation=0;
    bool cap=true, hud=true, shortcuts=true;
    bool operator==(const State&) const = default;
};
// Bits 4-5 stay reserved so old state cannot enable interpolation.
inline unsigned Pack(State s) { return s.resolution | s.scaling<<2 | unsigned(s.cap)<<6 | unsigned(s.hud)<<7 | unsigned(s.shortcuts)<<8; }
inline State Unpack(unsigned n) { return {n&3,(n>>2)&3,0,bool(n&64),bool(n&128),bool(n&256)}; }
inline bool Enabled() { static const bool on=[] {const char* p=std::getenv("GOW3_RUNTIME_CONTROLS");return p&&p[0]=='1';}(); return on; }
inline State Initial() {
    State s; const char* p=std::getenv("GOW3_RUNTIME_RESOLUTION"); if(p)s.resolution=std::clamp(std::atoi(p),0,3);
    p=std::getenv("GOW3_METALFX_PRESET"); if(p)s.scaling=!std::strcmp(p,"quality")?1:!std::strcmp(p,"balanced")?2:!std::strcmp(p,"performance")?3:0;
    p=std::getenv("GOW3_RUNTIME_CAP");s.cap=!p||std::strcmp(p,"0");
    p=std::getenv("GOW3_RUNTIME_HUD");s.hud=!p||std::strcmp(p,"0");
    p=std::getenv("GOW3_SHORTCUTS");s.shortcuts=!p||std::strcmp(p,"0");
    return s;
}
inline std::atomic<unsigned>& RequestedBits(){ static std::atomic<unsigned> bits{Pack(Initial())}; return bits; }
inline std::atomic<unsigned>& AppliedBits(){ static std::atomic<unsigned> bits{RequestedBits().load()}; return bits; }
inline State Requested(){return Unpack(RequestedBits().load());}
inline State Applied(){return Unpack(AppliedBits().load());}
inline bool Latch(){const auto next=RequestedBits().load();return AppliedBits().exchange(next)!=next;}
inline std::atomic<bool> interpolation_supported{false};
inline std::atomic<unsigned> refresh_hz{120};
inline std::atomic<uint64_t> scene_depth_address{};
inline constexpr unsigned Widths[]{1280,1920,2560,3840}, Heights[]{720,1080,1440,2160};
inline constexpr const char* Scales[]{"native","quality","balanced","performance"};
inline constexpr const char* Modes[]{"Off","30 + 30","Half refresh rate","Uncapped"};
inline unsigned RealLimit(State s,unsigned hz) {
    return s.cap?60:std::min(hz,120u);
}
inline std::pair<unsigned,unsigned> RenderSize(State s) {
    constexpr double divisors[]{1,1.5,1.7,2};
    if(!s.scaling)return {Widths[s.resolution],Heights[s.resolution]};
    const auto units=unsigned(std::lround(Heights[s.resolution]/divisors[s.scaling]/18));
    return {units*32,units*18};
}
// SDL/window thread only. No application preferences are written by the renderer.
bool Shortcut(unsigned key);
void PollCommands();
void ReportDisplay(unsigned hz);
}
