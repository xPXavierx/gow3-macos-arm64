#ifndef GOW3_COMPAT_MACOS_H
#define GOW3_COMPAT_MACOS_H
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#ifndef CLOCK_REALTIME_COARSE
#define CLOCK_REALTIME_COARSE CLOCK_REALTIME
#endif
void *gow3_macos_malloc(size_t size);
void *gow3_macos_calloc(size_t count, size_t size);
void *gow3_macos_realloc(void *ptr, size_t size);
void gow3_macos_free(void *ptr);
int gow3_macos_mutex_timedlock(pthread_mutex_t *, const struct timespec *);
int gow3_macos_rwlock_timedrdlock(pthread_rwlock_t *, const struct timespec *);
int gow3_macos_rwlock_timedwrlock(pthread_rwlock_t *, const struct timespec *);
#ifndef GOW3_MACOS_ALLOC_IMPL
/* Only project C runtime allocations. Third-party libraries keep their own
 * allocator. Guest-visible handles and TLS must fit PS4's 40-bit pointers. */
#define malloc(size) gow3_macos_malloc(size)
#define calloc(count,size) gow3_macos_calloc(count,size)
#define realloc(ptr,size) gow3_macos_realloc(ptr,size)
#define free(ptr) gow3_macos_free(ptr)
#define pthread_mutex_timedlock gow3_macos_mutex_timedlock
#define pthread_rwlock_timedrdlock gow3_macos_rwlock_timedrdlock
#define pthread_rwlock_timedwrlock gow3_macos_rwlock_timedwrlock
#endif
#endif
