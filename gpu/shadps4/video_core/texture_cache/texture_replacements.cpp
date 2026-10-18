// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
#include <array>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <xxhash.h>
#include "video_core/texture_cache/texture_replacements.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace VideoCore {
namespace {
u64 DimensionKey(u32 w,u32 h,u32 block,u32 layers) {
    return u64(w)<<40 | u64(h)<<24 | u64(block)<<8 | layers;
}
u32 SourceBlock(vk::Format f) {
    switch (f) {
    case vk::Format::eBc1RgbaUnormBlock:case vk::Format::eBc1RgbaSrgbBlock:
    case vk::Format::eBc1RgbUnormBlock:case vk::Format::eBc1RgbSrgbBlock:return 8;
    case vk::Format::eBc3UnormBlock:case vk::Format::eBc3SrgbBlock:return 16;
    case vk::Format::eR8G8B8A8Unorm:case vk::Format::eR8G8B8A8Srgb:return 4;
    default:return 0;
    }
}
}
TextureReplacements::TextureReplacements(const Vulkan::Instance& i,Vulkan::Runtime& r)
    :instance(i),runtime(r) {
    const char* directory=std::getenv("GOW3_TEXTURE_PACK");
    if (!directory || !*directory) return;
    if (const char* value=std::getenv("GOW3_TEXTURE_PACK_CONTROL")) control=value;
    // A bounded opt-in experiment: no recursive paths, no write to guest memory or the dump.
    std::error_code ec;
    u64 bytes=0;
    for (const auto& path:std::filesystem::directory_iterator(directory,ec)) {
        if (path.path().extension()!=".g3tx" || packs.size()>=2048) continue;
        const auto size=path.file_size(ec);
        if (ec || size>128_MB || size<40 || bytes+size>4_GB) continue;
        std::array<u8,64> header{};
        std::ifstream f(path.path(),std::ios::binary);
        f.read(reinterpret_cast<char*>(header.data()),std::min<u64>(size,header.size()));
        auto e=std::make_unique<Entry>();
        if (!f || !ReplacementData::DecodeHeader(header,size,e->data)) {
            std::printf("Texture pack rejected: %s\n",path.path().filename().string().c_str());
            continue;
        }
        if (e->data.encoding==2 &&
            (!instance.IsFormatSupported(vk::Format::eAstc4x4UnormBlock,vk::FormatFeatureFlagBits2::eSampledImage|vk::FormatFeatureFlagBits2::eTransferDst) ||
             !instance.IsFormatSupported(vk::Format::eAstc4x4SrgbBlock,vk::FormatFeatureFlagBits2::eSampledImage))) continue;
        e->data.original.resize(e->data.original_size);
        f.seekg(e->data.payload_offset-e->data.original_size);
        if (!f.read(reinterpret_cast<char*>(e->data.original.data()),e->data.original.size())) continue;
        e->name=path.path().stem().string();e->path=path.path().string();
        e->hash=XXH3_64bits(e->data.original.data(),e->data.original.size());
        dimensions.insert(DimensionKey(e->data.width,e->data.height,e->data.block,e->data.layers));
        by_hash.emplace(e->hash,e.get());
        bytes+=size;packs.push_back(std::move(e));
    }
    std::printf("Texture pack: %zu maps, %.2f MiB on disk; lazy GPU payloads; exact base bytes; 2x sampling\n",
                packs.size(),double(bytes)/(1024*1024));
    PollControl();
}
TextureReplacements::~TextureReplacements() {
    if (!pending.empty()) {
        runtime.GetScheduler().Finish();
        for (auto& p:pending) runtime.GetStagingPool().FreeDeferred(p.staging);
    }
}

bool TextureReplacements::PollControl() {
    const bool retired=Retire();
    if (control.empty() || control_tick++%15) return retired;
    char value=0;std::ifstream f(control);
    if (!f.get(value) || (value!='0' && value!='1')) return retired;
    const bool next=value=='1';
    if (next==active) return retired;
    active=next;std::printf("Texture pack sampling: %s\n",active?"2x":"original");
    return true;
}

void TextureReplacements::Forget(u64 uid) {
    if (!Enabled()) return;
    std::scoped_lock lock{mutex}; sources.erase(uid);
}

