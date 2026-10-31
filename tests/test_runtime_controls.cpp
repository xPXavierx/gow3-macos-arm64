// SPDX-License-Identifier: GPL-2.0-or-later
#include "gow3_runtime_control.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace Gow3Runtime;
    for(unsigned r=0;r<4;++r)for(unsigned s=0;s<4;++s)for(unsigned f=0;f<4;++f)
        for(bool cap:{false,true})for(bool hud:{false,true})for(bool shortcuts:{false,true}) {
            State state{r,s,f,cap,hud,shortcuts};auto safe=state;safe.interpolation=0;
            assert(Unpack(Pack(state))==safe);
            assert(Unpack(Pack(state)|(f<<4))==safe);
            auto [w,h]=RenderSize(state);assert(w*9==h*16 && w%2==0 && h%2==0);
            assert(w<=Widths[r] && h<=Heights[r]);
        }
    setenv("GOW3_FRAMEGEN_MODE","3",1);assert(Initial().interpolation==0);
    State s;assert(RealLimit(s,120)==60);
    for(unsigned retired=1;retired<4;++retired){s.interpolation=retired;assert(RealLimit(s,120)==60);}
    s.interpolation=0;s.cap=false;assert(RealLimit(s,120)==120);
    (void)AppliedBits();RequestedBits()=Pack(s);assert(Latch());assert(Applied()==s);assert(!Latch());
    s.scaling=2;s.resolution=3;assert((RenderSize(s)==std::pair<unsigned,unsigned>(2272,1278)));
    std::puts("runtime-controls: coherent states, 16:9 sizes and real-frame pacing passed");
}
