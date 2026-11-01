// SPDX-License-Identifier: GPL-2.0-or-later
// Real PM4 decoding -> production fence writer; no Vulkan device required.
#include <cassert>
#include <cstdio>
#include "video_core/amdgpu/fence_write.h"
#include "video_core/amdgpu/pm4_cmds.h"

int main(int argc,char**) {
    if (argc>1) {
        AmdGpu::WriteFenceLabel(nullptr,0,3,[](void*,const void*,u32) { return false; });
        return 99;
    }
    using namespace AmdGpu;
    unsigned cases=0;
    for (bool backing : {false,true}) for (bool wide : {false,true}) {
        u64 guest=0, physical=0;
        const u64 value=wide ? 0xa1b2c3d4e5f60718ull : 0xe5f60718;
        const u32 bytes=wide ? 8 : 4;
        unsigned attempts=0, irqs=0;
        auto write=[&](void* dst,u64 data,u32 size) {
            assert(dst==&guest && size==bytes);
            WriteFenceLabel(dst,data,size,[&](void* p,const void* src,u32 n) {
                assert(p==&guest && n==bytes); ++attempts;
                if (backing) runtime_store_once(&physical,src,n);
                return backing;
            });
        };
        auto irq=[&] {
            assert(attempts==1 && (backing ? physical : guest)==value);
            assert((backing ? guest : physical)==0);
            ++irqs;
        };
        PM4CmdEventWriteEop eop{.header={PM4ItOpcode::EventWriteEop, sizeof(eop)/4-2}};
        const auto address=reinterpret_cast<uintptr_t>(&guest);
        eop.address_lo=u32(address); eop.address_hi.Assign(u32(address>>32));
        eop.data_sel.Assign(wide ? DataSelect::Data64 : DataSelect::Data32Low);
        eop.data_lo=u32(value); eop.data_hi=u32(value>>32);
        eop.int_sel.Assign(InterruptSelect::IrqWhenWriteConfirm);
        eop.SignalFence(write,irq);
        assert(attempts==1 && irqs==1); ++cases;

        guest=physical=0; attempts=irqs=0;
        PM4CmdReleaseMem release{.header={PM4ItOpcode::ReleaseMem, sizeof(release)/4-2}};
        release.address_lo=u32(address); release.address_hi=u32(address>>32);
        release.data_sel.Assign(wide ? DataSelect::Data64 : DataSelect::Data32Low);
        release.data_lo=u32(value); release.data_hi=u32(value>>32);
        release.int_sel.Assign(InterruptSelect::IrqWhenWriteConfirm);
        release.SignalFence(irq,[](VAddr,u16,u16) { assert(false); },write);
        assert(attempts==1 && irqs==1); ++cases;
        if (!wide) {
            guest=physical=0; attempts=0;
            PM4CmdEventWriteEos eos{.header={PM4ItOpcode::EventWriteEos, sizeof(eos)/4-2}};
            eos.address_lo=u32(address); eos.address_hi.Assign(u32(address>>32));
            eos.command.Assign(PM4CmdEventWriteEos::Command::SignalFence);
            eos.data=u32(value);
            eos.SignalFence(write);
            assert(attempts==1 && (backing ? physical : guest)==value); ++cases;
        }
    }
    // No-data EOP and GDS release preserve their dispatch/interrupt semantics.
    PM4CmdEventWriteEop empty{.header={PM4ItOpcode::EventWriteEop, sizeof(empty)/4-2}};
    empty.data_sel.Assign(DataSelect::None); empty.int_sel.Assign(InterruptSelect::None);
    empty.SignalFence([](void*,u64,u32) { assert(false); },[] { assert(false); });
    PM4CmdReleaseMem gds{.header={PM4ItOpcode::ReleaseMem, sizeof(gds)/4-2}};
    gds.data_sel.Assign(DataSelect::GdsMemStore); gds.int_sel.Assign(InterruptSelect::IrqWhenWriteConfirm);
    gds.address_lo=0x1000; gds.gds_index=7; gds.num_dw=9;
    bool copied=false, signaled=false;
    gds.SignalFence([&] { assert(copied); signaled=true; },
        [&](VAddr dst,u16 offset,u16 words) {
            assert(dst==0x1000 && offset==7 && words==9); copied=true;
        },[](void*,u64,u32) { assert(false); });
    assert(copied && signaled);
    std::printf("PASS: %u EOP/EOS/RELEASE_MEM backing/fallback widths; write-before-IRQ; no-data/GDS dispatch\n",cases);
}
