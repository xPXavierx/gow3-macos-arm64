// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in timestamps from public drawable callbacks. Does not change presentation.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {
struct Row {
    unsigned long id;
    double acquired, actual, callback, acquire_ms;
};
struct State {
    IMP original_next{};
    std::mutex mutex;
    std::vector<Row> rows;
    const char* active{};
    FILE* output{};
};
// Process-lifetime state: presentation callbacks can outlive application shutdown.
State* state{};

id NextDrawable(id self, SEL selector) {
    const double begin = CACurrentMediaTime();
    id<CAMetalDrawable> drawable = ((id(*)(id, SEL))state->original_next)(self, selector);
    const double acquired = CACurrentMediaTime();
    if (drawable && !access(state->active, F_OK)) {
        const unsigned long sequence = drawable.drawableID;
        [drawable addPresentedHandler:^(id<MTLDrawable> presented) {
            const Row row{sequence, acquired, presented.presentedTime,
                          CACurrentMediaTime(), (acquired - begin) * 1000};
            std::lock_guard guard(state->mutex);
            if (state->rows.size() < 65536) state->rows.push_back(row);
        }];
    }
    return drawable;
}

__attribute__((constructor)) void InstallPresentationProbe() {
    if (!getprogname() || std::strcmp(getprogname(), "gow3-probe-arm64")) return;
    const char* active = std::getenv("GOW3_DRAWABLE_ACTIVE");
    const char* output = std::getenv("GOW3_DRAWABLE_OUTPUT");
    if (!active || !output) return;
    FILE* stream = std::fopen(output, "w");
    if (!stream) return;
    state = new State;
    state->active = active;
    state->output = stream;
    Class cls = [CAMetalLayer class];
    Method method = class_getInstanceMethod(cls, @selector(nextDrawable));
    state->original_next = method_getImplementation(method);
    class_replaceMethod(cls, @selector(nextDrawable), (IMP)NextDrawable,
                        method_getTypeEncoding(method));
    std::thread([] {
        for (;;) {
            sleep(1);
            std::vector<Row> batch;
            { std::lock_guard guard(state->mutex); batch.swap(state->rows); }
            for (const auto& row : batch)
                std::fprintf(state->output,
                    "{\"id\":%lu,\"acquired\":%.9f,\"actual\":%.9f,\"callback\":%.9f,\"acquire_ms\":%.6f}\n",
                    row.id, row.acquired, row.actual, row.callback, row.acquire_ms);
            std::fflush(state->output);
        }
    }).detach();
    std::fprintf(stderr, "Presentation probe: direct Metal timestamps; no presentation override\n");
}
} // namespace
