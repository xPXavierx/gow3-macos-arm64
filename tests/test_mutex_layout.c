/* SPDX-License-Identifier: GPL-2.0-or-later
 * Guest libc writes offset 0x20 of a mutex handle. This must not change the
 * Darwin pthread object, including while a condition wait drops/reacquires it.
 * Layout fix: SpartanPort 9e8a080bb757. Exercises the actual low-address allocator.
 */
#include "../src/runtime_mutex.c"
#include <assert.h>

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) {
    (void)table; (void)count; (void)nid; return 0;
}

static GuestMutex *shared;
static GuestCond *condition;
static int signaled;

static void *signal_condition(void *unused) {
    (void)unused;
    assert(mutex_lock(&shared) == 0);
    signaled = 1;
    assert(cond_signal(&condition) == 0);
    assert(mutex_unlock(&shared) == 0);
    return NULL;
}

static void layout_and_conditions(void) {
    assert(mutex_init(&shared, NULL, "guest-libc-layout") == 0);
    assert((uintptr_t)shared < (UINT64_C(1) << 40));
    assert(offsetof(GuestMutex, native) >= 0x24);
    uint32_t *flags = (uint32_t *)((unsigned char *)shared + 0x20);
    assert(*flags == 0);
    *flags |= 0x100;
    assert(mutex_lock(&shared) == 0);
    assert((uint32_t)mutex_destroy(&shared) == UINT32_C(0x80020010));
    assert(*flags == 0x100);
    assert(cond_init(&condition, NULL, "layout-cond") == 0);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, signal_condition, NULL) == 0);
    while (!signaled) assert(cond_wait(&condition, &shared) == 0);
    assert(*flags == 0x100);
    assert(mutex_unlock(&shared) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(cond_destroy(&condition) == 0);
    assert(mutex_destroy(&shared) == 0);
    assert((uint32_t)mutex_lock(&shared) == UINT32_C(0x80020016));
}

static void recursive_and_timed(void) {
    GuestAttr *attr = NULL;
    GuestMutex *mutex = NULL;
    assert(attr_init(&attr) == 0 && attr_type(&attr, 2) == 0);
    assert(mutex_init(&mutex, &attr, "recursive-layout") == 0);
    assert(attr_destroy(&attr) == 0);
    uint32_t *flags = (uint32_t *)((unsigned char *)mutex + 0x20);
    *flags = 0x100;
    assert(mutex_lock(&mutex) == 0);
    assert(mutex_trylock(&mutex) == 0);
    assert(mutex_timedlock(&mutex, 1000) == 0);
    assert((uint32_t)mutex_destroy(&mutex) == UINT32_C(0x80020010));
    assert(*flags == 0x100);
    assert(mutex_unlock(&mutex) == 0 && mutex_unlock(&mutex) == 0);
    assert(mutex_unlock(&mutex) == 0 && mutex_destroy(&mutex) == 0);
}

static void normal_mutex_busy(void) {
    GuestAttr *attr = NULL;
    GuestMutex *mutex = NULL;
    assert(attr_init(&attr) == 0 && attr_type(&attr, 3) == 0);
    assert(mutex_init(&mutex, &attr, "normal-busy") == 0 && attr_destroy(&attr) == 0);
    assert(mutex_lock(&mutex) == 0);
    assert((uint32_t)mutex_trylock(&mutex) == UINT32_C(0x80020010));
    assert((uint32_t)mutex_timedlock(&mutex, 1000) == UINT32_C(0x8002003c));
    assert((uint32_t)mutex_destroy(&mutex) == UINT32_C(0x80020010));
    assert(mutex_unlock(&mutex) == 0 && mutex_destroy(&mutex) == 0);
}

int main(void) {
    layout_and_conditions();
    recursive_and_timed();
    normal_mutex_busy();
    puts("PASS: guest flags, Darwin low pointers, mutex/condition lifecycle and recursive timed locks");
    return 0;
}
