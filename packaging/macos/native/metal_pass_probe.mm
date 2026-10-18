// Local bounded diagnostics inside this game process only. No desktop capture.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <unistd.h>

@interface GpuPassRecord : NSObject
@property(nonatomic,strong) id<MTLCounterSampleBuffer> samples;
@property(nonatomic,strong) NSMutableDictionary* data;
@property(nonatomic,strong) NSMutableArray* pipelines;
@property(nonatomic) NSUInteger offset;
@end
@implementation GpuPassRecord
@end
static char pass_key, buffer_key;
static IMP original_render;
static IMP original_compute_dispatch, original_compute_descriptor;
static IMP original_blit, original_blit_descriptor;
static thread_local bool creating_other_encoder = false;
static std::mutex hook_mutex, output_mutex;
static std::unordered_map<Class,IMP> original_pipeline;
static std::unordered_map<Class,IMP> original_compute_pipeline, original_end;
static IMP original_new_pipeline;
static IMP original_new_pipeline_async, original_new_pipeline_reflection, original_new_pipeline_async_reflection;
static IMP original_new_depth;
static std::unordered_map<Class,IMP> original_bind_depth;
static char depth_key;
static char pipeline_key;
static id<MTLCounterSet> timestamp_set;
static std::atomic<uint64_t> pass_id{0}, buffer_id{0};
static std::chrono::steady_clock::time_point until;
static const char* trigger;
static FILE* output;
static uint64_t pass_limit=4096; // reset the bounded budget for each requested A/B window

