/* Test-only reusable barrier: Darwin does not provide pthread_barrier_t. */
#ifndef GOW3_TEST_MACOS_BARRIER_H
#define GOW3_TEST_MACOS_BARRIER_H
#include <pthread.h>
#include <errno.h>
#define PTHREAD_BARRIER_SERIAL_THREAD 1
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned count, waiting, generation;
} pthread_barrier_t;
static int pthread_barrier_init(pthread_barrier_t *b, const void *attr, unsigned count) {
    if (attr || !count) return EINVAL;
    int e=pthread_mutex_init(&b->mutex,NULL);
    if (e) return e;
    e=pthread_cond_init(&b->cond,NULL);
    if (e) { pthread_mutex_destroy(&b->mutex); return e; }
    b->count=count; b->waiting=0; b->generation=0;
    return 0;
}
static int pthread_barrier_wait(pthread_barrier_t *b) {
    int e=pthread_mutex_lock(&b->mutex);
    if (e) return e;
    unsigned generation=b->generation;
    if (++b->waiting==b->count) {
        b->waiting=0; ++b->generation;
        e=pthread_cond_broadcast(&b->cond);
        pthread_mutex_unlock(&b->mutex);
        return e ? e : PTHREAD_BARRIER_SERIAL_THREAD;
    }
    while (generation==b->generation && !e) e=pthread_cond_wait(&b->cond,&b->mutex);
    pthread_mutex_unlock(&b->mutex);
    return e;
}
static int pthread_barrier_destroy(pthread_barrier_t *b) {
    int e=pthread_cond_destroy(&b->cond);
    return e ? e : pthread_mutex_destroy(&b->mutex);
}
#endif
