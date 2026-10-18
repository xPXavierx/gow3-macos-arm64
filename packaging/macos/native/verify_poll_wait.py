"""Synthetic polling CFGs, concurrent wakeups and negative recognition cases.

All input instructions are hand-authored. The generated functions have no CPU
fallback callbacks. A missed wakeup, changed register, flag result or memory byte
fails the test. CPU consumption is reported separately from correctness.
"""
from pathlib import Path
import os, struct, subprocess, sys

generator, source, include, compiler, directory = sys.argv[1:]
out = Path(directory); out.mkdir(parents=True, exist_ok=True)
positive = {
    'byte': '803f0075fb0f94c0c3',
    'word': '66833f0075fa0f94c0c3',
    'dword': '833f0075fb0f94c0c3',
    'qword': '48833f0075fa0f94c0c3',
    'nop': '909090833f0075f80f94c0c3',
    'pause': 'f390833f0075f90f94c0c3',
    'test': '853775fc0f94c0c3',
}
negative = {
    'counter': 'ffc975fcc3',
    'store': '8907833f0075f9c3',
    'two-comparisons': '833f00833f0075f8c3',
    'changed-address': '4883c704833f0075f7c3',
    'call': 'e800000000833f0075f6c3',
}
table=[]; image=bytearray()
for name, encoding in {**positive, **negative}.items():
    offset=len(image); code=bytes.fromhex(encoding)
    table.append((name,offset,len(code)))
    image.extend(code); image.extend(b'\x90'*(-len(image)%64))
image=bytes(image).ljust(16384,b'\0')
eh=struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01'.ljust(16,b'\0'),3,62,1,0,64,0,0,64,56,1,0,0,0)
ph=struct.pack('<II6Q',1,5,16384,0,0,len(image),len(image),16384)
(out/'fixture.elf').write_bytes((eh+ph).ljust(16384,b'\0')+image)
(out/'functions.tsv').write_text('offset\tsize\n'+''.join(f'{o:#x}\t{n}\n' for _,o,n in table))
subprocess.run([generator,str(out/'fixture.elf'),str(out/'functions.tsv'),str(out/'recomp.c'),*[hex(o) for _,o,_ in table]],check=True,env=dict(os.environ,BB_JIT='0'))
generated=(out/'recomp_00000.c').read_text()
for name,offset,_ in table:
    body=generated.split(f'void f_{offset:x}(BbCpu *c) {{',1)[1].split('\n}\n',1)[0]
    assert ('rc_poll_wait(&poll_spins)' in body)==(name in positive), name
assert 'rc->step(' not in generated
library=out/'libpoll.dylib'
subprocess.run([compiler,'-arch','arm64','-O2','-dynamiclib','-I'+str(Path(source)/'src'),'-I'+include,*map(str,out.glob('recomp*.c')),'-o',str(library)],check=True)
harness=out/'wake.c'
harness.write_text(r'''
#include "recomp/rc.h"
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
static _Atomic uint64_t value;
static _Atomic unsigned ready;
static double now(clockid_t clock) { struct timespec t; assert(!clock_gettime(clock,&t)); return t.tv_sec+t.tv_nsec*1e-9; }
static void *release(void *p) {
    (void)p; while(!atomic_load(&ready)) {}
    const struct timespec delay={0,2000000}; nanosleep(&delay,NULL);
    atomic_store(&value,0); return NULL;
}
int main(int argc, char **argv) {
    assert(argc==2); void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); assert(lib);
    RcInit init=(RcInit)dlsym(lib,BB_RECOMP_INIT); assert(init);
    const RcApi api={.version=BB_RECOMP_API_VERSION}; size_t count=0;
    const RcFunction *table=init(&api,&count); assert(count>=7);
    double cpu=0,wall=0;
    for(unsigned f=0;f<7;++f) for(unsigned k=0;k<16;++k) {
        BbCpu c={0}; uint64_t stack[64]={0}, before[16];
        for(unsigned r=0;r<16;++r)c.r[r]=0x1234567800000000ull+r*0x100;
        stack[16]=0x12345678; c.r[RSP]=(uint64_t)&stack[16];
        c.r[RDI]=(uint64_t)&value; c.r[RSI]=~0ull;
        memcpy(before,c.r,sizeof(before)); c.flags=0x202; c.mxcsr=0x1f80;
        atomic_store(&value,1); atomic_store(&ready,0); pthread_t thread;
        assert(!pthread_create(&thread,NULL,release,NULL)); errno=EDOM;
        double w=now(CLOCK_MONOTONIC),u=now(CLOCK_THREAD_CPUTIME_ID);
        atomic_store(&ready,1); table[f].fn(&c);
        cpu+=now(CLOCK_THREAD_CPUTIME_ID)-u; wall+=now(CLOCK_MONOTONIC)-w;
        assert(errno==EDOM); pthread_join(thread,NULL);
        assert(c.rip==stack[16]); assert(c.r[RSP]==before[RSP]+8);
        for(unsigned r=0;r<16;++r) if(r!=RSP&&r!=RAX)assert(c.r[r]==before[r]);
        assert(c.r[RAX]==((before[RAX]&~255ull)|1));
        assert(atomic_load(&value)==0);
        for(unsigned j=0;j<64;++j)assert(stack[j]==(j==16?0x12345678:0));
    }
    printf("PASS: 112 concurrent wakeups; CPU %.6f s / wall %.6f s (%.2f%%)\n",cpu,wall,100*cpu/wall);
}
''')
executable=out/'wake'
subprocess.run([compiler,'-arch','arm64','-O2','-I'+str(Path(source)/'src'),'-I'+include,str(harness),'-o',str(executable)],check=True)
for enabled in ('0','1'):
    subprocess.run([str(executable),str(library)],check=True,env=dict(os.environ,BB_JIT='0',BB_RECOMP_POLL_WAIT=enabled),timeout=15)
print('PASS: 7 polling families, 5 excluded side-effect/control-flow families')
