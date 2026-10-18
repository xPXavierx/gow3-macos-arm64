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
    assert(argc==2);setenv("GOW3_TEXTURE_PACK",argv[1],1);
    Vulkan::Instance instance(0,false);Vulkan::Scheduler scheduler(instance);
    Vulkan::Runtime runtime(instance,scheduler);
    Common::SlotVector<VideoCore::ImageView> views;
    std::vector<u8> source(512,0x55);
    VideoCore::ImageInfo info;info.size={32,32,1};info.resources={6,1};
    info.type=AmdGpu::ImageType::Color2D;info.props.is_block=1;
    info.pixel_format=vk::Format::eBc1RgbaUnormBlock;info.num_bits=64;
    info.guest_address=reinterpret_cast<u64>(source.data());info.guest_size=512;
    info.mips_layout[0]={512,32,32,0};
    VideoCore::Image image(instance,runtime,views,info);
    image.flags=VideoCore::ImageFlagBits::Registered;
    VideoCore::TextureReplacements pack(instance,runtime);assert(pack.Enabled());
    VideoCore::ImageViewInfo view;view.format=info.pixel_format;view.range.extent={6,1};
    const auto upload=[&] {
        auto s=runtime.GetStagingPool().Request(source.size(),VideoCore::MemoryType::HostUncached,16);
        std::memcpy(s.mapped,source.data(),source.size());s.Flush();
        const vk::BufferImageCopy copy{.bufferOffset=s.offset,
            .imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={32,32,1}};
        runtime.UploadImage(&image,s.buffer,std::span{&copy,1});
        pack.ObserveUpload(image);scheduler.Finish();pack.PollControl();
    };
    upload();assert(pack.Sample(image,view,false));
    assert(!pack.Sample(image,view,true));
    auto srgb=view;srgb.format=vk::Format::eBc1RgbaSrgbBlock;
    assert(pack.Sample(image,srgb,false));
    srgb.mapping.r=vk::ComponentSwizzle::eB;assert(pack.Sample(image,srgb,false));
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
    std::puts("Texture replacements: real Vulkan upload, sRGB/swizzle views, CPU/GPU/alias invalidation, native-coordinate fallback: PASS");
}
