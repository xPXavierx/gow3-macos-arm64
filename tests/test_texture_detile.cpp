// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic payloads only: verifies production uploads/detiling on the host GPU.
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/texture_cache/blit_helper.h"
#include "video_core/texture_cache/image.h"
#include "video_core/texture_cache/image_view.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/tile_manager.h"
using namespace VideoCore;
static void TestRenderTargetFormats(Vulkan::Instance& instance, Vulkan::Scheduler& scheduler,
                                    Vulkan::Runtime& runtime) {
    Common::SlotVector<ImageView> views;
    ImageInfo info{};
    info.type = AmdGpu::ImageType::Color2DMsaa;
    info.pixel_format = vk::Format::eR8G8B8A8Unorm;
    info.num_bits = 32; info.num_samples = info.guest_samples = 2;
    info.size = {32, 32, 1}; info.pitch = 32; info.resources = {1, 1};
    info.guest_size = 32 * 32 * 4 * 2;
    info.mips_layout[0] = {info.guest_size, 32, 32, 0};
    Image image(instance, runtime, views, info);
    const char* mode = std::getenv("GOW3_LAZY_COLOR");
    assert(image.backing->limited_color_formats == (mode && mode[0] == '1'));
    const SubresourceRange range{.base = {}, .extent = {1, 1}};
    runtime.ClearImage(&image, range, vk::ClearValue{vk::ClearColorValue{
        std::array<float, 4>{0.25f, 0.5f, 0.75f, 1.f}}});
    image.TryCompactMinTarget();
    assert(image.backing->limited_color_formats);
    ImageViewInfo linear{}; linear.type = AmdGpu::ImageType::Color2DMsaa;
    linear.range = range;
    image.FindView(linear);
    auto srgb = linear; srgb.format = vk::Format::eR8G8B8A8Srgb;
    image.FindView(srgb);
    assert(image.backing->limited_color_formats); // Sampling the sRGB pair stays restricted.
    assert(image.backing->limited_color_formats);
    auto resolved_info = info; resolved_info.num_samples = resolved_info.guest_samples = 1;
    resolved_info.type = AmdGpu::ImageType::Color2D;
    Image resolved(instance, runtime, views, resolved_info);
    Buffer readback(instance, 0, 32 * 32 * 4, MemoryType::HostCached);
    const auto download = [&] {
        runtime.ResolveImage(&image, &resolved, range, range);
        const vk::BufferImageCopy copy{.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                       .imageExtent = {32, 32, 1}};
        runtime.DownloadImage(&resolved, &readback, std::span{&copy, 1});
        runtime.FlushBarriers(); scheduler.Finish(); readback.Invalidate(0, 32 * 32 * 4);
        return std::vector<u8>(readback.mapped_data.begin(), readback.mapped_data.begin() + 4096);
    };
    const auto original = download();
    for (u32 i = 0; i < original.size(); i += 4) {
        assert(original[i] == 64 && original[i+1] == 128 && original[i+2] == 191 && original[i+3] == 255);
    }
    // Reinterpretation needs a wider allocation. Every sample must survive the
    // copy, and previously recorded descriptors must retain their old backing.
    auto integer = linear; integer.format = vk::Format::eR32Uint;
    image.FindView(integer);
    assert(!image.backing->limited_color_formats);
    assert(download() == original);
    runtime.SetBackingSamples(&image, 1);
    runtime.SetBackingSamples(&image, 2);
    image.FindView(integer);
    assert(download() == original);
    std::puts("PASS: MSAA linear/sRGB views, integer view expansion and backing sample changes preserve exact color bytes");
}
static u32 Morton(u32 x,u32 y) {
    u32 result=0;
    for(u32 bit=0;bit<3;++bit) result|=((x>>bit)&1)<<(2*bit)|((y>>bit)&1)<<(2*bit+1);
    return result;
}
int main(int argc, char**) {
    const unsigned rounds = argc > 1 ? 32 : 1;
    Vulkan::Instance instance(0,std::getenv("GOW3_VK_VALIDATION")!=nullptr);
    Vulkan::Scheduler scheduler(instance);Vulkan::Runtime runtime(instance,scheduler);
    if(const char* option=std::getenv("GOW3_LAZY_COLOR");
       option && (option[0]=='1' || option[0]=='3')) TestRenderTargetFormats(instance,scheduler,runtime);
    StreamBuffer uniforms(instance,scheduler,MemoryType::Stream,1<<20);
    TileManager tiler(instance,scheduler,runtime,uniforms);
    struct Pending {
        std::unique_ptr<Buffer> input;
        std::unique_ptr<Buffer> readback;
        std::vector<u8> expected;
        u32 bits;
        bool block;
        unsigned round;
        std::unique_ptr<Image> image;
    };
    Common::SlotVector<ImageView> views;
    std::vector<Pending> pending;
    size_t checked=0;
    for (unsigned round = 0; round < rounds; ++round)
    for(u32 bits:{8,16,32,64,128}) for(bool block:{false,true}) {
        if(block && bits<64) continue;
        ImageInfo info{};info.props.is_tiled=1;info.props.is_block=block;
        info.tile_mode=AmdGpu::TileMode::Thin1DThin;
        info.array_mode=AmdGpu::ArrayMode::Array1DTiledThin1;
        info.num_bits=bits;info.resources.levels=4;info.resources.layers=2;
        const u32 bytes=bits/8, scale=block?4:1;
        for(u32 m=0;m<4;++m) {
            u32 w=128>>m,h=64>>m,size=w*h*2*bytes;
            info.mips_layout[m]={size,w*scale,h*scale,info.guest_size};info.guest_size+=size;
        }
        auto& entry = pending.emplace_back(Pending{
            std::make_unique<Buffer>(instance,0,info.guest_size+256,MemoryType::HostUncached),
            std::make_unique<Buffer>(instance,0,info.guest_size,MemoryType::HostCached),
            std::vector<u8>(info.guest_size), bits, block, round});
        auto& input = *entry.input;
        auto& readback = *entry.readback;
        auto& expected = entry.expected;
        for(u32 m=0;m<4;++m) {
            auto p=info.mips_layout[m];u32 w=p.pitch/scale,h=p.height/scale;
            for(u32 layer=0;layer<2;++layer) for(u32 y=0;y<h;++y) for(u32 x=0;x<w;++x) {
                u32 linear=p.offset+((layer*h+y)*w+x)*bytes;
                u32 tiled=p.offset+(layer*w*h+((y/8)*(w/8)+x/8)*64+Morton(x%8,y%8))*bytes;
                for(u32 b=0;b<bytes;++b) {
                    u8 value=(x*13+y*37+layer*61+m*43+b*71+round*97)%251;
                    expected[linear+b]=value;input.mapped_data[256+tiled+b]=value;
                }
            }
        }
        input.Flush(0,input.SizeBytes());
        const auto [output,offset]=tiler.DetileImage(&input,256,info);
        const vk::BufferCopy copy{offset,0,info.guest_size};
        if (block) {
            info.size = {128 * scale, 64 * scale, 1};
            info.pitch = info.size.width;
            info.type = AmdGpu::ImageType::Color2DArray;
            info.pixel_format = bits == 64 ? vk::Format::eBc1RgbaUnormBlock
                                          : vk::Format::eBc3UnormBlock;
            entry.image = std::make_unique<Image>(instance, runtime, views, info);
            std::vector<vk::BufferImageCopy> regions;
            for (u32 m = 0; m < 4; ++m) {
                const auto& mip = info.mips_layout[m];
                regions.push_back({
                    .bufferOffset = offset + mip.offset,
                    .bufferRowLength = mip.pitch,
                    .bufferImageHeight = mip.height,
                    .imageSubresource = {vk::ImageAspectFlagBits::eColor, m, 0, 2},
                    .imageExtent = {info.size.width >> m, info.size.height >> m, 1},
                });
            }
            runtime.UploadImage(entry.image.get(), output, regions);
            for (auto& region : regions) region.bufferOffset -= offset;
            runtime.DownloadImage(entry.image.get(), &readback, regions);
        } else {
            runtime.CopyBuffer(output,&readback,std::span{&copy,1});
        }
    }
    // Keep every source/destination alive and queue different payloads before waiting.
    // This exercises staging suballocations and descriptor lifetimes across dispatches.
    scheduler.Finish();
    for (const auto& entry : pending) {
        auto& readback = *entry.readback;
        const auto& expected = entry.expected;
        const auto bits = entry.bits;
        const auto block = entry.block;
        readback.Invalidate(0,expected.size());
        size_t errors=0;
        for(size_t i=0;i<expected.size();++i) if(readback.mapped_data[i]!=expected[i]) {
            if(errors++<4) std::fprintf(stderr,"bits=%u block=%d offset=%zu got=%u expected=%u\n",bits,block,i,readback.mapped_data[i],expected[i]);
        }
        assert(errors==0);checked+=expected.size();
        if (rounds == 1) std::printf("PASS detile %u-bit block=%d: 4 mips, 2 layers, %zu bytes\n",bits,block,expected.size());
    }
    std::printf("PASS production detiler: %zu bytes, %zu queued dispatches verified against synthetic Morton layout (BC1/BC3 image roundtrip included)\n",checked,pending.size());
}
