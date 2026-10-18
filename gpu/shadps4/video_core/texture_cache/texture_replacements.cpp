// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <xxhash.h>
#include "video_core/texture_cache/texture_replacements.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace VideoCore {
TextureReplacements::TextureReplacements(const Vulkan::Instance& i,Vulkan::Runtime& r)
    :instance(i),runtime(r) {
    const char* directory=std::getenv("GOW3_TEXTURE_PACK");
    if (!directory || !*directory) return;
    if (const char* value=std::getenv("GOW3_TEXTURE_PACK_CONTROL")) control=value;
    // A bounded opt-in experiment: no recursive paths, no write to guest memory or the dump.
    std::error_code ec;
    u64 bytes=0;
    for (const auto& path:std::filesystem::directory_iterator(directory,ec)) {
        if (path.path().extension()!=".g3tx" || packs.size()>=64) continue;
        const auto size=path.file_size(ec);
        if (ec || size>96_MB || size<40 || bytes+size>256_MB) continue;
        std::vector<u8> data(size);
        std::ifstream f(path.path(),std::ios::binary);
        if (!f.read(reinterpret_cast<char*>(data.data()),data.size())) continue;
        auto e=std::make_unique<Entry>();
        if (!ReplacementData::Decode(data,e->data)) {
            std::printf("Texture pack rejected: %s\n",path.path().filename().string().c_str());
            continue;
        }
        e->name=path.path().stem().string();
        e->hash=XXH3_64bits(e->data.original.data(),e->data.original.size());
        bytes+=size; packs.push_back(std::move(e));
    }
    std::printf("Texture pack: %zu color maps, %.2f MiB; exact original blocks; 2x sampling only\n",
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
    if (!info.guest_address || !info.props.is_block || info.props.is_volume ||
        info.props.is_depth || info.resources.layers!=1 || info.num_samples!=1 ||
        info.guest_samples!=1 || image.usage.storage || image.usage.render_target ||
        image.usage.depth_target || True(image.flags&ImageFlagBits::GpuModified)) return;
    const bool bc1=info.pixel_format==vk::Format::eBc1RgbaUnormBlock ||
                   info.pixel_format==vk::Format::eBc1RgbaSrgbBlock ||
                   info.pixel_format==vk::Format::eBc1RgbUnormBlock ||
                   info.pixel_format==vk::Format::eBc1RgbSrgbBlock;
    const bool bc3=info.pixel_format==vk::Format::eBc3UnormBlock ||
                   info.pixel_format==vk::Format::eBc3SrgbBlock;
    if (!bc1 && !bc3) return;
    const u32 block=bc1?8:16;
    if (!std::ranges::any_of(packs,[&](const auto& p) {
        return p->data.width==info.size.width && p->data.height==info.size.height &&
               p->data.block==block;
    })) return;
    // Identify what was actually uploaded, including GPU-written/staged sources. Read only
    // tightly packed BC blocks; never use a potentially stale CPU copy to select a pack.
    // Retirement is asynchronous, bounded, and checked against this upload's revision.
    const u64 bytes=u64((info.size.width+3)/4)*((info.size.height+3)/4)*block;
    if (pending.size()>=64 || pending_bytes+bytes>32_MB) return;
    auto staging=runtime.GetStagingPool().Request(bytes,MemoryType::HostCached,16,true);
    const vk::BufferImageCopy copy{.bufferOffset=staging.offset,
        .imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},
        .imageExtent={info.size.width,info.size.height,1}};
    runtime.DownloadImage(&image,staging.buffer,std::span{&copy,1});
    const u64 serial=++revision;
    {
        std::scoped_lock lock{mutex};
        sources[image.image_uid]={image.backing,nullptr,image.alias_generation,serial};
    }
    pending.push_back({image.image_uid,serial,runtime.GetScheduler().CurrentTick(),
                       info.size.width,info.size.height,block,staging});
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
                for (auto& e:packs) if (e->hash==hash && e->data.width==p.width &&
                    e->data.height==p.height && e->data.block==p.block &&
                    std::ranges::equal(e->data.original,base)) {
                    it->second.pack=e.get();changed=true;
                    std::printf("Texture pack matched: %s uid %llu %ux%u -> %ux%u (GPU bytes verified)\n",
                        e->name.c_str(),(unsigned long long)p.uid,p.width,p.height,
                        e->data.out_width,e->data.out_height);
                    break;
                }
            }
        }
        runtime.GetStagingPool().FreeDeferred(p.staging);pending_bytes-=p.staging.size;
        pending[i]=pending.back();pending.pop_back();
    }
    return changed;
}

