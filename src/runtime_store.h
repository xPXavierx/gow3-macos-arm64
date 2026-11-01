/* SPDX-License-Identifier: GPL-2.0-or-later
 * Adapted from SpartanPort 6f5aa50c764047d56d5fec622049218d67615013.
 * Shared by the native backing writer and GPU fence fallback. */
#ifndef GOW3_RUNTIME_STORE_H
#define GOW3_RUNTIME_STORE_H
#include <stdint.h>
#include <string.h>

/* Small completion writes must not be libc byte loops/overlapping stores.
 * Naturally aligned 32/64-bit labels publish once with release ordering.
 * Other small ranges use nonoverlapping aligned chunks, not whole-range atomicity.
 * Large resource transfers retain memcpy. Source bytes may be unaligned. */
static inline void runtime_store_once(void *destination,const void *source,uint64_t size) {
    unsigned char *dst=(unsigned char *)destination;
    const unsigned char *src=(const unsigned char *)source;
    if (size>64) { memcpy(dst,src,size); return; }
    while (size) {
        uintptr_t at=(uintptr_t)dst;
        if (size>=8 && !(at&7)) {
            uint64_t value; memcpy(&value,src,8);
            __atomic_store_n((uint64_t *)dst,value,__ATOMIC_RELEASE);
            dst+=8; src+=8; size-=8;
        } else if (size>=4 && !(at&3)) {
            uint32_t value; memcpy(&value,src,4);
            __atomic_store_n((uint32_t *)dst,value,__ATOMIC_RELEASE);
            dst+=4; src+=4; size-=4;
        } else if (size>=2 && !(at&1)) {
            uint16_t value; memcpy(&value,src,2);
            __atomic_store_n((uint16_t *)dst,value,__ATOMIC_RELEASE);
            dst+=2; src+=2; size-=2;
        } else {
            __atomic_store_n(dst,*src,__ATOMIC_RELEASE);
            ++dst; ++src; --size;
        }
    }
}
#endif
