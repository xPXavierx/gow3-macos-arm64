/* Native dispatch bridge, using bloodborne-native/bloodborne-recomp (GPL-2.0+).
 * No direct cast of a guest code address to an ARM64 function pointer. */
#ifdef GOW3_ARM64
#include "runtime.h"
#include <stdlib.h>
#include <stdio.h>
uint64_t bb_image_base;
uint64_t runtime_guest_call(const void *fn,int count,const uint64_t *args) {
    if (count<0 || count>6 || (count && !args)) abort();
    if (bbcpu_is_guest((uintptr_t)fn)) return bbcpu_call((uintptr_t)fn,count,args);
    uint64_t a[6]={0};
    for (int i=0;i<count;++i) a[i]=args[i];
    typedef uint64_t (*Host)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
    return ((Host)fn)(a[0],a[1],a[2],a[3],a[4],a[5]);
}
static uintptr_t trap_base;
static size_t trap_count;
static void (*trap_handler)(uint32_t,uintptr_t);
static void unresolved_native(uintptr_t argument) {
    uintptr_t address=bbcpu_guest_rip();
    if (address<trap_base || (address-trap_base)%32 || (address-trap_base)/32>=trap_count) abort();
    trap_handler((uint32_t)((address-trap_base)/32),argument);
    abort();
}
void runtime_arm64_unresolved_traps(uintptr_t base,size_t count,void (*handler)(uint32_t,uintptr_t)) {
    trap_base=base; trap_count=count; trap_handler=handler;
    for (size_t i=0;i<count;++i) bbcpu_set_native(base+32*i,(void *)unresolved_native);
}
#endif