void TextureReplacements::Upload(Entry& e) {
    auto& scheduler=runtime.GetScheduler();
    scheduler.EndRendering(); runtime.FlushBarriers();
    e.image.device=instance.GetDevice(); e.image.allocator=instance.GetAllocator();
    e.image.Create({.flags=vk::ImageCreateFlagBits::eMutableFormat,
        .imageType=vk::ImageType::e2D,.format=vk::Format::eR8G8B8A8Unorm,
        .extent={e.data.out_width,e.data.out_height,1},.mipLevels=e.data.levels,
        .arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
        .usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst,
        .sharingMode=vk::SharingMode::eExclusive,.initialLayout=vk::ImageLayout::eUndefined});
    auto staging=runtime.GetStagingPool().Request(e.data.rgba.size(),MemoryType::HostUncached,16);
    std::memcpy(staging.mapped,e.data.rgba.data(),e.data.rgba.size()); staging.Flush();
    std::vector<vk::BufferImageCopy> regions;
    u64 offset=staging.offset;
    for (u32 m=0;m<e.data.levels;++m) {
        const u32 w=std::max(e.data.out_width>>m,1u),h=std::max(e.data.out_height>>m,1u);
        regions.push_back({.bufferOffset=offset,
            .imageSubresource={vk::ImageAspectFlagBits::eColor,m,0,1},.imageExtent={w,h,1}});
        offset+=u64(w)*h*4;
    }
    const auto image=e.image.image;
    const auto buffer=staging.buffer->Handle();
    scheduler.Record([image,buffer,regions=std::move(regions)](vk::CommandBuffer cmd) {
        vk::ImageMemoryBarrier2 barrier{
            .srcStageMask=vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask=vk::PipelineStageFlagBits2::eCopy,
            .dstAccessMask=vk::AccessFlagBits2::eTransferWrite,
            .oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=image,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,VK_REMAINING_MIP_LEVELS,0,1}};
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
}

std::optional<vk::ImageView> TextureReplacements::Sample(
    const Image& image,const ImageViewInfo& view,bool native) {
    if (!Enabled() || !active || native || view.is_storage || view.min_lod ||
        view.type!=AmdGpu::ImageType::Color2D || view.range.base.level || view.range.base.layer ||
        view.range.extent.layers!=1 || view.range.extent.levels!=image.info.resources.levels ||
        image.binding.is_target || image.binding.force_general || image.usage.storage ||
        image.usage.render_target || image.usage.depth_target ||
        True(image.flags&(ImageFlagBits::Dirty|ImageFlagBits::GpuModified))) return {};
    Entry* p=nullptr;
    {
        std::scoped_lock lock{mutex}; const auto it=sources.find(image.image_uid);
        if (it==sources.end() || it->second.backing!=image.backing ||
            it->second.alias_generation!=image.alias_generation) return {};
        p=it->second.pack;
    }
    if (!p) return {};
    const auto f=view.format;
    const bool srgb=f==vk::Format::eBc1RgbaSrgbBlock || f==vk::Format::eBc1RgbSrgbBlock ||
                    f==vk::Format::eBc3SrgbBlock;
    if (!srgb && f!=vk::Format::eBc1RgbaUnormBlock && f!=vk::Format::eBc1RgbUnormBlock &&
        f!=vk::Format::eBc3UnormBlock) return {};
    if (!p->image) Upload(*p);
    for (const auto& [key,v]:p->views) if (key==view) return *v;
    auto [result,v]=instance.GetDevice().createImageViewUnique({
        .image=p->image.image,.viewType=vk::ImageViewType::e2D,
        .format=srgb?vk::Format::eR8G8B8A8Srgb:vk::Format::eR8G8B8A8Unorm,
        .components=view.mapping,
        .subresourceRange={vk::ImageAspectFlagBits::eColor,0,p->data.levels,0,1}});
    if (result!=vk::Result::eSuccess) return {};
    const auto handle=*v; p->views.emplace_back(view,std::move(v)); return handle;
}
}
