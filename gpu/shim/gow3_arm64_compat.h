// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from bloodborne-native/bloodborne-runtime's ARM64 compatibility layer.
#pragma once
#if defined(__aarch64__)
#include <stdint.h>
#include <pthread.h>
static inline uint64_t gettid(void) { uint64_t tid; pthread_threadid_np(nullptr, &tid); return tid; }
static inline uint64_t __rdtsc(void) {
    uint64_t ticks,hz;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(ticks));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz));
    return (uint64_t)(((__uint128_t)ticks*2400000000ull)/hz);
}
static inline void _mm_pause(void) { __asm__ volatile("yield"); }
static inline void _mm_lfence(void) { __asm__ volatile("dsb ld" ::: "memory"); }
#define __builtin_ia32_pause() __asm__ volatile("yield")
#endif
