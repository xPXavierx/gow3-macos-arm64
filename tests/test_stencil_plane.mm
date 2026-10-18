// Synthetic Metal regression: a stencil-only projection preserves both planes
// and produces exactly the same masked MIN blend as a combined attachment.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cassert>
#include <bit>
#include <cstdio>
#include <cstring>
#include <cmath>

int main() { @autoreleasepool {
    constexpr NSUInteger w=64,h=64,row=256;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice(); assert(device);
    NSError* error=nil;
    NSString* source=@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 vs(uint i [[vertex_id]]) { return float4(i==1?3.0:-1.0,i==2?3.0:-1.0,0.0,1.0); }\n"
        "fragment float4 fs(){return float4(0.25);}";
    id<MTLLibrary> library=[device newLibraryWithSource:source options:nil error:&error]; assert(library);
    auto make_texture=[&](MTLPixelFormat format) {
        auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
        d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        id<MTLTexture> t=[device newTextureWithDescriptor:d]; assert(t); return t;
    };
    auto depth=make_texture(MTLPixelFormatDepth32Float_Stencil8);
    auto descriptor=[MTLRenderPipelineDescriptor new];descriptor.vertexFunction=[library newFunctionWithName:@"vs"];
    descriptor.depthAttachmentPixelFormat=descriptor.stencilAttachmentPixelFormat=depth.pixelFormat;
    auto mask_pipeline=[device newRenderPipelineStateWithDescriptor:descriptor error:&error]; assert(mask_pipeline);
    descriptor.fragmentFunction=[library newFunctionWithName:@"fs"];
    auto a=descriptor.colorAttachments[0];a.pixelFormat=MTLPixelFormatRGBA8Unorm;
    a.writeMask=MTLColorWriteMaskBlue;a.blendingEnabled=YES;
    a.sourceRGBBlendFactor=a.destinationRGBBlendFactor=MTLBlendFactorOne;a.rgbBlendOperation=MTLBlendOperationMin;
    auto reference=[device newRenderPipelineStateWithDescriptor:descriptor error:&error];assert(reference);
    descriptor.depthAttachmentPixelFormat=MTLPixelFormatInvalid;
    auto optimized=[device newRenderPipelineStateWithDescriptor:descriptor error:&error];assert(optimized);
    auto ds=[MTLDepthStencilDescriptor new];ds.depthCompareFunction=MTLCompareFunctionAlways;ds.depthWriteEnabled=NO;
    auto stencil=[MTLStencilDescriptor new];stencil.stencilCompareFunction=MTLCompareFunctionAlways;
    stencil.depthStencilPassOperation=MTLStencilOperationReplace;stencil.writeMask=0xff;
    ds.frontFaceStencil=ds.backFaceStencil=stencil;
    auto mask_state=[device newDepthStencilStateWithDescriptor:ds];assert(mask_state);
    stencil=[MTLStencilDescriptor new];stencil.stencilCompareFunction=MTLCompareFunctionEqual;
    stencil.readMask=0x5f;stencil.writeMask=0xff;
    ds.frontFaceStencil=ds.backFaceStencil=stencil;
    auto test_state=[device newDepthStencilStateWithDescriptor:ds];assert(test_state);
    id<MTLCommandQueue> queue=[device newCommandQueue];id<MTLCommandBuffer> cmd=[queue commandBuffer];
    auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
    pass.depthAttachment.texture=pass.stencilAttachment.texture=depth;
    pass.depthAttachment.loadAction=pass.stencilAttachment.loadAction=MTLLoadActionClear;
    pass.depthAttachment.storeAction=pass.stencilAttachment.storeAction=MTLStoreActionStore;
    pass.depthAttachment.clearDepth=.618f;pass.stencilAttachment.clearStencil=0x12;
    auto encoder=[cmd renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:mask_pipeline];[encoder setDepthStencilState:mask_state];
    [encoder setStencilReferenceValue:0x34];[encoder setScissorRect:MTLScissorRect{0,0,w/2,h}];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[encoder endEncoding];
    id<MTLBuffer> colors[2];
    for(unsigned i=0;i<2;++i) {
        auto color=make_texture(MTLPixelFormatRGBA8Unorm);colors[i]=[device newBufferWithLength:row*h options:MTLResourceStorageModeShared];
        pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=color;pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        pass.colorAttachments[0].storeAction=MTLStoreActionStore;pass.colorAttachments[0].clearColor=MTLClearColorMake(.8,.6,.4,.2);
        pass.stencilAttachment.texture=depth;pass.stencilAttachment.loadAction=MTLLoadActionLoad;pass.stencilAttachment.storeAction=MTLStoreActionStore;
        if(i==0){pass.depthAttachment.texture=depth;pass.depthAttachment.loadAction=MTLLoadActionLoad;pass.depthAttachment.storeAction=MTLStoreActionStore;}
        encoder=[cmd renderCommandEncoderWithDescriptor:pass];[encoder setRenderPipelineState:i?optimized:reference];
        [encoder setDepthStencilState:test_state];[encoder setStencilReferenceValue:0x12];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[encoder endEncoding];
        auto blit=[cmd blitCommandEncoder];
        [blit copyFromTexture:color sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOrigin{0,0,0} sourceSize:MTLSize{w,h,1} toBuffer:colors[i] destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*h];[blit endEncoding];
    }
    auto depths=[device newBufferWithLength:row*h options:MTLResourceStorageModeShared];
    auto stencils=[device newBufferWithLength:row*h options:MTLResourceStorageModeShared];auto blit=[cmd blitCommandEncoder];
    [blit copyFromTexture:depth sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOrigin{0,0,0} sourceSize:MTLSize{w,h,1} toBuffer:depths destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*h options:MTLBlitOptionDepthFromDepthStencil];
    [blit copyFromTexture:depth sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOrigin{0,0,0} sourceSize:MTLSize{w,h,1} toBuffer:stencils destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*h options:MTLBlitOptionStencilFromDepthStencil];[blit endEncoding];
    [cmd commit];[cmd waitUntilCompleted];assert(cmd.status==MTLCommandBufferStatusCompleted);
    assert(!std::memcmp(colors[0].contents,colors[1].contents,row*h));
    for(NSUInteger y=0;y<h;++y) for(NSUInteger x=0;x<w;++x) {
        const auto* rgba=(const unsigned char*)colors[1].contents+y*row+x*4;
        assert(std::abs(int(rgba[0])-204)<=1 && std::abs(int(rgba[1])-153)<=1 && std::abs(int(rgba[3])-51)<=1);
        assert(std::abs(int(rgba[2])-(x<w/2?102:64))<=1);
        assert(((const unsigned char*)stencils.contents)[y*row+x]==(x<w/2?0x34:0x12));
        assert(((const float*)depths.contents)[y*w+x]==.618f);
    }
    std::puts("PASS: 4096 identical masked MIN pixels; depth and stencil preserved with depth plane omitted");
} }
