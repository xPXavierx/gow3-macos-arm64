// SPDX-License-Identifier: GPL-2.0-or-later
// CAMetalDisplayLink owns drawables only while interpolation is enabled. The
// MoltenVK adapter consumes these exact drawables and presentation deadlines.
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

// Window HUD and presentation timestamps belong to every Metal build, including
// builds that exclude frame interpolation.
extern "C" void Gow3SetMetalHud(void* opaque, bool show) {
    @autoreleasepool {
        CAMetalLayer* layer = (__bridge CAMetalLayer*)opaque;
        if (@available(macOS 13.0, *))
            layer.developerHUDProperties = @{@"mode": show ? @"default" : @"disabled"};
    }
}
extern "C" unsigned long long Gow3MetalPresentationTimeNs() {
    return (unsigned long long)(CACurrentMediaTime() * 1e9);
}
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <deque>
#include <unordered_map>
#include <pthread.h>

namespace {
std::atomic<unsigned> adapter_version{};
struct DisplayState {
    std::mutex mutex;
    std::condition_variable changed;
    CAMetalLayer* layer;
    struct Drawable {id<CAMetalDrawable> image; double target;};
    std::deque<Drawable> pending;
    double retry_after{};
    unsigned requested{};
    bool started{}, active{}, waiting{};
    CFRunLoopRef runloop{};
    std::unordered_map<unsigned long long,double> targets;
};
DisplayState& State() { static auto* state=new DisplayState; return *state; }
}

@interface Gow3MetalDisplayDelegate : NSObject<CAMetalDisplayLinkDelegate>
@end
@implementation Gow3MetalDisplayDelegate
- (void)metalDisplayLink:(CAMetalDisplayLink*)link needsUpdate:(CAMetalDisplayLinkUpdate*)update {
    auto& state=State();
    std::lock_guard guard(state.mutex);
    if(!state.requested || (state.requested<=60 && !state.waiting)) return;
    // At high refresh, keep at most two not-yet-bound drawables ahead of the
    // consumer. A game-frame pair can be encoded without blocking Vulkan's
    // rendering queue for another display callback. No game image is dropped.
    const unsigned capacity=state.requested>60 ? 2 : 1;
    while(state.pending.size()>=capacity)state.pending.pop_front();
    state.pending.push_back({update.drawable,update.targetPresentationTimestamp});
    state.waiting=false;
    state.changed.notify_all();
}
@end

namespace {
void RunDisplayLink() { @autoreleasepool {
    pthread_setname_np("gow3:MetalDisplayLink");
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
    auto& state=State();
    CAMetalLayer* layer;
    {
        std::lock_guard guard(state.mutex);
        if(!state.requested) {state.started=false;state.changed.notify_all();return;}
        layer=state.layer;
    }
    __attribute__((objc_precise_lifetime)) id delegate=[Gow3MetalDisplayDelegate new];
    auto link=[[CAMetalDisplayLink alloc] initWithMetalLayer:layer];
    if(!link) {
        std::lock_guard guard(state.mutex);state.started=false;state.changed.notify_all();return;
    }
    link.delegate=delegate;
    link.preferredFrameLatency=2;
    [link addToRunLoop:NSRunLoop.currentRunLoop forMode:NSDefaultRunLoopMode];
    {
        std::lock_guard guard(state.mutex);
        state.runloop=CFRunLoopGetCurrent();
        state.active=true;state.changed.notify_all();
    }
    unsigned previous=0;
    for(;;) { @autoreleasepool {
        unsigned rate;
        {std::lock_guard guard(state.mutex);rate=state.requested;}
        if(!rate)break;
        if(rate!=previous) {
            link.preferredFrameRateRange=CAFrameRateRangeMake(rate,rate,rate);
            previous=rate;
            std::printf("Metal display link: %u Hz, exact drawable/deadline ownership\n",rate);
        }
        [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.05]];
    }}
    // Pausing is insufficient: nextDrawable cannot be used until invalidation
    // detaches the display link from the layer (including live Off switches).
    [link invalidate];
    {
        std::lock_guard guard(state.mutex);
        state.pending.clear();state.runloop=nullptr;state.active=state.started=false;
        state.changed.notify_all();
    }
}}
void StopAndDrain(std::unique_lock<std::mutex>& lock) {
    auto& state=State();state.requested=0;state.pending.clear();state.waiting=false;
    if(state.runloop)CFRunLoopWakeUp(state.runloop);
    state.changed.notify_all();
    state.changed.wait(lock,[&]{return !state.started;});
}
}

