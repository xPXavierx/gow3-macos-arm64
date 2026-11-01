/* SPDX-License-Identifier: GPL-2.0-or-later
 * Lookup policy/optional measurements. Call only under the owning registry lock. */
#ifndef GOW3_RUNTIME_REGISTRY_H
#define GOW3_RUNTIME_REGISTRY_H
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct RuntimeRegistryStats {
    int initialized, front, diagnostic;
    uint64_t lookups, hits, nodes, nanoseconds;
} RuntimeRegistryStats;

static inline uint64_t registry_clock(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;
}
static inline uint64_t registry_begin(RuntimeRegistryStats *s,const char *option) {
    if (!s->initialized) {
        const char *front=getenv(option), *diag=getenv("GOW3_REGISTRY_DIAG");
        s->front=!(front && front[0]=='0');
        s->diagnostic=diag && diag[0]!='0';
        s->initialized=1;
    }
    return s->diagnostic ? registry_clock() : 0;
}
static inline void registry_end(RuntimeRegistryStats *s,uint64_t start,uint64_t nodes,int hit) {
    if (!s->diagnostic) return;
    ++s->lookups; s->hits+=!!hit; s->nodes+=nodes;
    s->nanoseconds+=registry_clock()-start;
}
static inline void registry_report(const char *name,const RuntimeRegistryStats *s) {
    if (!s->diagnostic) return;
    printf("Registry lookup: %s front=%d calls=%" PRIu64 " hits=%" PRIu64
           " nodes=%" PRIu64 " search_ns=%" PRIu64 "\n",
           name,s->front,s->lookups,s->hits,s->nodes,s->nanoseconds);
}
#endif