void TextureReplacements::ObserveUpload(Image& image) {
    if (!Enabled()) return;
    Forget(image.image_uid);
    const auto& info=image.info;
    static const bool diag=std::getenv("GOW3_TEXTURE_PACK_DIAG")!=nullptr;
    static u32 notes=0;
    if (diag && notes++<512 && info.props.is_block) {
        std::printf("Texture pack source: uid %llu %ux%u format %u tile %u array %u flags %x usage %u%u%u bits %u\n",
            (unsigned long long)image.image_uid,info.size.width,info.size.height,u32(info.pixel_format),
            u32(info.tile_mode),u32(info.array_mode),u32(image.flags),image.usage.storage,
            image.usage.render_target,image.usage.depth_target,info.num_bits);
    }
    if (!info.guest_address || info.props.is_volume ||
        info.props.is_depth || (info.resources.layers!=1 && info.resources.layers!=6) || info.num_samples!=1 ||
        info.guest_samples!=1 || image.usage.storage || image.usage.render_target ||
        image.usage.depth_target || True(image.flags&ImageFlagBits::GpuModified)) return;
    const u32 block=SourceBlock(info.pixel_format),layers=info.resources.layers;
    if (!block || !dimensions.contains(DimensionKey(info.size.width,info.size.height,block,layers))) return;
    // Identify what was actually uploaded, including GPU-written/staged sources. Read only
    // tightly packed BC blocks; never use a potentially stale CPU copy to select a pack.
    // Retirement is asynchronous, bounded, and checked against this upload's revision.
    const u64 bytes=(block==4?u64(info.size.width)*info.size.height*4:
        u64((info.size.width+3)/4)*((info.size.height+3)/4)*block)*layers;
    if (pending.size()>=64 || pending_bytes+bytes>32_MB) return;
    auto staging=runtime.GetStagingPool().Request(bytes,MemoryType::HostCached,16,true);
    const vk::BufferImageCopy copy{.bufferOffset=staging.offset,
        .imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,layers},
        .imageExtent={info.size.width,info.size.height,1}};
    runtime.DownloadImage(&image,staging.buffer,std::span{&copy,1});
    const u64 serial=++revision;
    {
        std::scoped_lock lock{mutex};
        sources[image.image_uid]={image.backing,nullptr,image.alias_generation,serial};
    }
    pending.push_back({image.image_uid,serial,runtime.GetScheduler().CurrentTick(),
                       info.size.width,info.size.height,block,layers,staging});
    pending_bytes+=bytes;
}

bool TextureReplacements::Retire() {
    bool changed=false;
    for (size_t i=0;i<pending.size();) {
        const auto p=pending[i];
        if (!runtime.GetScheduler().IsFree(p.tick)) {++i;continue;}
        p.staging.Invalidate();
        const auto base=std::span{p.staging.mapped,size_t(p.staging.size)};
        const u64 hash=XXH3_64bits(base.data(),base.size());
        {
            std::scoped_lock lock{mutex};auto it=sources.find(p.uid);
            if (it!=sources.end() && it->second.revision==p.revision) {
                const auto [begin,end]=by_hash.equal_range(hash);
                for (auto entry=begin;entry!=end;++entry) {
                    auto* e=entry->second;
                    if (e->data.width!=p.width || e->data.height!=p.height ||
                        e->data.block!=p.block || e->data.layers!=p.layers ||
                        !std::ranges::equal(e->data.original,base)) continue;
                    it->second.pack=e;
                    std::printf("Texture pack matched: %s uid %llu %ux%u -> %ux%u layers %u (GPU bytes verified)\n",
                        e->name.c_str(),(unsigned long long)p.uid,p.width,p.height,
                        e->data.out_width,e->data.out_height,p.layers);
                    break;
                }
            }
        }
        // Retired misses also release queue capacity. Revisit descriptors that could not
        // enqueue earlier, rather than permanently leaving startup textures unmatched.
        changed=true;
        runtime.GetStagingPool().FreeDeferred(p.staging);pending_bytes-=p.staging.size;
        pending[i]=pending.back();pending.pop_back();
    }
    return changed;
}

