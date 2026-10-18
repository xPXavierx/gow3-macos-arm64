/* SPDX-License-Identifier: GPL-2.0-or-later
 * Architectural invariants independent of interp_x87.c. Synthetic code only.
 * The first nine snippets in verify_aot.py form this control-environment suite.
 */
#include "recomp/rc.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>

static int exception_guard;
static void pending_exception(BbCpu *cpu, uint64_t rsp) {
    assert(rsp == cpu->r[RSP]);
    ++exception_guard;
}
static uint16_t word(const unsigned char *memory) {
    return (uint16_t)(memory[0] | ((unsigned)memory[1] << 8));
}
int main(int argc, char **argv) {
    assert(argc == 2);
    void *lib = dlopen(argv[1], RTLD_NOW); assert(lib);
    RcInit init = (RcInit)dlsym(lib, BB_RECOMP_INIT); assert(init);
    const RcApi api = {.version=BB_RECOMP_API_VERSION, .image_base=0x30000000000ull,
                       .bail=pending_exception};
    size_t count = 0;
    const RcFunction *functions = init(&api, &count); assert(count >= 9);
    for (int i=0; i<9; ++i) assert(functions[i].offset == (unsigned)i+1);
    for (int small=0; small<2; ++small) {
        for (int top=0; top<8; ++top) {
            unsigned char memory[64]; memset(memory,0xa5,sizeof(memory));
            BbCpu cpu = {0}; cpu.r[RBX]=(uintptr_t)(memory+8);
            cpu.r[RBP]=(uintptr_t)(memory+8+40);
            cpu.fcw=0x1240; cpu.fsw=0xd3a5; cpu.top=top; cpu.flags=0x202;
            cpu.st[top]=3.25;
            functions[small ? 2 : 0].fn(&cpu);
            const int size=small ? 14 : 28, offset=small ? 2 : 4;
            assert(word(memory+8)==0x1240 && cpu.fcw==0x127f);
            assert(word(memory+8+offset)==(uint16_t)((0xd3a5&~0x3800)|(top<<11)));
            assert(cpu.top==top && cpu.st[top]==3.25 && cpu.flags==0x202);
            for(int i=0;i<8;++i) assert(memory[i]==0xa5);
            for(int i=8+size;i<64;++i) assert(memory[i]==0xa5);
            cpu.fcw=0x37f; cpu.fsw=0; cpu.top=0;
            functions[small ? 4 : 3].fn(&cpu);
            assert(cpu.fcw==0x1240 && cpu.fsw==(0xd3a5&~0x3800) && cpu.top==top);
            if(!small) {
                memset(memory,0xa5,sizeof(memory)); cpu.fcw=0x1240;
                functions[1].fn(&cpu); /* exact RBP-40 form reported at runtime */
                assert(word(memory+8)==0x1240 && cpu.fcw==0x127f);
                for(int i=0;i<8;++i) assert(memory[i]==0xa5);
                for(int i=36;i<64;++i) assert(memory[i]==0xa5);
            }
        }
    }
    BbCpu cpu={0}; cpu.fcw=0x1240; cpu.fsw=0xffff; cpu.top=7;
    functions[5].fn(&cpu); assert(cpu.fsw==0x7f00 && cpu.fcw==0x1240 && cpu.top==7);
    functions[6].fn(&cpu); assert(cpu.fsw==0 && cpu.fcw==0x37f && cpu.top==0);
    cpu.fsw=0x30; cpu.flags=0x202;
    functions[7].fn(&cpu); assert(exception_guard==0 && cpu.flags==0x202);
    cpu.fcw&=~0x30; functions[7].fn(&cpu);
    assert(exception_guard==1 && cpu.rip==api.image_base); /* never swallow #MF */
    functions[8].fn(&cpu); assert(cpu.fsw==0x30 && exception_guard==1);
    puts("x87 environment: 14/28-byte boundaries, save/mask/restore, TOP and pending-exception guard passed");
    return 0;
}
