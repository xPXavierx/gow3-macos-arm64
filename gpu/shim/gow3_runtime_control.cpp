// SPDX-License-Identifier: GPL-2.0-or-later
#include "gow3_runtime_control.h"
#include "gow3_overlay.h"
#include "gow3_settings.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <chrono>

namespace Gow3Runtime {
namespace {
void Publish() {
    const auto s=Requested();const auto [w,h]=RenderSize(s);
    std::printf("Runtime graphics: render=%ux%u output=%ux%u MetalFX=%s cap=%s HUD=%d refresh=%u\n",w,h,Widths[s.resolution],Heights[s.resolution],Scales[s.scaling],s.cap?"60":"unlimited",s.hud,refresh_hz.load());
    if(const char* path=std::getenv("GOW3_RUNTIME_STATE")) {
        const std::string temp=std::string(path)+".tmp";
        std::ofstream f(temp);f<<"{\"resolution\":"<<s.resolution<<",\"scaling\":"<<s.scaling<<",\"interpolation\":"<<s.interpolation<<",\"cap\":"<<(s.cap?60:0)<<",\"hud\":"<<(s.hud?"true":"false")<<",\"shortcuts\":"<<(s.shortcuts?"true":"false")<<",\"refresh_hz\":"<<refresh_hz.load()<<"}\n";f.close();
        if(f)std::rename(temp.c_str(),path);
    }
}
}
bool Shortcut(unsigned key) {
    if(!Enabled())return false;
    auto s=Requested();
    switch(key) {
    case 0:s.shortcuts=!s.shortcuts;break;
    case 1:s.hud=!s.hud;break;
    case 2:s.scaling=(s.scaling+1)%4;break;
    case 3:s.resolution=(s.resolution+1)%4;break;
    case 4:s.cap=!s.cap;break;
    default:return false;
    }
    RequestedBits()=Pack(s);Gow3Settings::Get().show_fps=s.hud;Publish();
    char text[256];const auto [w,h]=RenderSize(s);
    std::snprintf(text,sizeof(text),"%u x %u -> %u x %u | %s | %s",w,h,Widths[s.resolution],Heights[s.resolution],Scales[s.scaling],s.cap?"60 FPS":"Uncapped");
    Gow3Overlay::ShowNotice(text,3500);return true;
}
void PollCommands() {
    if(!Enabled())return;
    static auto last=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();if(now-last<std::chrono::milliseconds(100))return;last=now;
    static bool first=true;if(first){Publish();first=false;}
    if(const char* path=std::getenv("GOW3_RUNTIME_COMMAND")) {
        std::ifstream f(path);char key=0;if(f.get(key)){f.close();std::error_code ec;std::filesystem::remove(path,ec);if(key>='0'&&key<='4')Shortcut(key-'0');}
    }
}
void ReportDisplay(unsigned hz) {if(!Enabled()||!hz)return;if(refresh_hz.exchange(hz)!=hz)Publish();}
}
