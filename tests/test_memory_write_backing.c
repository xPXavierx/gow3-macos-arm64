/* SPDX-License-Identifier: GPL-2.0-or-later
 * Production memory manager: protected views, aliases and fence recycling. */
#include "../src/runtime_memory.c"
#include <assert.h>
#include <sched.h>

int32_t *runtime_errno(void) { abort(); }
uintptr_t runtime_lookup(const RuntimeExport *t,size_t n,const char *s) {
    (void)t; (void)n; (void)s; abort();
}
static uintptr_t base;
static void fixture(void) {
    base=(uintptr_t)runtime_low_map(5*PAGE,PROT_NONE);
    assert(base);
    backing_base=malloc(5*PAGE);
    assert(backing_base);
    memset(backing_base,0xcc,5*PAGE);
    write_lock();
    assert(!vma_insert(0,(Vma){base,base+PAGE,KIND_DIRECT,3,0,PAGE}));
    assert(!vma_insert(1,(Vma){base+PAGE,base+2*PAGE,KIND_FLEXIBLE,3,0,3*PAGE}));
    assert(!vma_insert(2,(Vma){base+2*PAGE,base+3*PAGE,KIND_RESERVED,0,0,0}));
    assert(!vma_insert(3,(Vma){base+4*PAGE,base+5*PAGE,KIND_DIRECT,3,0,PAGE}));
    write_unlock();
}
static void ranges(void) {
    unsigned char data[256], out[256];
    for (size_t i=0;i<sizeof(data);++i) data[i]=(unsigned char)(i*17+3);
    for (unsigned offset=0;offset<16;++offset) for (unsigned src=0;src<2;++src)
        for (unsigned size=0;size<=128;++size) {
            memset(backing_base+PAGE,0xcc,256);
            assert(runtime_memory_write_backing(base+offset,data+src,size));
            assert(!memcmp(backing_base+PAGE+offset,data+src,size));
            assert(backing_base[PAGE+offset+size]==0xcc);
            if (offset) assert(backing_base[PAGE+offset-1]==0xcc);
            runtime_memory_read_backing(base+4*PAGE+offset,out,size);
            assert(!memcmp(out,data+src,size));
        }
    // An adjacent virtual range may use a completely different physical offset.
    unsigned char *large=malloc(2*PAGE);
    assert(large);
    memset(large,0x31,PAGE); memset(large+PAGE,0x72,PAGE);
    assert(runtime_memory_write_backing(base,large,2*PAGE));
    assert(!memcmp(backing_base+PAGE,large,PAGE));
    assert(!memcmp(backing_base+3*PAGE,large+PAGE,PAGE));
    assert(runtime_memory_write_backing(base+PAGE-4,data,16));
    assert(!memcmp(backing_base+2*PAGE-4,data,4));
    assert(!memcmp(backing_base+3*PAGE,data+4,12));
    free(large);
    // No prefix may be published before a failed backing attempt falls back.
    memset(backing_base,0x9a,5*PAGE);
    assert(!runtime_memory_write_backing(base+2*PAGE-4,data,8)); // reserved tail
    assert(!runtime_memory_write_backing(base+5*PAGE-4,data,8)); // unmapped tail
    assert(!runtime_memory_write_backing(base+3*PAGE,data,8));   // sparse gap
    assert(!runtime_memory_write_backing(base+2*PAGE,data,8));   // reservation
    assert(!runtime_memory_write_backing(UINTPTR_MAX-3,data,8));
    for (size_t i=0;i<5*PAGE;++i) assert(backing_base[i]==0x9a);
    assert(runtime_memory_write_backing(UINTPTR_MAX,NULL,0));
    runtime_store_once(NULL,NULL,0);
}

enum { ITERATIONS=30000 };
static struct {
    unsigned width;
    uint64_t ack, payload[8];
} message;
static uint64_t recycled(void) {
    return message.width==4 ? UINT32_C(0xdeadbeef) : UINT64_C(0x76543210deadbeef);
}
static uint64_t tag(unsigned i) {
    return message.width==4 ? UINT32_C(0xa5b60000)+i : UINT64_C(0xa5b61f2e00000000)+i;
}
static uint64_t label(void) {
    void *p=backing_base+PAGE+64;
    return message.width==4 ? __atomic_load_n((uint32_t *)p,__ATOMIC_ACQUIRE)
                            : __atomic_load_n((uint64_t *)p,__ATOMIC_ACQUIRE);
}
static void recycle(void) {
    void *p=backing_base+PAGE+64;
    if (message.width==4) __atomic_store_n((uint32_t *)p,(uint32_t)recycled(),__ATOMIC_RELEASE);
    else __atomic_store_n((uint64_t *)p,recycled(),__ATOMIC_RELEASE);
}
static void *producer(void *unused) {
    (void)unused;
    for (unsigned i=1;i<=ITERATIONS;++i) {
        while (__atomic_load_n(&message.ack,__ATOMIC_ACQUIRE)!=i-1) sched_yield();
        for (unsigned k=0;k<8;++k) message.payload[k]=((uint64_t)i<<32)|k;
        uint64_t value=tag(i);
        assert(runtime_memory_write_backing(base+64,&value,message.width));
        while (__atomic_load_n(&message.ack,__ATOMIC_ACQUIRE)!=i) sched_yield();
        // The consumer may immediately reuse the object after observing completion.
        assert(label()==recycled());
    }
    return NULL;
}
static void publication_and_recycling(unsigned width) {
    message.width=width; message.ack=0; recycle();
    pthread_t writer;
    assert(!pthread_create(&writer,NULL,producer,NULL));
    for (unsigned i=1;i<=ITERATIONS;++i) {
        uint64_t observed;
        do {
            observed=label();
            assert(observed==recycled() || observed==tag(i)); // no torn label
        } while (observed!=tag(i));
        for (unsigned k=0;k<8;++k) assert(message.payload[k]==(((uint64_t)i<<32)|k));
        recycle();
        __atomic_store_n(&message.ack,i,__ATOMIC_RELEASE);
    }
    assert(!pthread_join(writer,NULL));
}
int main(void) {
    fixture(); ranges();
    publication_and_recycling(4); publication_and_recycling(8);
    puts("PASS: protected backing/aliases, all alignments 0..128, cross-VMA bulk, failure without prefix, 60000 release publications/recycles");
}