bool TextureReplacements::Upload(Entry& e) {
    if (e.failed || resident_bytes+e.data.payload_size>4_GB) return false;
    std::ifstream input(e.path,std::ios::binary);
    if (!input.seekg(e.data.payload_offset)) {e.failed=true;return false;}
    auto staging=runtime.GetStagingPool().Request(e.data.payload_size,MemoryType::HostUncached,16);
    if (!input.read(reinterpret_cast<char*>(staging.mapped),e.data.payload_size)) {
        e.failed=true;std::printf("Texture pack payload unreadable: %s\n",e.name.c_str());return false;
    }
    staging.Flush();
    const bool compressed=e.data.encoding==2;
    auto& scheduler=runtime.GetScheduler();
    scheduler.EndRendering(); runtime.FlushBarriers();
    e.image.device=instance.GetDevice(); e.image.allocator=instance.GetAllocator();
    e.image.Create({.flags=vk::ImageCreateFlagBits::eMutableFormat,
        .imageType=vk::ImageType::e2D,.format=compressed?vk::Format::eAstc4x4UnormBlock:vk::Format::eR8G8B8A8Unorm,
        .extent={e.data.out_width,e.data.out_height,1},.mipLevels=e.data.levels,
        .arrayLayers=e.data.layers,.samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
        .usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst,
        .sharingMode=vk::SharingMode::eExclusive,.initialLayout=vk::ImageLayout::eUndefined});
    std::vector<vk::BufferImageCopy> regions;
    u64 offset=staging.offset;
    for (u32 m=0;m<e.data.levels;++m) {
        const u32 w=std::max(e.data.out_width>>m,1u),h=std::max(e.data.out_height>>m,1u);
        regions.push_back({.bufferOffset=offset,
            .imageSubresource={vk::ImageAspectFlagBits::eColor,m,0,e.data.layers},.imageExtent={w,h,1}});
        offset+=ReplacementData::MipBytes(e.data,w,h);
    }
    const auto image=e.image.image;
    const auto buffer=staging.buffer->Handle();
    const u32 layers=e.data.layers;
    scheduler.Record([image,buffer,layers,regions=std::move(regions)](vk::CommandBuffer cmd) {
        vk::ImageMemoryBarrier2 barrier{
            .srcStageMask=vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask=vk::PipelineStageFlagBits2::eCopy,
            .dstAccessMask=vk::AccessFlagBits2::eTransferWrite,
            .oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=image,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,VK_REMAINING_MIP_LEVELS,0,layers}};
        vk::DependencyInfo dep{.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&barrier};
        cmd.pipelineBarrier2(dep);
        cmd.copyBufferToImage(buffer,image,vk::ImageLayout::eTransferDstOptimal,regions);
        barrier.srcStageMask=vk::PipelineStageFlagBits2::eCopy;
        barrier.srcAccessMask=vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands;
        barrier.dstAccessMask=vk::AccessFlagBits2::eShaderRead;
        barrier.oldLayout=vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout=vk::ImageLayout::eShaderReadOnlyOptimal;
        cmd.pipelineBarrier2(dep);
    });
    runtime.AccessBuffer(staging.buffer,staging.offset,staging.size,
                         vk::PipelineStageFlagBits2::eCopy,vk::AccessFlagBits2::eTransferRead);
    std::printf("Texture pack uploaded: %s %.2f MiB GPU, %u mips\n",e.name.c_str(),
                double(e.image.size_bytes)/(1024*1024),e.data.levels);
    resident_bytes+=e.data.payload_size;return true;
}

std::optional<vk::ImageView> TextureReplacements::Sample(
    Image& image,const ImageViewInfo& view,bool native) {
    if (!Enabled() || !active || native || view.is_storage || view.min_lod ||
        (view.type!=AmdGpu::ImageType::Color2D && view.type!=AmdGpu::ImageType::Color2DArray) || view.range.base.level ||
        !view.range.extent.layers || view.range.base.layer+view.range.extent.layers>image.info.resources.layers || view.range.extent.levels!=image.info.resources.levels ||
        image.binding.is_target || image.binding.force_general || image.usage.storage ||
        image.usage.render_target || image.usage.depth_target ||
        True(image.flags&(ImageFlagBits::Dirty|ImageFlagBits::GpuModified))) return {};
    Entry* p=nullptr;bool needs_observe=false;
    {
        std::scoped_lock lock{mutex};const auto it=sources.find(image.image_uid);
        needs_observe=it==sources.end() || it->second.backing!=image.backing ||
                      it->second.alias_generation!=image.alias_generation;
        if (!needs_observe) p=it->second.pack;
    }
    if (needs_observe) {ObserveUpload(image);return {};}
    if (!p) return {};
    const auto f=view.format;
    const bool srgb=f==vk::Format::eBc1RgbaSrgbBlock || f==vk::Format::eBc1RgbSrgbBlock ||
                    f==vk::Format::eBc3SrgbBlock;
    const bool is_srgb=srgb || f==vk::Format::eR8G8B8A8Srgb;
    if (SourceBlock(f)!=p->data.block) return {};
    if (!p->image && !Upload(*p)) return {};
    for (const auto& [key,v]:p->views) if (key==view) return *v;
    auto mapping=view.mapping;
    if (f==vk::Format::eBc1RgbUnormBlock || f==vk::Format::eBc1RgbSrgbBlock) {
        // BC1 RGB has implicit alpha 1, even if the same blocks encode transparent
        // black in an RGBA view. Preserve this before applying the guest swizzle.
        for (auto* channel:{&mapping.r,&mapping.g,&mapping.b,&mapping.a}) {
            if (*channel==vk::ComponentSwizzle::eA ||
                (channel==&mapping.a && *channel==vk::ComponentSwizzle::eIdentity))
                *channel=vk::ComponentSwizzle::eOne;
        }
    }
    auto [result,v]=instance.GetDevice().createImageViewUnique({
        .image=p->image.image,.viewType=view.type==AmdGpu::ImageType::Color2DArray?vk::ImageViewType::e2DArray:vk::ImageViewType::e2D,
        .format=p->data.encoding==2?(is_srgb?vk::Format::eAstc4x4SrgbBlock:vk::Format::eAstc4x4UnormBlock):
            (is_srgb?vk::Format::eR8G8B8A8Srgb:vk::Format::eR8G8B8A8Unorm),
        .components=mapping,
        .subresourceRange={vk::ImageAspectFlagBits::eColor,0,p->data.levels,view.range.base.layer,view.range.extent.layers}});
    if (result!=vk::Result::eSuccess) return {};
    const auto handle=*v; p->views.emplace_back(view,std::move(v)); return handle;
}
}
