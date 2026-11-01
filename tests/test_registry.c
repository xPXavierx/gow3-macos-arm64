/* SPDX-License-Identifier: GPL-2.0-or-later
 * Use the real exported operations while old thread records and newer locks accumulate. */
#define _GNU_SOURCE
#include "runtime.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void runtime_restart(void) { abort(); }
typedef void *(ABI *Self)(void);
typedef int32_t (ABI *GetPrio)(void *,int *);
typedef int32_t (ABI *Init)(void **,void **,const char *);
typedef int32_t (ABI *HandleOp)(void **);
typedef int32_t (ABI *SemCreate)(uint32_t *,const char *,uint32_t,int32_t,int32_t,const void *);
typedef int32_t (ABI *SemPoll)(uint32_t,int32_t);
typedef int32_t (ABI *SemDelete)(uint32_t);
#define GET(type,nid) ((type)runtime_resolve(nid,0))
enum { RECORDS=64, HOT=4, ITERATIONS=12000 };
static void *locks[RECORDS], *records[RECORDS];
static uint32_t sems[RECORDS];
static Init init;
static HandleOp write_lock,unlock,destroy;
static GetPrio get_prio;
static SemCreate sem_create;
static SemPoll sem_poll,sem_signal;
static SemDelete sem_delete;

static void *attach_record(void *slot) {
    *(void **)slot=GET(Self,"aI+OeCz8xrQ#libkernel")();
    return NULL;
}
static void exercise(unsigned i) {
    assert(!write_lock(&locks[i]));
    assert(!unlock(&locks[i]));
    assert(!sem_poll(sems[i],1));
    assert(!sem_signal(sems[i],1));
    int priority=0;
    assert(!get_prio(records[i],&priority) && priority==700);
}
static void *worker(void *arg) {
    unsigned i=(unsigned)(uintptr_t)arg;
    for (unsigned n=0;n<2000;++n) exercise(i);
    return NULL;
}
int main(int argc,char **argv) {
    assert(argc==2 && (!strcmp(argv[1],"0") || !strcmp(argv[1],"1")));
    assert(!setenv("GOW3_REGISTRY_DIAG","1",1));
    for (unsigned i=0;i<3;++i) {
        const char *options[]={"GOW3_RWLOCK_FRONT","GOW3_THREAD_FRONT","GOW3_SEMA_FRONT"};
        assert(!setenv(options[i],argv[1],1));
    }
    runtime_start(1);
    init=GET(Init,"6ULAa0fq4jA#libkernel");
    write_lock=GET(HandleOp,"mqdNorrB+gI#libkernel");
    unlock=GET(HandleOp,"+L98PIbGttk#libkernel");
    destroy=GET(HandleOp,"BB+kb08Tl9A#libkernel");
    get_prio=GET(GetPrio,"1tKyG7RlMJo#libkernel");
    sem_create=GET(SemCreate,"188x57JYp0g#libkernel");
    sem_poll=GET(SemPoll,"12wOHk8ywb0#libkernel");
    sem_signal=GET(SemPoll,"4czppHBiriw#libkernel");
    sem_delete=GET(SemDelete,"R1Jvn8bSCW8#libkernel");
    assert(init); assert(write_lock); assert(unlock); assert(destroy); assert(get_prio);
    assert(sem_create); assert(sem_poll); assert(sem_signal); assert(sem_delete);
    for (unsigned i=0;i<RECORDS;++i) {
        assert(!init(&locks[i],NULL,"registry-test"));
        assert(!sem_create(&sems[i],"registry-test",1,1,1,NULL));
        pthread_t thread;
        assert(!pthread_create(&thread,NULL,attach_record,&records[i]));
        assert(!pthread_join(thread,NULL));
    }
    for (unsigned n=0;n<ITERATIONS;++n) exercise(n%HOT);
    // Snapshots of a distant semaphore must not perturb production lookup order.
    assert(!sem_poll(sems[0],1) && !sem_signal(sems[0],1));
    runtime_sema_report();
    for (unsigned n=0;n<32;++n) assert(!runtime_sema_waiters(sems[RECORDS-1]));
    assert(!sem_poll(sems[0],1) && !sem_signal(sems[0],1));
    runtime_sema_report();
    pthread_t threads[HOT];
    for (unsigned i=0;i<HOT;++i) assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));
    // Concurrent creation/destruction and hot lookups must preserve every handle.
    for (unsigned i=0;i<100;++i) {
        void *temporary=NULL; uint32_t sem;
        assert(!init(&temporary,NULL,"churn"));
        assert(!sem_create(&sem,"churn",1,1,1,NULL));
        assert(!destroy(&temporary)); assert(!sem_delete(sem));
    }
    for (unsigned i=0;i<HOT;++i) assert(!pthread_join(threads[i],NULL));
    assert((uint32_t)write_lock(&(void *){(void *)(uintptr_t)1})==0x80020016);
    int priority;
    assert((uint32_t)get_prio((void *)(uintptr_t)1,&priority)==0x80020003);
    assert((uint32_t)sem_poll(UINT32_MAX,1)==0x80020003);
    for (unsigned i=0;i<RECORDS;++i) {
        exercise(i);
        assert(!write_lock(&locks[i]));
        assert((uint32_t)destroy(&locks[i])==0x80020010);
        assert(!unlock(&locks[i]) && !destroy(&locks[i]));
        assert(!sem_delete(sems[i]));
        assert((uint32_t)write_lock(&locks[i])==0x80020016);
    }
    runtime_rwlock_report(); runtime_sema_report(); runtime_thread_report();
    puts("PASS: retained thread handles, hot locks/semaphores, concurrent churn and invalid handles");
}