extern "C" __attribute__((visibility("default"))) void Gow3UpdateMetalDisplayLink(void* opaque,unsigned rate) {
    auto& state=State();
    std::unique_lock lock(state.mutex);
    if(!rate) {if(state.started)StopAndDrain(lock);return;}
    if(CACurrentMediaTime()<state.retry_after)return;
    if(state.layer && state.layer!=(__bridge CAMetalLayer*)opaque && state.started)StopAndDrain(lock);
    state.layer=(__bridge CAMetalLayer*)opaque;
    if(!state.layer.device)return;
    state.requested=rate;
    if(!state.started) {state.started=true;std::thread(RunDisplayLink).detach();}
    if(state.runloop)CFRunLoopWakeUp(state.runloop);
    state.changed.wait(lock,[&]{return state.active || !state.started;});
}

// A true return transfers one retained drawable to MoltenVK, which already owns
// the matching release. A false return means the ordinary nextDrawable path is
// safe; the display link has been fully detached first.
extern "C" __attribute__((visibility("default"))) bool Gow3AcquireMetalDisplayDrawable(void* layer,void** drawable) {
    auto& state=State();std::unique_lock lock(state.mutex);
    if(state.layer!=(__bridge CAMetalLayer*)layer || !state.started)return false;
    while(!state.pending.empty() && state.pending.front().target<CACurrentMediaTime()+.0005)
        state.pending.pop_front();
    state.waiting=true;
    const bool ready=state.changed.wait_for(lock,std::chrono::milliseconds(250),[&]{return !state.pending.empty()||!state.requested;});
    if(!ready || !state.requested) {
        const bool stalled=state.requested!=0;
        StopAndDrain(lock);
        if(stalled) {
            state.retry_after=CACurrentMediaTime()+3;
            std::fprintf(stderr,"Metal display link: callbacks unavailable; ordinary presentation restored, retry in 3 s\n");
        }
        return false;
    }
    const auto entry=state.pending.front();state.pending.pop_front();
    state.targets[entry.image.drawableID]=entry.target;
    if(state.targets.size()>128)state.targets.erase(state.targets.begin());
    *drawable=(__bridge_retained void*)entry.image;
    return true;
}
extern "C" __attribute__((visibility("default"))) bool Gow3PresentMetalDisplayDrawable(void* opaque) {
    auto drawable=(__bridge id<CAMetalDrawable>)opaque;
    auto& state=State();double target;
    {
        std::lock_guard guard(state.mutex);
        auto found=state.targets.find(drawable.drawableID);
        if(found==state.targets.end())return false;
        target=found->second;state.targets.erase(found);
    }
    // CAMetalDisplayLink supplies the deadline internally; presentAtTime is
    // forbidden for its drawables. present() targets that update's display slot.
    (void)target;[drawable present];return true;
}

extern "C" __attribute__((visibility("default"))) unsigned Gow3MetalDisplayLinkRate() {
    auto& state=State();std::lock_guard guard(state.mutex);return state.requested;
}
extern "C" __attribute__((visibility("default"))) void Gow3RegisterMetalPresentationAdapter(unsigned version) {
    adapter_version=version;
}
extern "C" __attribute__((visibility("default"))) unsigned Gow3MetalDisplayLinkDesiredRate(unsigned rate) {
    if(adapter_version.load()!=1)return 0;
    if(const char* path=std::getenv("GOW3_FRAMEGEN_LINK_CONTROL"))
        if(FILE* file=std::fopen(path,"r")){if(std::fgetc(file)=='0')rate=0;std::fclose(file);}
    auto& state=State();std::lock_guard guard(state.mutex);
    return CACurrentMediaTime()<state.retry_after ? 0 : rate;
}
