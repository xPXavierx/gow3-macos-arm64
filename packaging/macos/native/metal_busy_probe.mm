// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in, bounded command-buffer timing in this process only. No images or counters.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>
#include <unistd.h>
struct BusyRow { double start, end; unsigned status; };
static std::mutex lock;
static std::vector<BusyRow> rows;
static IMP original_commit;
static const char* trigger;
static const char* output;
static double duration;
static bool armed=false, flushed=false;
static unsigned pending=0, invalid=0;
static std::chrono::steady_clock::time_point until;
static void flush_locked() {
    if(flushed || !armed || pending) return;
    flushed=true;
    @autoreleasepool {
        NSMutableArray* values=[NSMutableArray new];
        for(const auto& r:rows) [values addObject:@{@"start":@(r.start),@"end":@(r.end),@"status":@(r.status)}];
        NSData* data=[NSJSONSerialization dataWithJSONObject:@{@"requested_seconds":@(duration),@"invalid_timestamps":@(invalid),@"counter_buffers_used":@NO,@"buffers":values} options:0 error:nil];
        if(data) [data writeToFile:[NSString stringWithUTF8String:output] atomically:YES];
    }
}
static void commit_buffer(id<MTLCommandBuffer> cb, SEL sel) {
    bool collect=false;
    {
        std::lock_guard guard(lock);
        const auto now=std::chrono::steady_clock::now();
        if(!armed && !access(trigger,F_OK)) {
            unlink(trigger); armed=true;
            until=now+std::chrono::milliseconds(int(duration*1000));
        }
        collect=armed && now<until && rows.size()+pending<10000;
        if(collect) ++pending;
        else if(armed && now>=until) flush_locked();
    }
    if(collect) [cb addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        std::lock_guard guard(lock);
        const double begin=completed.GPUStartTime, end=completed.GPUEndTime;
        if(begin>0 && end>=begin) rows.push_back({begin,end,unsigned(completed.status)});
        else ++invalid;
        --pending;
        if(std::chrono::steady_clock::now()>=until) flush_locked();
    }];
    ((void(*)(id,SEL))original_commit)(cb,sel);
}
__attribute__((constructor)) static void install_busy_probe() {
    @autoreleasepool {
        if (!getprogname() || std::strcmp(getprogname(), "gow3-probe-arm64")) return;
        trigger=std::getenv("GOW3_METAL_BUSY_TRIGGER");output=std::getenv("GOW3_METAL_BUSY_OUTPUT");
        if(!trigger || !output) return;
        const char* seconds=std::getenv("GOW3_METAL_BUSY_SECONDS");
        duration=seconds ? std::atof(seconds) : 10.;
        if(!std::isfinite(duration) || duration<1 || duration>30) return;
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue=[device newCommandQueue];
        id buffer=[queue commandBuffer];
        Class cls=object_getClass(buffer);SEL selector=@selector(commit);
        Method method=class_getInstanceMethod(cls,selector);if(!method) return;
        original_commit=method_getImplementation(method);
        class_replaceMethod(cls,selector,(IMP)commit_buffer,method_getTypeEncoding(method));
        std::fprintf(stderr,"Metal busy probe: bounded command-buffer timestamps on %s; no encoder counter samples\n",class_getName(cls));
    }
}
