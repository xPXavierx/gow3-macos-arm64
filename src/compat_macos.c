/* Experimental Darwin x86-64 backend, also used under Rosetta. */
#define GOW3_MACOS_ALLOC_IMPL
#include "compat_macos.h"
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

/* Separate from runtime_low_map's image/stack range. Mach fixed allocations
 * fail on occupied addresses; unlike MAP_FIXED, they cannot replace host data.
 * Freed blocks are recycled, so startup and steady-state allocations do not
 * consume monotonically increasing virtual address space. */
typedef struct Header {
    size_t mapped, requested;
    struct Header *next;
    uint64_t magic;
    unsigned char padding[32];
} Header;
_Static_assert(sizeof(Header)==64,"aligned guest heap header");
#define HEAP_MAGIC UINT64_C(0x474f57334d414348)
static pthread_mutex_t heap_lock=PTHREAD_MUTEX_INITIALIZER;
#ifdef GOW3_ARM64
static mach_vm_address_t heap_next=UINT64_C(0xfc00000000);
#define HEAP_END UINT64_C(0xff00000000)
#else
static mach_vm_address_t heap_next=UINT64_C(0x0200000000);
#define HEAP_END UINT64_C(0x0800000000)
#endif
static Header *free_blocks;

void *gow3_macos_malloc(size_t size) {
    const size_t page=16384;
    if (size>SIZE_MAX-sizeof(Header)-page) { errno=ENOMEM; return NULL; }
    size_t bytes=(size+sizeof(Header)+page-1)&~(page-1);
    pthread_mutex_lock(&heap_lock);
    Header **link=&free_blocks, *h=NULL;
    while (*link) {
        if ((*link)->mapped>=bytes) { h=*link; *link=h->next; break; }
        link=&(*link)->next;
    }
    while (!h && heap_next+bytes<=HEAP_END) {
        mach_vm_address_t address=heap_next;
        heap_next+=bytes;
        if (mach_vm_allocate(mach_task_self(),&address,bytes,VM_FLAGS_FIXED)==KERN_SUCCESS) {
            h=(Header *)(uintptr_t)address; h->mapped=bytes;
        }
    }
    if (h) { h->requested=size; h->magic=HEAP_MAGIC; h->next=NULL; }
    pthread_mutex_unlock(&heap_lock);
    if (!h) { errno=ENOMEM; return NULL; }
    return h+1;
}
void gow3_macos_free(void *ptr) {
    if (!ptr) return;
    Header *h=(Header *)ptr-1;
    if (h->magic!=HEAP_MAGIC) { fputs("STOP: invalid Darwin runtime heap pointer\n",stderr); abort(); }
    pthread_mutex_lock(&heap_lock);
    h->magic=0; h->next=free_blocks; free_blocks=h;
    pthread_mutex_unlock(&heap_lock);
}
void *gow3_macos_calloc(size_t count,size_t size) {
    if (size && count>SIZE_MAX/size) { errno=ENOMEM; return NULL; }
    void *p=gow3_macos_malloc(count*size);
    if (p) memset(p,0,count*size);
    return p;
}
void *gow3_macos_realloc(void *ptr,size_t size) {
    if (!ptr) return gow3_macos_malloc(size);
    if (!size) { gow3_macos_free(ptr); return NULL; }
    Header *h=(Header *)ptr-1;
    if (h->magic!=HEAP_MAGIC) abort();
    if (size<=h->mapped-sizeof(*h)) { h->requested=size; return ptr; }
    void *p=gow3_macos_malloc(size);
    if (p) { memcpy(p,ptr,h->requested); gow3_macos_free(ptr); }
    return p;
}
/* Darwin lacks POSIX timed mutex/rwlock acquisition. Preserve realtime absolute
 * deadlines; brief sleeps avoid busy spinning. Try first, even for past deadlines. */
static int pause_until(const struct timespec *end) {
    struct timespec now;
    if (end->tv_nsec<0 || end->tv_nsec>=1000000000) return EINVAL;
    if (clock_gettime(CLOCK_REALTIME,&now)) return errno;
    int64_t left=(end->tv_sec-now.tv_sec)*INT64_C(1000000000)+end->tv_nsec-now.tv_nsec;
    if (left<=0) return ETIMEDOUT;
    struct timespec sleep={0,(long)(left<1000000 ? left : 1000000)};
    nanosleep(&sleep,NULL);
    return 0;
}
#define TIMED_LOCK(name,type,attempt) \
int name(type *lock,const struct timespec *end) { \
    for (;;) { int e=attempt(lock); if (e!=EBUSY) return e; \
        e=pause_until(end); if (e) return e; } \
}
TIMED_LOCK(gow3_macos_mutex_timedlock,pthread_mutex_t,pthread_mutex_trylock)
TIMED_LOCK(gow3_macos_rwlock_timedrdlock,pthread_rwlock_t,pthread_rwlock_tryrdlock)
TIMED_LOCK(gow3_macos_rwlock_timedwrlock,pthread_rwlock_t,pthread_rwlock_trywrlock)
