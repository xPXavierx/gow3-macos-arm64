// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic BC1/RGBA fixture only; no game data. Real Vulkan allocation/upload/view path.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "common/slot_vector.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_replacements.h"
#include "video_core/texture_cache/blit_helper.h"
int main(int argc,char** argv) {
    assert(argc>=2);setenv("GOW3_TEXTURE_PACK",argv[1],1);
    Vulkan::Instance instance(0,false);Vulkan::Scheduler scheduler(instance);
    if (std::string(argv[1])=="--formats") {
        for (auto f:{vk::Format::eAstc4x4UnormBlock,vk::Format::eAstc4x4SrgbBlock}) {
            const bool supported=instance.IsFormatSupported(f,vk::FormatFeatureFlagBits2::eSampledImage|vk::FormatFeatureFlagBits2::eTransferDst);
            std::printf("ASTC format %u supported %u\n",u32(f),supported);
            assert(supported);
        }
        return 0;
    }
    Vulkan::Runtime runtime(instance,scheduler);
    Common::SlotVector<VideoCore::ImageView> views;
    const u32 layers=argc>2 && std::string(argv[2])=="cube"?6:1;
    const bool raw=argc>2 && std::string(argv[2])=="rgba";
    const u32 base_size=raw?4096:512;
    std::vector<u8> source(base_size*layers,0x55);
    VideoCore::ImageInfo info;info.size={32,32,1};info.resources={6,layers};
    info.type=AmdGpu::ImageType::Color2D;info.props.is_block=!raw;
    info.pixel_format=raw?vk::Format::eR8G8B8A8Unorm:vk::Format::eBc1RgbaUnormBlock;info.num_bits=raw?32:64;
    info.guest_address=reinterpret_cast<u64>(source.data());info.guest_size=source.size();
    info.mips_layout[0]={base_size,32,32,0};
    VideoCore::Image image(instance,runtime,views,info);
    image.flags=VideoCore::ImageFlagBits::Registered;
    VideoCore::TextureReplacements pack(instance,runtime);assert(pack.Enabled());
    VideoCore::ImageViewInfo view;view.format=info.pixel_format;view.range.extent={6,layers};
    if (layers==6) view.type=AmdGpu::ImageType::Color2DArray;
    const auto upload=[&] {
        auto s=runtime.GetStagingPool().Request(source.size(),VideoCore::MemoryType::HostUncached,16);
        std::memcpy(s.mapped,source.data(),source.size());s.Flush();
        const vk::BufferImageCopy copy{.bufferOffset=s.offset,
            .imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,layers},.imageExtent={32,32,1}};
        runtime.UploadImage(&image,s.buffer,std::span{&copy,1});
        pack.ObserveUpload(image);scheduler.Finish();pack.PollControl();
    };
    upload();assert(pack.Sample(image,view,false));
    assert(!pack.Sample(image,view,true));
    auto srgb=view;srgb.format=raw?vk::Format::eR8G8B8A8Srgb:vk::Format::eBc1RgbaSrgbBlock;
    assert(pack.Sample(image,srgb,false));
    srgb.mapping.r=vk::ComponentSwizzle::eB;assert(pack.Sample(image,srgb,false));
    if (layers==6) {
        auto face=view;face.type=AmdGpu::ImageType::Color2D;face.range.base.layer=3;face.range.extent.layers=1;
        assert(pack.Sample(image,face,false));
    }
    image.flags|=VideoCore::ImageFlagBits::CpuDirty;assert(!pack.Sample(image,view,false));
    image.flags=VideoCore::ImageFlagBits::Registered|VideoCore::ImageFlagBits::GpuModified;
    assert(!pack.Sample(image,view,false));
    image.flags=VideoCore::ImageFlagBits::Registered; ++image.alias_generation;
    assert(!pack.Sample(image,view,false));
    --image.alias_generation;
    image.flags=VideoCore::ImageFlagBits::Registered;
    image.usage.storage=1;assert(!pack.Sample(image,view,false));image.usage.storage=0;
    source[0]^=1;upload();assert(!pack.Sample(image,view,false));
    source[0]^=1;upload();assert(pack.Sample(image,view,false));
    // A match still in flight must not publish after a newer upload or destruction.
    pack.ObserveUpload(image);source[0]^=1;upload();assert(!pack.Sample(image,view,false));
    source[0]^=1;upload();assert(pack.Sample(image,view,false));
    pack.ObserveUpload(image);pack.Forget(image.image_uid);scheduler.Finish();pack.PollControl();
    assert(!pack.Sample(image,view,false));
    scheduler.Finish();
    // Startup can exceed the bounded readback queue. A sampled image that could not
    // enqueue must be retried once earlier requests retire, including queue misses.
    std::vector<std::unique_ptr<VideoCore::Image>> burst;
    for (unsigned n=0;n<65;++n) {
        auto extra=std::make_unique<VideoCore::Image>(instance,runtime,views,info);
        extra->flags=VideoCore::ImageFlagBits::Registered;
        auto staging=runtime.GetStagingPool().Request(source.size(),VideoCore::MemoryType::HostUncached,16);
        std::memcpy(staging.mapped,source.data(),source.size());staging.Flush();
        const vk::BufferImageCopy copy{.bufferOffset=staging.offset,
            .imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,layers},.imageExtent={32,32,1}};
        runtime.UploadImage(extra.get(),staging.buffer,std::span{&copy,1});
        pack.ObserveUpload(*extra);burst.push_back(std::move(extra));
    }
    scheduler.Finish();assert(pack.PollControl());
    assert(!pack.Sample(*burst.back(),view,false));
    scheduler.Finish();assert(pack.PollControl());
    assert(pack.Sample(*burst.back(),view,false));scheduler.Finish();
    std::puts("Texture replacements: real Vulkan upload, sRGB/swizzle views, CPU/GPU/alias invalidation, native-coordinate fallback: PASS");
}