static NSString* pipeline_name(MTLRenderPipelineDescriptor* d) {
    NSMutableString* name=[NSMutableString stringWithFormat:@"%@ [v=%@ f=%@]",d.label ?: @"internal",d.vertexFunction.name ?: @"none",d.fragmentFunction.name ?: @"none"];
    // Record actual host blend state, including disabled channels. This diagnoses
    // whether an identity output may be omitted without guessing from shader code.
    for(NSUInteger i=0;i<8;++i) {
        auto a=d.colorAttachments[i];if(a.pixelFormat==MTLPixelFormatInvalid)continue;
        [name appendFormat:@" [c%lu fmt=%lu mask=%lu blend=%d rgb=%lu,%lu,%lu a=%lu,%lu,%lu]",i,a.pixelFormat,a.writeMask,a.blendingEnabled,a.sourceRGBBlendFactor,a.destinationRGBBlendFactor,a.rgbBlendOperation,a.sourceAlphaBlendFactor,a.destinationAlphaBlendFactor,a.alphaBlendOperation];
    }
    return name;
}
static id new_depth(id self,SEL sel,MTLDepthStencilDescriptor* d) {
    id state=((id(*)(id,SEL,id))original_new_depth)(self,sel,d);
    if(state)objc_setAssociatedObject(state,&depth_key,d.description,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return state;
}
static void bind_depth(id self,SEL sel,id state) {
    GpuPassRecord* record=objc_getAssociatedObject(self,&pass_key);
    if(record) {
        NSMutableArray* states=record.data[@"depth_states"];
        if(!states){states=[NSMutableArray new];record.data[@"depth_states"]=states;}
        NSString* desc=objc_getAssociatedObject(state,&depth_key) ?: @"none";
        if(![states containsObject:desc])[states addObject:desc];
    }
    IMP fn;{std::lock_guard lock(hook_mutex);fn=original_bind_depth.at(object_getClass(self));}
    ((void(*)(id,SEL,id))fn)(self,sel,state);
}

static void bind_pipeline(id self,SEL sel,id<MTLRenderPipelineState> state) {
    GpuPassRecord* record=objc_getAssociatedObject(self,&pass_key);
    if (record) {
        NSString* label=objc_getAssociatedObject(state,&pipeline_key) ?: state.label ?: @"unlabelled";
        if (![record.pipelines containsObject:label]) [record.pipelines addObject:label];
        if ([label isEqualToString:@"unlabelled"] && !record.data[@"pipeline_description"]) record.data[@"pipeline_description"]=state.description;
    }
    IMP fn;
    {std::lock_guard lock(hook_mutex);fn=original_pipeline.at(object_getClass(self));}
    ((void(*)(id,SEL,id))fn)(self,sel,state);
}
static id new_pipeline(id self,SEL sel,MTLRenderPipelineDescriptor* d,NSError** error) {
    id state=((id(*)(id,SEL,id,NSError**))original_new_pipeline)(self,sel,d,error);
    if (state) objc_setAssociatedObject(state,&pipeline_key,pipeline_name(d),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return state;
}
static void new_pipeline_async(id self,SEL sel,MTLRenderPipelineDescriptor* d,MTLNewRenderPipelineStateCompletionHandler done) {
    NSString* name=pipeline_name(d);
    ((void(*)(id,SEL,id,MTLNewRenderPipelineStateCompletionHandler))original_new_pipeline_async)(self,sel,d,^(id<MTLRenderPipelineState> state,NSError* error){
        if(state) objc_setAssociatedObject(state,&pipeline_key,name,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        done(state,error);
    });
}
static id new_pipeline_reflection(id self,SEL sel,MTLRenderPipelineDescriptor* d,MTLPipelineOption options,MTLAutoreleasedRenderPipelineReflection* reflection,NSError** error) {
    id state=((id(*)(id,SEL,id,MTLPipelineOption,MTLAutoreleasedRenderPipelineReflection*,NSError**))original_new_pipeline_reflection)(self,sel,d,options,reflection,error);
    if(state) objc_setAssociatedObject(state,&pipeline_key,pipeline_name(d),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return state;
}
static void new_pipeline_async_reflection(id self,SEL sel,MTLRenderPipelineDescriptor* d,MTLPipelineOption options,MTLNewRenderPipelineStateWithReflectionCompletionHandler done) {
    NSString* name=pipeline_name(d);
    ((void(*)(id,SEL,id,MTLPipelineOption,MTLNewRenderPipelineStateWithReflectionCompletionHandler))original_new_pipeline_async_reflection)(self,sel,d,options,^(id<MTLRenderPipelineState> state,MTLRenderPipelineReflection* reflection,NSError* error){
        if(state) objc_setAssociatedObject(state,&pipeline_key,name,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        done(state,reflection,error);
    });
}
static void hook_encoder(id encoder) {
    Class cls=object_getClass(encoder);
    std::lock_guard lock(hook_mutex);
    if (original_pipeline.contains(cls)) return;
    SEL sel=@selector(setRenderPipelineState:);
    Method m=class_getInstanceMethod(cls,sel);
    original_pipeline[cls]=method_getImplementation(m);
    class_replaceMethod(cls,sel,(IMP)bind_pipeline,method_getTypeEncoding(m));
    sel=@selector(setDepthStencilState:);m=class_getInstanceMethod(cls,sel);
    original_bind_depth[cls]=method_getImplementation(m);
    class_replaceMethod(cls,sel,(IMP)bind_depth,method_getTypeEncoding(m));
}
static void complete(id<MTLCommandBuffer> cb,NSArray* records,uint64_t cb_id) {
    for (GpuPassRecord* r in records) {
        const bool other = r.data[@"kind"] != nil;
        const NSUInteger count = other ? 2 : 4;
        NSData* result=[r.samples resolveCounterRange:NSMakeRange(r.offset,count)];
        r.samples=nil;
        if (result.length<count*sizeof(MTLCounterResultTimestamp)) continue;
        const auto* t=(const MTLCounterResultTimestamp*)result.bytes;
        if (other) {
            if(!t[0].timestamp || !t[1].timestamp || t[0].timestamp==MTLCounterErrorValue || t[1].timestamp==MTLCounterErrorValue || t[1].timestamp<t[0].timestamp)continue;
            r.data[@"encoder_ms"]=@((t[1].timestamp-t[0].timestamp)/1e6);
            r.data[@"start_ns"]=@(t[0].timestamp);r.data[@"end_ns"]=@(t[1].timestamp);
            r.data[@"vertex_ms"]=@0;r.data[@"fragment_ms"]=@0;
        } else {
        const bool vertex=t[0].timestamp && t[1].timestamp && t[0].timestamp!=MTLCounterErrorValue && t[1].timestamp!=MTLCounterErrorValue && t[1].timestamp>=t[0].timestamp;
        const bool fragment=t[2].timestamp && t[3].timestamp && t[2].timestamp!=MTLCounterErrorValue && t[3].timestamp!=MTLCounterErrorValue && t[3].timestamp>=t[2].timestamp;
        if(!vertex&&!fragment)continue;
        r.data[@"vertex_ms"]=@(vertex?(t[1].timestamp-t[0].timestamp)/1e6:0);
        r.data[@"fragment_ms"]=@(fragment?(t[3].timestamp-t[2].timestamp)/1e6:0);
        r.data[@"vertex_timestamp_valid"]=@(vertex);r.data[@"fragment_timestamp_valid"]=@(fragment);
        r.data[@"start_ns"]=@(vertex&&fragment?std::min(t[0].timestamp,t[2].timestamp):vertex?t[0].timestamp:t[2].timestamp);
        r.data[@"end_ns"]=@(vertex&&fragment?std::max(t[1].timestamp,t[3].timestamp):vertex?t[1].timestamp:t[3].timestamp);
        r.data[@"kind"]=@"render";
        }
        r.data[@"pipelines"]=r.pipelines;
        r.data[@"command_buffer"]=@(cb_id);
        r.data[@"command_buffer_label"]=cb.label ?: @"";
        r.data[@"command_buffer_gpu_ms"]=@((cb.GPUEndTime-cb.GPUStartTime)*1000);
        NSData* json=[NSJSONSerialization dataWithJSONObject:r.data options:0 error:nil];
        if (json) {std::lock_guard lock(output_mutex);fwrite(json.bytes,1,json.length,output);fputc('\n',output);fflush(output);}
    }
}
static void end_other(id self,SEL sel) {
    GpuPassRecord* record=objc_getAssociatedObject(self,&pass_key);
    if(record)record.data[@"encoder_label"]=[self label] ?: @"";
    IMP fn;{std::lock_guard lock(hook_mutex);fn=original_end.at(object_getClass(self));}
    ((void(*)(id,SEL))fn)(self,sel);
}
static void bind_compute(id self,SEL sel,id<MTLComputePipelineState> state) {
    GpuPassRecord* record=objc_getAssociatedObject(self,&pass_key);
    if(record){NSString* name=state.label ?: @"unlabelled";if(![record.pipelines containsObject:name])[record.pipelines addObject:name];if(!record.data[@"pipeline_description"])record.data[@"pipeline_description"]=state.description;}
    IMP fn;{std::lock_guard lock(hook_mutex);fn=original_compute_pipeline.at(object_getClass(self));}
    ((void(*)(id,SEL,id))fn)(self,sel,state);
}
static void hook_other(id encoder,bool compute) {
    Class cls=object_getClass(encoder);std::lock_guard lock(hook_mutex);
    if(!original_end.contains(cls)){SEL sel=@selector(endEncoding);Method m=class_getInstanceMethod(cls,sel);original_end[cls]=method_getImplementation(m);class_replaceMethod(cls,sel,(IMP)end_other,method_getTypeEncoding(m));}
    if(compute&&!original_compute_pipeline.contains(cls)){SEL sel=@selector(setComputePipelineState:);Method m=class_getInstanceMethod(cls,sel);original_compute_pipeline[cls]=method_getImplementation(m);class_replaceMethod(cls,sel,(IMP)bind_compute,method_getTypeEncoding(m));}
}
static bool other_active() {
    std::lock_guard lock(hook_mutex);
    const auto now=std::chrono::steady_clock::now();
    if(!access(trigger,F_OK)){unlink(trigger);until=now+std::chrono::milliseconds(1500);pass_limit=pass_id.load()+4096;fprintf(stderr,"Metal all-pass probe: sampling render/compute/blit stages for 1.5 seconds\n");}
    return now<until&&pass_id.load()<pass_limit;
}
static id make_other(id<MTLCommandBuffer> self,bool compute,MTLDispatchType dispatch) {
    NSMutableArray* records=objc_getAssociatedObject(self,&buffer_key);
    id<MTLCounterSampleBuffer> samples=records.count?((GpuPassRecord*)records.firstObject).samples:nil;
    if(!samples){MTLCounterSampleBufferDescriptor* sd=[MTLCounterSampleBufferDescriptor new];sd.counterSet=timestamp_set;sd.storageMode=MTLStorageModeShared;sd.sampleCount=4096;NSError* error=nil;samples=[self.device newCounterSampleBufferWithDescriptor:sd error:&error];if(!samples)return nil;}
    const NSUInteger offset=records.count*4;if(offset+4>4096)return nil;
    creating_other_encoder=true;id encoder;
    if(compute){MTLComputePassDescriptor* desc=[MTLComputePassDescriptor new];desc.dispatchType=dispatch;auto a=desc.sampleBufferAttachments[0];a.sampleBuffer=samples;a.startOfEncoderSampleIndex=offset;a.endOfEncoderSampleIndex=offset+1;encoder=((id(*)(id,SEL,id))original_compute_descriptor)(self,@selector(computeCommandEncoderWithDescriptor:),desc);}
    else{MTLBlitPassDescriptor* desc=[MTLBlitPassDescriptor new];auto a=desc.sampleBufferAttachments[0];a.sampleBuffer=samples;a.startOfEncoderSampleIndex=offset;a.endOfEncoderSampleIndex=offset+1;encoder=((id(*)(id,SEL,id))original_blit_descriptor)(self,@selector(blitCommandEncoderWithDescriptor:),desc);}
    creating_other_encoder=false;if(!encoder)return nil;
    GpuPassRecord* r=[GpuPassRecord new];r.samples=samples;r.offset=offset;r.pipelines=[NSMutableArray new];r.data=[NSMutableDictionary dictionaryWithDictionary:@{@"pass":@(pass_id.fetch_add(1)),@"kind":compute?@"compute":@"blit",@"colors":@[]}];
    objc_setAssociatedObject(encoder,&pass_key,r,OBJC_ASSOCIATION_RETAIN_NONATOMIC);hook_other(encoder,compute);
    if(!records){records=[NSMutableArray new];objc_setAssociatedObject(self,&buffer_key,records,OBJC_ASSOCIATION_RETAIN_NONATOMIC);const uint64_t cb_id=buffer_id.fetch_add(1);[self addCompletedHandler:^(id<MTLCommandBuffer> cb){complete(cb,records,cb_id);[records removeAllObjects];}];}
    [records addObject:r];return encoder;
}
static id make_compute_dispatch(id<MTLCommandBuffer> self,SEL sel,MTLDispatchType dispatch) {
    if(!creating_other_encoder&&other_active()){id encoder=make_other(self,true,dispatch);if(encoder)return encoder;}
    return ((id(*)(id,SEL,MTLDispatchType))original_compute_dispatch)(self,sel,dispatch);
}
static id make_blit(id<MTLCommandBuffer> self,SEL sel) {
    if(!creating_other_encoder&&other_active()){id encoder=make_other(self,false,MTLDispatchTypeSerial);if(encoder)return encoder;}
    return ((id(*)(id,SEL))original_blit)(self,sel);
}
static id make_encoder(id<MTLCommandBuffer> self,SEL sel,MTLRenderPassDescriptor* desc) {
    bool active=false;
    {
        std::lock_guard lock(hook_mutex);
        const auto now=std::chrono::steady_clock::now();
        if (!access(trigger,F_OK)) {
            unlink(trigger);until=now+std::chrono::milliseconds(1500);pass_limit=pass_id.load()+4096;
            fprintf(stderr,"Metal pass probe: sampling render/compute/blit stages for up to 1.5 seconds or 4096 encoders\n");
        }
        active=now<until && pass_id.load()<pass_limit;
    }
    if (!active) return ((id(*)(id,SEL,id))original_render)(self,sel,desc);
    NSMutableArray* records=objc_getAssociatedObject(self,&buffer_key);
    id<MTLCounterSampleBuffer> sample;
    if (records.count) sample=((GpuPassRecord*)records.firstObject).samples;
    if (!sample) {
        MTLCounterSampleBufferDescriptor* sd=[MTLCounterSampleBufferDescriptor new];
        sd.counterSet=timestamp_set;sd.storageMode=MTLStorageModeShared;sd.sampleCount=4096;
        NSError* error=nil;sample=[self.device newCounterSampleBufferWithDescriptor:sd error:&error];
        if (!sample) {fprintf(stderr,"Metal pass probe: sample allocation failed %s\n",error.description.UTF8String);return ((id(*)(id,SEL,id))original_render)(self,sel,desc);}
    }
    const NSUInteger offset=records.count*4;
    if (offset+4>4096) return ((id(*)(id,SEL,id))original_render)(self,sel,desc);
    MTLRenderPassDescriptor* copy=[desc copy];
    NSUInteger slot=0;
    while (slot<4 && copy.sampleBufferAttachments[slot].sampleBuffer) ++slot;
    if (slot==4) return ((id(*)(id,SEL,id))original_render)(self,sel,desc);
    auto a=copy.sampleBufferAttachments[slot];a.sampleBuffer=sample;
    a.startOfVertexSampleIndex=offset;a.endOfVertexSampleIndex=offset+1;
    a.startOfFragmentSampleIndex=offset+2;a.endOfFragmentSampleIndex=offset+3;
    id encoder=((id(*)(id,SEL,id))original_render)(self,sel,copy);
    GpuPassRecord* r=[GpuPassRecord new];r.samples=sample;r.offset=offset;r.pipelines=[NSMutableArray new];
    r.data=[NSMutableDictionary dictionaryWithDictionary:@{@"pass":@(pass_id.fetch_add(1))}];
    NSMutableArray* targets=[NSMutableArray new];
    for (NSUInteger i=0;i<8;++i) {
        auto ca=desc.colorAttachments[i];id<MTLTexture> t=ca.texture;if (!t) continue;
        [targets addObject:@{@"width":@(std::max(NSUInteger(1),t.width>>ca.level)),@"height":@(std::max(NSUInteger(1),t.height>>ca.level)),@"format":@(t.pixelFormat),@"samples":@(t.sampleCount),@"usage":@(t.usage),@"load":@(ca.loadAction),@"store":@(ca.storeAction),@"label":t.label ?: @""}];
    }
    r.data[@"colors"]=targets;
    if(!targets.count && !desc.depthAttachment.texture && !desc.stencilAttachment.texture) r.data[@"descriptor_description"]=desc.description;
    id<MTLTexture> dt=desc.depthAttachment.texture;
    if (dt) r.data[@"depth"]=@{@"width":@(dt.width),@"height":@(dt.height),@"format":@(dt.pixelFormat),@"samples":@(dt.sampleCount),@"load":@(desc.depthAttachment.loadAction),@"store":@(desc.depthAttachment.storeAction)};
    id<MTLTexture> st=desc.stencilAttachment.texture;
    if (st) r.data[@"stencil"]=@{@"width":@(st.width),@"height":@(st.height),@"format":@(st.pixelFormat),@"samples":@(st.sampleCount),@"load":@(desc.stencilAttachment.loadAction),@"store":@(desc.stencilAttachment.storeAction),@"label":st.label ?: @""};
    objc_setAssociatedObject(encoder,&pass_key,r,OBJC_ASSOCIATION_RETAIN_NONATOMIC);hook_encoder(encoder);
    if (!records) {
        records=[NSMutableArray new];objc_setAssociatedObject(self,&buffer_key,records,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        const uint64_t cb_id=buffer_id.fetch_add(1);
        [self addCompletedHandler:^(id<MTLCommandBuffer> cb){complete(cb,records,cb_id);[records removeAllObjects];}];
    }
    [records addObject:r];return encoder;
}
__attribute__((constructor)) static void start_probe() {
    if (strcmp(getprogname(),"gow3-probe-arm64")) return;
    trigger=getenv("GOW3_METAL_PASS_TRIGGER");const char* path=getenv("GOW3_METAL_PASS_OUTPUT");
    if (!trigger||!path) return;
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        Class dc=object_getClass(device);SEL ps=@selector(newRenderPipelineStateWithDescriptor:error:);
        SEL ds=@selector(newDepthStencilStateWithDescriptor:);Method dm=class_getInstanceMethod(dc,ds);
        original_new_depth=method_getImplementation(dm);class_replaceMethod(dc,ds,(IMP)new_depth,method_getTypeEncoding(dm));
        Method pm=class_getInstanceMethod(dc,ps);original_new_pipeline=method_getImplementation(pm);
        class_replaceMethod(dc,ps,(IMP)new_pipeline,method_getTypeEncoding(pm));
        ps=@selector(newRenderPipelineStateWithDescriptor:completionHandler:);pm=class_getInstanceMethod(dc,ps);
        original_new_pipeline_async=method_getImplementation(pm);class_replaceMethod(dc,ps,(IMP)new_pipeline_async,method_getTypeEncoding(pm));
        ps=@selector(newRenderPipelineStateWithDescriptor:options:reflection:error:);pm=class_getInstanceMethod(dc,ps);
        original_new_pipeline_reflection=method_getImplementation(pm);class_replaceMethod(dc,ps,(IMP)new_pipeline_reflection,method_getTypeEncoding(pm));
        ps=@selector(newRenderPipelineStateWithDescriptor:options:completionHandler:);pm=class_getInstanceMethod(dc,ps);
        original_new_pipeline_async_reflection=method_getImplementation(pm);class_replaceMethod(dc,ps,(IMP)new_pipeline_async_reflection,method_getTypeEncoding(pm));
        if (![device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) {fprintf(stderr,"Metal pass probe: no stage counters\n");return;}
        for (id<MTLCounterSet> set in device.counterSets) if ([set.name isEqualToString:MTLCommonCounterSetTimestamp]) timestamp_set=set;
        if (!timestamp_set) return;
        output=fopen(path,"w");if (!output) return;
        id<MTLCommandQueue> queue=[device newCommandQueue];id cb=[queue commandBuffer];Class cls=object_getClass(cb);
        SEL sel=@selector(renderCommandEncoderWithDescriptor:);Method m=class_getInstanceMethod(cls,sel);
        original_render=method_getImplementation(m);class_replaceMethod(cls,sel,(IMP)make_encoder,method_getTypeEncoding(m));
        original_compute_descriptor=class_getMethodImplementation(cls,@selector(computeCommandEncoderWithDescriptor:));
        original_blit_descriptor=class_getMethodImplementation(cls,@selector(blitCommandEncoderWithDescriptor:));
        sel=@selector(computeCommandEncoderWithDispatchType:);m=class_getInstanceMethod(cls,sel);original_compute_dispatch=method_getImplementation(m);class_replaceMethod(cls,sel,(IMP)make_compute_dispatch,method_getTypeEncoding(m));
        sel=@selector(blitCommandEncoder);m=class_getInstanceMethod(cls,sel);original_blit=method_getImplementation(m);class_replaceMethod(cls,sel,(IMP)make_blit,method_getTypeEncoding(m));
        fprintf(stderr,"Metal pass probe: attached ARM64 per-pass stage counters to %s\n",class_getName(cls));
    }
}
