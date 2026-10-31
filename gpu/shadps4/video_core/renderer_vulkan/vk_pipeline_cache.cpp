// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <ranges>
#include <string>
#include <unordered_set>
#include "gow3_threads.h"

#include "common/hash.h"
#include "common/thread.h"
#include "common/io_file.h"
#include "common/path_util.h"
#include "core/debug_state.h"
#include "core/emulator_settings.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/recompiler.h"
#include "shader_recompiler/runtime_info.h"
#include "gow3_settings.h"
#include "video_core/renderer_vulkan/vk_framegen_inputs.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/cache_storage.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_depth_stencil_state.h"
#include "video_core/renderer_vulkan/motion_history.h"
#include "video_core/renderer_vulkan/vk_draw_prep.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_lighting_test.h"
#include "video_core/renderer_vulkan/vk_pipeline_serialization.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

using Shader::HwStage;
using Shader::Output;
using Shader::SwStage;

struct AsyncShaderCompilation {
    AsyncJob<std::shared_ptr<AsyncShaderCompilation>> job;
    Shader::Info info;
    Shader::RuntimeInfo runtime{};
    std::vector<u32> code, user_data, geometry_code, binary, flat_snapshot;
    std::unique_ptr<Shader::Pools> pools;
    CompilerPool<Shader::Pools>* pool_cache{};
    std::optional<Shader::IR::Program> ir;
    Shader::StageSpecialization spec;
    Shader::Gcn::FetchShaderSnapshot fetch;
    Shader::Backend::Bindings binding;
    vk::Device device;
    vk::ShaderModule module{};
    size_t permutation{};
    bool initial{};
    ~AsyncShaderCompilation() {
        ir.reset();
        if (pools) {
            pool_cache->Recycle(std::move(pools));
        }
        if (module) {
            device.destroyShaderModule(module);
        }
    }
};

struct AsyncGraphicsCompilation {
    AsyncJob<std::shared_ptr<AsyncGraphicsCompilation>> job;
    PipelineSelection selection;
    std::array<std::unique_ptr<Shader::Info>, MaxShaderStages> infos;
    std::array<std::vector<u32>, MaxShaderStages> user_data;
    std::array<Shader::Gcn::FetchShaderSnapshot, MaxShaderStages> fetch;
    std::array<const Shader::Info*, MaxShaderStages> live_infos;
    GraphicsPipeline::SerializationSupport data;
    std::unique_ptr<GraphicsPipeline> pipeline;
    bool failure_logged = false;
};

// Only immutable, fully specialized snapshots cross this boundary. The GPU thread
// owns publication; workers cannot change live Programs or the rendering cache.
struct PipelineCache::GraphicsLookahead {
    std::mutex mutex;
    std::unordered_set<GraphicsPipelineKey> known;
    std::unordered_map<GraphicsPipelineKey, std::shared_ptr<AsyncGraphicsCompilation>> builds;
    std::atomic<bool> ready{false};
    std::atomic<u64> queued{}, published{}, used{}, ready_on_use{}, failed{}, wait_ns{};
    struct Stage {
        const Program* program;
        std::shared_ptr<AsyncShaderCompilation> build;
    };
    std::vector<Stage> shaders;
    std::atomic<u64> shader_queued{}, shader_published{}, shader_used{}, shader_ready{}, shader_failed{}, shader_wait_ns{};
    static constexpr size_t Capacity = 12;
};

constexpr static auto SpirvVersion1_6 = 0x00010600U;

constexpr static std::array DescriptorHeapSizes = {
    vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 8192},
    vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1024},
    vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1024},
};

static u32 MapOutputs(std::span<Shader::OutputMap, 3> outputs, const AmdGpu::VsOutputControl& ctl) {
    u32 num_outputs = 0;

    if (ctl.vs_out_misc_enable) {
        auto& misc_vec = outputs[num_outputs++];
        misc_vec[0] = ctl.use_vtx_point_size ? Output::PointSize : Output::None;
        misc_vec[1] = ctl.use_vtx_edge_flag
                          ? Output::EdgeFlag
                          : (ctl.use_vtx_gs_cut_flag ? Output::GsCutFlag : Output::None);
        misc_vec[2] =
            ctl.use_vtx_kill_flag
                ? Output::KillFlag
                : (ctl.use_vtx_render_target_idx ? Output::RenderTargetIndex : Output::None);
        misc_vec[3] = ctl.use_vtx_viewport_idx ? Output::ViewportIndex : Output::None;
    }

    if (ctl.vs_out_ccdist0_enable) {
        auto& ccdist0 = outputs[num_outputs++];
        ccdist0[0] = ctl.IsClipDistEnabled(0)
                         ? Output::ClipDist0
                         : (ctl.IsCullDistEnabled(0) ? Output::CullDist0 : Output::None);
        ccdist0[1] = ctl.IsClipDistEnabled(1)
                         ? Output::ClipDist1
                         : (ctl.IsCullDistEnabled(1) ? Output::CullDist1 : Output::None);
        ccdist0[2] = ctl.IsClipDistEnabled(2)
                         ? Output::ClipDist2
                         : (ctl.IsCullDistEnabled(2) ? Output::CullDist2 : Output::None);
        ccdist0[3] = ctl.IsClipDistEnabled(3)
                         ? Output::ClipDist3
                         : (ctl.IsCullDistEnabled(3) ? Output::CullDist3 : Output::None);
    }

    if (ctl.vs_out_ccdist1_enable) {
        auto& ccdist1 = outputs[num_outputs++];
        ccdist1[0] = ctl.IsClipDistEnabled(4)
                         ? Output::ClipDist4
                         : (ctl.IsCullDistEnabled(4) ? Output::CullDist4 : Output::None);
        ccdist1[1] = ctl.IsClipDistEnabled(5)
                         ? Output::ClipDist5
                         : (ctl.IsCullDistEnabled(5) ? Output::CullDist5 : Output::None);
        ccdist1[2] = ctl.IsClipDistEnabled(6)
                         ? Output::ClipDist6
                         : (ctl.IsCullDistEnabled(6) ? Output::CullDist6 : Output::None);
        ccdist1[3] = ctl.IsClipDistEnabled(7)
                         ? Output::ClipDist7
                         : (ctl.IsCullDistEnabled(7) ? Output::CullDist7 : Output::None);
    }

    return num_outputs;
}

const Shader::RuntimeInfo& PipelineCache::BuildRuntimeInfo(PipelineSelection& sel, HwStage stage,
                                                             SwStage l_stage) {
    auto& info = sel.runtime_infos[u32(l_stage)];
    const auto& regs = (*sel.regs);
    const auto BuildCommon = [&](const auto& program) {
        info.props.num_user_data = program.settings.num_user_regs;
        info.props.num_input_vgprs = program.settings.vgpr_comp_cnt;
        info.props.num_allocated_vgprs = program.NumVgprs();
        info.props.fp_denorm_mode32 = program.settings.fp_denorm_mode32;
        info.props.fp_denorm_mode16_64 = program.settings.fp_denorm_mode64;
        info.props.fp_round_mode32 = program.settings.fp_round_mode32;
        info.props.fp_round_mode16_64 = program.settings.fp_round_mode64;
    };
    info.Initialize(stage, l_stage);
#if defined(__APPLE__) && defined(ARCH_ARM64)
    if (stage == HwStage::Fragment || stage == HwStage::Vertex) {
        const auto bit = stage == HwStage::Fragment ? Gow3Toggle::ExplicitRoundedMath
                                                   : Gow3Toggle::ExplicitRoundedVertexMath;
        // Normal sessions keep one strategy for their lifetime. Live shader
        // permutation switches are a diagnostic, only safe in a frozen scene.
        static const bool diagnostic_ab = [] {
            const char* value = std::getenv("GOW3_SHADER_AB_TEST");
            return value && value[0] == '1';
        }();
        info.props.explicit_rounded_math = instance.GetDriverID() == vk::DriverId::eMoltenvk &&
            (!diagnostic_ab || !Gow3Toggle::Disabled(bit));
    }
#endif
    switch (stage) {
    case HwStage::Local: {
        BuildCommon(regs.ls_program);
        Shader::TessellationDataConstantBuffer tess_constants{};
        const auto* hull_info = sel.infos[u32(SwStage::TessellationControl)];
        hull_info->ReadTessConstantBuffer(tess_constants);
        info.hw.ls.ls_stride = tess_constants.ls_stride;
        break;
    }
    case HwStage::Hull:
        BuildCommon(regs.hs_program);
        break;
    case HwStage::Export:
        BuildCommon(regs.es_program);
        info.hw.es.vertex_data_size = regs.vgt_esgs_ring_itemsize;
        break;
    case HwStage::Geometry: {
        BuildCommon(regs.gs_program);
        info.hw.gs.num_outputs = MapOutputs(info.hw.gs.outputs, regs.vs_output_control);
        info.hw.gs.output_vertices = regs.vgt_gs_max_vert_out;
        info.hw.gs.num_invocations =
            regs.vgt_gs_instance_cnt.IsEnabled() ? regs.vgt_gs_instance_cnt.count : 1;
        if (regs.stage_enable.raw == AmdGpu::ShaderStageEnable::LsHsEsGs) {
            info.hw.gs.in_primitive = [&]() {
                switch (regs.tess_config.topology) {
                case AmdGpu::TessellationTopology::Point:
                    return AmdGpu::PrimitiveType::PointList;
                case AmdGpu::TessellationTopology::Line:
                    return AmdGpu::PrimitiveType::LineList;
                case AmdGpu::TessellationTopology::TriangleCw:
                case AmdGpu::TessellationTopology::TriangleCcw:
                    return AmdGpu::PrimitiveType::TriangleList;
                default:
                    UNREACHABLE();
                }
            }();
        } else {
            info.hw.gs.in_primitive = regs.primitive_type;
        }
        for (u32 stream_id = 0; stream_id < Shader::GsMaxOutputStreams; ++stream_id) {
            info.hw.gs.out_primitive[stream_id] =
                regs.vgt_gs_out_prim_type.GetPrimitiveType(stream_id);
        }
        info.hw.gs.in_vertex_data_size = regs.vgt_esgs_ring_itemsize;
        info.hw.gs.out_vertex_data_size = regs.vgt_gs_vert_itemsize[0];
        info.hw.gs.mode = regs.vgt_gs_mode.mode;
        const auto params_vc = AmdGpu::GetParams(regs.vs_program);
        info.hw.gs.vs_copy = params_vc.code;
        info.hw.gs.vs_copy_hash = params_vc.hash;
        DumpShader(info.hw.gs.vs_copy, info.hw.gs.vs_copy_hash, Shader::HwStage::Vertex, 0,
                   "copy.bin");
        break;
    }
    case HwStage::Vertex: {
        BuildCommon(regs.vs_program);
        info.hw.vs.user_clip_plane_mask = regs.clipper_control.user_clip_plane_enable;
        info.hw.vs.num_outputs = MapOutputs(info.hw.vs.outputs, regs.vs_output_control);
        info.hw.vs.emulate_depth_negative_one_to_one =
            !instance.IsDepthClipControlSupported() &&
            regs.clipper_control.clip_space == AmdGpu::ClipSpace::MinusWToW;
        info.hw.vs.clip_disable = regs.IsClipDisabled();
        info.hw.vs.motion_vectors = sel.motion;
        break;
    }
    case HwStage::Fragment: {
        BuildCommon(regs.ps_program);
        info.hw.fs.en_flags = regs.ps_input_ena;
        info.hw.fs.addr_flags = regs.ps_input_addr;
        info.hw.fs.num_inputs = regs.num_interp;
        info.hw.fs.front_face_all_bits = regs.barycentric_control.front_face_all_bits;
        info.hw.fs.depth_before_shader = regs.depth_shader_control.depth_before_shader;
        if (info.hw.fs.depth_before_shader) {
            static std::atomic_flag reported = ATOMIC_FLAG_INIT;
            if (!reported.test_and_set())
                std::puts("Depth before shader: guest early depth/stencil request preserved in shader variants");
        }
        info.hw.fs.num_samples =
            regs.ps_input_addr.sample_coverage_ena && regs.ps_input_ena.sample_coverage_ena
                ? regs.aa_config.NumSamples()
                : 1;
        info.hw.fs.z_export_format = regs.z_export_format;
        info.hw.fs.motion_vectors = sel.motion;
        u8 stencil_ref_export_enable = regs.depth_shader_control.stencil_op_val_export_enable |
                                       regs.depth_shader_control.stencil_test_val_export_enable;
        info.hw.fs.mrtz_mask = regs.depth_shader_control.z_export_enable |
                               (stencil_ref_export_enable << 1) |
                               (regs.depth_shader_control.mask_export_enable << 2) |
                               (regs.depth_shader_control.coverage_to_mask_enable << 3);
        const auto& cb0_blend = regs.blend_control[0];
        if (cb0_blend.enable) {
            info.hw.fs.dual_source_blending =
                LiverpoolToVK::IsDualSourceBlendFactor(cb0_blend.color_dst_factor) ||
                LiverpoolToVK::IsDualSourceBlendFactor(cb0_blend.color_src_factor);
            if (cb0_blend.separate_alpha_blend) {
                info.hw.fs.dual_source_blending |=
                    LiverpoolToVK::IsDualSourceBlendFactor(cb0_blend.alpha_dst_factor) ||
                    LiverpoolToVK::IsDualSourceBlendFactor(cb0_blend.alpha_src_factor);
            }
        } else {
            info.hw.fs.dual_source_blending = false;
        }
        const auto& ps_inputs = regs.ps_inputs;
        for (u32 i = 0; i < regs.num_interp; i++) {
            info.hw.fs.inputs[i] = {
                .param_index = u8(ps_inputs[i].input_offset),
                .is_default = bool(ps_inputs[i].use_default),
                .is_flat = bool(ps_inputs[i].flat_shade),
                .default_value = u8(ps_inputs[i].default_value),
            };
        }
        for (u32 i = 0; i < Shader::MaxColorBuffers; i++) {
            info.hw.fs.color_buffers[i] = sel.graphics_key.color_buffers[i];
        }
        // Lowered user clip planes ride the same emulation path as guest-exported distances, so
        // the fragment side arms whenever the hardware vertex stage lowers them, keeping its input
        // locations in sync with the shifted vertex outputs.
        const bool lowers_user_clip_planes =
            regs.clipper_control.user_clip_plane_enable &&
            !regs.stage_enable.IsStageEnabled(static_cast<u32>(HwStage::Geometry));
        info.hw.fs.clip_distance_emulation =
            ((regs.vs_output_control.clip_distance_enable &&
              !regs.stage_enable.IsStageEnabled(static_cast<u32>(HwStage::Local))) ||
             lowers_user_clip_planes) &&
            profile.needs_clip_distance_emulation;
        break;
    }
    case HwStage::Compute: {
        const auto& cs_pgm = liverpool->GetCsRegs();
        info.props.num_user_data = cs_pgm.settings.num_user_regs;
        info.props.num_allocated_vgprs = cs_pgm.settings.num_vgprs * 4;
        info.props.fp_denorm_mode32 = cs_pgm.settings.fp_denorm_mode32;
        info.props.fp_denorm_mode16_64 = cs_pgm.settings.fp_denorm_mode64;
        info.props.fp_round_mode32 = cs_pgm.settings.fp_round_mode32;
        info.props.fp_round_mode16_64 = cs_pgm.settings.fp_round_mode64;
        info.hw.cs.workgroup_size = {cs_pgm.num_thread_x.full, cs_pgm.num_thread_y.full,
                                     cs_pgm.num_thread_z.full};
        info.hw.cs.tgid_enable = {cs_pgm.IsTgidEnabled(0), cs_pgm.IsTgidEnabled(1),
                                  cs_pgm.IsTgidEnabled(2)};
        info.hw.cs.shared_memory_size = cs_pgm.SharedMemSize();
        break;
    }
    default:
        break;
    }
    switch (l_stage) {
    case SwStage::Vertex:
        info.sw.vs.step_rate_0 = regs.vgt_instance_step_rate_0;
        info.sw.vs.step_rate_1 = regs.vgt_instance_step_rate_1;
        info.sw.vs.vertex_sgpr_offset = sel.draw_indirect_params.vertex_sgpr_offset;
        info.sw.vs.instance_sgpr_offset = sel.draw_indirect_params.instance_sgpr_offset;
        info.sw.vs.tess_emulated_primitive =
            regs.primitive_type == AmdGpu::PrimitiveType::RectList ||
            regs.primitive_type == AmdGpu::PrimitiveType::QuadList;
        break;
    case SwStage::TessellationControl: {
        info.sw.tcs.num_input_control_points = regs.ls_hs_config.hs_input_control_points;
        info.sw.tcs.num_threads = regs.ls_hs_config.hs_output_control_points;
        info.sw.tcs.tess_type = regs.tess_config.type;
        info.sw.tcs.offchip_lds_enable = regs.hs_program.settings.oc_lds_en;
        break;
    }
    case SwStage::TessellationEval: {
        info.sw.tes.tess_type = regs.tess_config.type;
        info.sw.tes.tess_topology = regs.tess_config.topology;
        info.sw.tes.tess_partitioning = regs.tess_config.partitioning;
        break;
    }
    default:
        break;
    }
    return info;
}

PipelineCache::PipelineCache(const Instance& instance_, Scheduler& scheduler_,
                             AmdGpu::Liverpool* liverpool_, u32 sparse_page_shift)
    : instance{instance_}, scheduler{scheduler_}, liverpool{liverpool_},
      desc_heap{instance, scheduler.GetWorkSemaphore(), DescriptorHeapSizes} {
    sel.regs = &liverpool->regs;
    ASSERT_MSG(instance.GetPhysicalDevice().getProperties().limits.maxPushConstantsSize >= sizeof(Shader::PushData),
               "Device does not support the logical image dimension push constants");
    const auto& vk12_props = instance.GetVk12Properties();
    profile = Shader::Profile{
        .max_viewport_width = instance.GetMaxViewportWidth(),
        .max_viewport_height = instance.GetMaxViewportHeight(),
        .max_shared_memory_size = instance.MaxComputeSharedMemorySize(),
        .supported_spirv = SpirvVersion1_6,
        .subgroup_size = instance.SubgroupSize(),
        .sparse_page_shift = sparse_page_shift,
        .manual_buffer_bounds = !instance.IsRobustBufferAccess2Supported(),
        .native_constant_buffers = instance.GetDriverID() == vk::DriverId::eMoltenvk &&
            instance.UniformMinAlignment() <= 128 && instance.StorageMinAlignment() <= 128,
        .manual_null_images = !instance.IsNullDescriptorSupported(),
        .support_int8 = instance.IsShaderInt8Supported(),
        .support_int16 = instance.IsShaderInt16Supported(),
        .support_int64 = instance.IsShaderInt64Supported(),
        .support_float16 = instance.IsShaderFloat16Supported(),
        .support_float64 = instance.IsShaderFloat64Supported(),
        .supports_denorm_behavior_independence =
            vk12_props.denormBehaviorIndependence != vk::ShaderFloatControlsIndependence::eNone,
        .supports_rounding_mode_independence =
            vk12_props.roundingModeIndependence != vk::ShaderFloatControlsIndependence::eNone,
        .support_fp16_denorm_preserve = bool(vk12_props.shaderDenormPreserveFloat16),
        .support_fp16_denorm_flush = bool(vk12_props.shaderDenormFlushToZeroFloat16),
        .support_fp16_round_to_zero = bool(vk12_props.shaderRoundingModeRTZFloat16),
        .support_fp32_denorm_preserve = bool(vk12_props.shaderDenormPreserveFloat32),
        .support_fp32_denorm_flush = bool(vk12_props.shaderDenormFlushToZeroFloat32),
        .support_fp32_round_to_zero = bool(vk12_props.shaderRoundingModeRTZFloat32),
        .support_fp64_denorm_preserve = bool(vk12_props.shaderDenormPreserveFloat64),
        .support_fp64_denorm_flush = bool(vk12_props.shaderDenormFlushToZeroFloat64),
        .support_fp64_round_to_zero = bool(vk12_props.shaderRoundingModeRTZFloat64),
        .support_fp16_signed_zero_inf_nan_preserve =
            bool(vk12_props.shaderSignedZeroInfNanPreserveFloat16),
        .support_fp32_signed_zero_inf_nan_preserve =
            bool(vk12_props.shaderSignedZeroInfNanPreserveFloat32),
        .support_fp64_signed_zero_inf_nan_preserve =
            bool(vk12_props.shaderSignedZeroInfNanPreserveFloat64),
        .supports_image_load_store_lod = instance_.IsImageLoadStoreLodSupported(),
        .supports_native_cube_calc = instance_.IsAmdGcnShaderSupported(),
        .supports_trinary_minmax = instance_.IsAmdShaderTrinaryMinMaxSupported(),
        .supports_buffer_fp32_atomic_min_max =
            instance_.IsShaderAtomicFloatBuffer32MinMaxSupported(),
        .supports_image_fp32_atomic_min_max = instance_.IsShaderAtomicFloatImage32MinMaxSupported(),
        .supports_buffer_int64_atomics = instance_.IsBufferInt64AtomicsSupported(),
        .supports_shared_int64_atomics = instance_.IsSharedInt64AtomicsSupported(),
        .supports_workgroup_explicit_memory_layout =
            instance_.IsWorkgroupMemoryExplicitLayoutSupported(),
        .supports_amd_shader_explicit_vertex_parameter =
            instance_.IsAmdShaderExplicitVertexParameterSupported(),
        .supports_fragment_shader_barycentric = instance_.IsFragmentShaderBarycentricSupported(),
        .needs_single_barycentric_mode = instance.GetDriverID() == vk::DriverId::eMoltenvk,
        .supports_shader_subgroup_clock = instance_.IsShaderSubgroupClockSupported(),
        .needs_manual_interpolation = instance.IsFragmentShaderBarycentricSupported() &&
                                      instance.GetDriverID() == vk::DriverId::eNvidiaProprietary,
        // gow3: older NVIDIA (Pascal) has no barycentrics; GOW3_INTERP_INT_FIX=0/1 overrides.
        .needs_integer_interpolation_fix = [&] {
            if (const char* env = std::getenv("GOW3_INTERP_INT_FIX")) {
                return env[0] == '1';
            }
            return !instance.IsFragmentShaderBarycentricSupported() &&
                   instance.GetDriverID() == vk::DriverId::eNvidiaProprietary;
        }(),
        .needs_lds_barriers = instance.GetDriverID() == vk::DriverId::eNvidiaProprietary ||
                              instance.GetDriverID() == vk::DriverId::eMesaKosmickrisp,
        .needs_buffer_offsets = instance.StorageMinAlignment() > 4,
        .needs_unorm_fixup = instance.GetDriverID() == vk::DriverId::eMesaKosmickrisp,
        .needs_clip_distance_emulation = instance.GetDriverID() == vk::DriverId::eNvidiaProprietary,
        .supports_shader_stencil_export = instance_.IsShaderStencilExportSupported(),
        .dref_broadcast = LightingTest::Current().dref,
    };

    auto [cache_result, cache] = instance.GetDevice().createPipelineCacheUnique({});
    ASSERT_MSG(cache_result == vk::Result::eSuccess, "Failed to create pipeline cache: {}",
               vk::to_string(cache_result));
    pipeline_cache = std::move(cache);
    // gow3: the compiler threads always exist; the in-game menu switches async compilation live
    // (Gow3Settings::async_shaders, GOW3_ASYNC_SHADERS at start).
    {
        const auto count = std::clamp(Gow3Threads::Available() / 2, 1u, 6u);
        const auto capture_count = std::min(2u, std::max(1u, Gow3Threads::Available() / 4));
        for (unsigned i = 0; i < count; ++i) {
            auto [result, worker_cache] = instance.GetDevice().createPipelineCacheUnique({});
            ASSERT_MSG(result == vk::Result::eSuccess, "Failed to create compiler pipeline cache");
            worker_pipeline_caches.push_back(std::move(worker_cache));
        }
        capture_compiler = std::make_unique<AsyncCompiler>(capture_count, 8, [](unsigned index) {
            Common::SetCurrentThreadName(("gow3:ShaderCapture" + std::to_string(index)).c_str());
            Common::SetCurrentThreadPriority(Common::ThreadPriority::High);
        });
        compiler = std::make_unique<AsyncCompiler>(count, 64, [](unsigned index) {
            Common::SetCurrentThreadName(("gow3:ShaderCompile" + std::to_string(index)).c_str());
            Common::SetCurrentThreadPriority(AllowsIncompleteDraws()
                ? Common::ThreadPriority::Low : Common::ThreadPriority::High);
        });
        LOG_INFO(Render_Vulkan, "gow3: async graphics compilation ready ({} capture, {} compile workers)",
                 capture_count, count);
    }
    if (std::getenv("GOW3_COMMUNITY_RECORD_DIR"))
        community_recorder = std::make_unique<AsyncCompiler>(1, 1024, [](unsigned) {
            Common::SetCurrentThreadName("gow3:CatalogRecorder");
            Common::SetCurrentThreadPriority(Common::ThreadPriority::Low);
        });
    std::printf("Shader policy: %s\n", AllowsIncompleteDraws()
        ? "skip pending draws; background warm-up (comparison)"
        : "complete required draws; prewarm known pipelines before play");
    const char* ahead = std::getenv("GOW3_SHADER_LOOKAHEAD");
    if (!AllowsIncompleteDraws() && (!ahead || std::strcmp(ahead, "0") != 0) &&
        !EmulatorSettings.IsShaderCollect() && !EmulatorSettings.IsPatchShaders()) {
        lookahead = std::make_unique<GraphicsLookahead>();
    }
    std::printf("Pipeline look-ahead: %s\n", lookahead ? "enabled (validated command snapshots)" : "disabled");
}

PipelineCache::~PipelineCache() {
    StopWarmUp();
    FinishCompilations();
    if (community_recorder) community_recorder->Stop();
}

// gow3: shader/pipeline compile time on the GPU thread, reported by GOW3_FRAME_STATS.
std::atomic<u64> g_gow3_compile_ns;
std::atomic<u32> g_gow3_compiles;
std::atomic<u64> g_gow3_capture_ns, g_gow3_async_ns, g_gow3_compile_wait_ns;
std::atomic<u64> g_gow3_snapshot_ns, g_gow3_guest_wait_ns;
std::atomic<u32> g_gow3_pool_reuses;
std::atomic<u32> g_gow3_pending_draws;
namespace {
struct PhaseTimer {
    std::atomic<u64>& counter;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    bool active = true;
    void Stop() {
        if (!active) {
            return;
        }
        counter += u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now() - start).count());
        active = false;
    }
    ~PhaseTimer() { Stop(); }
};
struct RequiredWaitTimer : PhaseTimer {
    const char* phase;
    u64 hash;
    RequiredWaitTimer(std::atomic<u64>& counter, const char* phase, u64 hash)
        : PhaseTimer{counter}, phase{phase}, hash{hash} {}
    ~RequiredWaitTimer() {
        static const bool trace = std::getenv("GOW3_SHADER_TRACE") != nullptr;
        if (trace) {
            const double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            if (ms >= 2.0) {
                std::printf("Required shader wait: %s %016llx %.3f ms\n", phase,
                            (unsigned long long)hash, ms);
            }
        }
    }
};
struct CompileTimer {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~CompileTimer() {
        g_gow3_compile_ns += u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count());
        ++g_gow3_compiles;
    }
};
} // namespace

bool PipelineCache::QueueShader(Program& program, HwStage stage, SwStage l_stage,
                                 const Shader::ShaderParams& params,
                                 Shader::RuntimeInfo runtime_info,
                                 Shader::Backend::Bindings binding, size_t permutation,
                                 bool initial,
                                 std::optional<Shader::StageSpecialization> specialization) {
    if (!compiler->HasCapacity()) {
        return false;
    }
    PhaseTimer snapshot{g_gow3_snapshot_ns};
    auto build = std::make_shared<AsyncShaderCompilation>();
    build->device = instance.GetDevice();
    build->code.assign(params.code.begin(), params.code.end());
    build->user_data.assign(params.user_data.begin(), params.user_data.end());
    build->info = Shader::Info(stage, l_stage, params);
    build->info.user_data = build->user_data;
    build->runtime = runtime_info;
    if (stage == HwStage::Geometry && !runtime_info.hw.gs.vs_copy.empty()) {
        build->geometry_code.assign(runtime_info.hw.gs.vs_copy.begin(), runtime_info.hw.gs.vs_copy.end());
        build->runtime.hw.gs.vs_copy = build->geometry_code;
    }
    build->binding = binding;
    build->permutation = permutation;
    build->initial = initial;
    if (specialization) {
        build->spec = *specialization;
    }
    build->pool_cache = &compiler_pools;
    snapshot.Stop();
    AsyncJob<bool> capture_job;
    if (!capture_job.Start(*capture_compiler, [this, build, binding] {
        PhaseTimer capture{g_gow3_capture_ns};
        auto [pools, reused] = compiler_pools.Acquire();
        build->pools = std::move(pools);
        if (reused) {
            ++g_gow3_pool_reuses;
        }
        // gow3: the GPU thread waits until all guest reads and specialization are captured.
        build->ir.emplace(Shader::TranslateProgram(build->code, *build->pools, build->info,
                                                   build->runtime, profile));
        build->spec = Shader::StageSpecialization(build->info, build->runtime, profile, binding);
        build->fetch.Capture(build->info);
        return true;
    })) {
        return false;
    }
    {
        RequiredWaitTimer waiting{g_gow3_guest_wait_ns, "guest capture", params.hash};
        capture_job.Wait();
    }
    if (!capture_job.Poll()) {
        if (initial) {
            program.compilation_failed = true;
        } else {
            specialization->info = &program.info;
            program.failed_permutations.push_back(std::move(*specialization));
        }
        LOG_ERROR(Render_Vulkan, "gow3: shader {:#x}/{} capture failed: {}", params.hash,
                  permutation, capture_job.Error());
        return false;
    }
    const bool queued = build->job.Start(*compiler, [this, build] {
        PhaseTimer compile{g_gow3_async_ns};
        Shader::Gcn::ScopedFetchShaderSnapshot snapshot{build->fetch};
        auto spv = Shader::Backend::SPIRV::EmitSPIRV(profile, build->runtime, *build->ir,
                                                    build->binding);
        DumpShader(build->code, build->info.pgm_hash, build->info.hw_stage, build->permutation, "bin");
        DumpShader(spv, build->info.pgm_hash, build->info.hw_stage, build->permutation, "spv");
        auto patch = GetShaderPatch(build->info.pgm_hash, build->info.hw_stage,
                                    build->permutation, "spv");
        build->module = CompileSPV(patch && EmulatorSettings.IsPatchShaders() ? *patch : spv,
                                    instance.GetDevice());
        build->binary = std::move(spv);
        build->ir.reset();
        compiler_pools.Recycle(std::move(build->pools));
        return build;
    }, true);
    if (queued) {
        program.pending = std::move(build);
        pending_programs.push_back(&program);
    }
    return queued;
}

void PipelineCache::PublishShaders(bool wait) {
    for (size_t i = 0; i < pending_programs.size();) {
        auto* program = pending_programs[i];
        const auto hash = program->info.pgm_hash;
        const auto retire = [&] {
            pending_programs[i] = pending_programs.back();
            pending_programs.pop_back();
        };
        auto build = program->pending;
        if (wait) {
            build->job.Wait();
        }
        auto result = build->job.Poll();
        if (!result) {
            if (build->job.Failed()) {
                if (build->initial) {
                    program->compilation_failed = true;
                } else {
                    build->spec.info = &program->info;
                    program->failed_permutations.push_back(std::move(build->spec));
                }
                program->pending.reset();
                LOG_ERROR(Render_Vulkan, "gow3: shader {:#x}/{} compilation failed: {}", hash,
                          build->permutation, build->job.Error());
                retire();
            } else {
                ++i;
            }
            continue;
        }
        const auto perm_hash = HashCombine(hash, build->permutation);
        Vulkan::SetObjectName(instance.GetDevice(), build->module,
                             GetShaderName(build->info.hw_stage, hash, build->permutation));
        ExportShaderRecipe(build->info, build->spec, build->permutation, build->binary);
        RegisterShaderBinary(std::move(build->binary), hash, build->permutation);
        {
            std::unique_lock lock{programs_mutex};
            if (build->initial) {
                program->info = std::move(build->info);
                // user_data is refreshed on the next lookup; no pointer into a completed job survives.
                program->info.user_data = {};
                program->info_template = std::make_unique<Shader::Info>(program->info);
            }
            build->spec.info = &program->info;
            RegisterShaderMeta(program->info, build->spec.fetch_shader_data, build->spec,
                               perm_hash, build->permutation);
            program->AddPermut(build->module, std::move(build->spec));
            build->module = nullptr;
            program->pending.reset();
            NotifyPreparation();
        }
        retire();
    }
}

void PipelineCache::PublishGraphics(bool wait) {
    for (auto it = pending_graphics.begin(); it != pending_graphics.end();) {
        auto build = it.value();
        if (wait) {
            build->job.Wait();
        }
        auto result = build->job.Poll();
        if (!result) {
            if (build->job.Failed() && !build->failure_logged) {
                LOG_ERROR(Render_Vulkan, "gow3: pipeline {:#x} compilation failed: {}",
                          std::hash<GraphicsPipelineKey>{}(it.key()), build->job.Error());
                build->failure_logged = true;
            }
            ++it;
            continue;
        }
        const auto key = it.key();
        build->pipeline->PublishStages(build->live_infos);
        RegisterPipelineData(key, std::hash<GraphicsPipelineKey>{}(key), build->data);
    ExportPipelineRecipe(key, build->data);
        graphics_pipelines.emplace(key, std::move(build->pipeline));
        ++num_new_pipelines;
        it = pending_graphics.erase(it);
    }
}

void PipelineCache::FinishCompilations() {
    if (lookahead) {
        lookahead->ready.store(false, std::memory_order_release);
    }
    if (compiler) {
        capture_compiler->Stop();
        compiler->Stop();
        PublishShaders(true);
        PublishGraphics(true);
        if (lookahead) {
            std::scoped_lock lock{lookahead->mutex};
            for (auto& [key, build] : lookahead->builds) {
                build->job.Wait();
                // Consume unused results too: their future owns a shared_ptr to
                // its build and must not leave an ownership cycle at shutdown.
                build->job.Poll();
            }
            lookahead->builds.clear();
            for (auto& stage : lookahead->shaders) {
                stage.build->job.Wait();
                stage.build->job.Poll();
            }
            lookahead->shaders.clear();
        }
        std::vector<vk::PipelineCache> sources;
        for (const auto& cache : worker_pipeline_caches) {
            sources.push_back(*cache);
        }
        const auto result = instance.GetDevice().mergePipelineCaches(*pipeline_cache, sources);
        if (result != vk::Result::eSuccess) {
            LOG_ERROR(Render_Vulkan, "gow3: compiler cache merge failed: {}", vk::to_string(result));
        }
    }
}

bool PipelineCache::AsyncCompilation() const {
    return compiler && Gow3Settings::Get().async_shaders.load(std::memory_order_relaxed);
}

bool PipelineCache::AllowsIncompleteDraws() {
    static const bool skip = [] {
        const char* policy = std::getenv("GOW3_SHADER_POLICY");
        return policy && std::strcmp(policy, "skip") == 0;
    }();
    return skip;
}

void PipelineCache::SeedWorkerCaches() {
    if (!compiler || worker_caches_seeded) {
        return;
    }
    for (const auto& cache : worker_pipeline_caches) {
        const auto result = instance.GetDevice().mergePipelineCaches(*cache, *pipeline_cache);
        ASSERT_MSG(result == vk::Result::eSuccess, "Failed to seed compiler pipeline cache");
    }
    worker_caches_seeded = true;
    if (lookahead) {
        {
            std::unique_lock lock{programs_mutex};
            for (auto& [hash, program] : program_cache) {
                if (!program->info_template && !program->modules.empty()) {
                    program->info_template = std::make_unique<Shader::Info>(program->info);
                }
            }
        }
        std::scoped_lock lock{lookahead->mutex};
        for (const auto& [key, pipeline] : graphics_pipelines) {
            lookahead->known.insert(key);
        }
        lookahead->ready.store(true, std::memory_order_release);
    }
}

void PipelineCache::ReportCompilerStats() {
    if (program_aliases_ready.load(std::memory_order_acquire)) {
        std::printf("Shader equivalence stats: %llu requests reused, %llu alias requests declined\n",
                    (unsigned long long)program_alias_hits.exchange(0),
                    (unsigned long long)program_alias_rejected.exchange(0));
    }
    if (compiler) {
        std::printf("Compiler stats: capture queue %zu, compile queue %zu; queue waits: capture "
                    "%.1f ms, compile %.1f ms; pools retained %zu, reused %u\n",
                    capture_compiler->Depth(), compiler->Depth(),
                    capture_compiler->QueueWaitNs() / 1e6, compiler->QueueWaitNs() / 1e6,
                    compiler_pools.Retained(), g_gow3_pool_reuses.exchange(0));
    }
    if (lookahead) {
        std::scoped_lock lock{lookahead->mutex};
        std::printf("Pipeline look-ahead stats: %llu queued, %llu published, %llu taken (%llu ready), %llu failed; "
                    "%.1f ms required wait; %zu outstanding\n",
                    (unsigned long long)lookahead->queued.exchange(0),
                    (unsigned long long)lookahead->published.exchange(0),
                    (unsigned long long)lookahead->used.exchange(0),
                    (unsigned long long)lookahead->ready_on_use.exchange(0),
                    (unsigned long long)lookahead->failed.exchange(0),
                    lookahead->wait_ns.exchange(0) / 1e6, lookahead->builds.size());
        std::printf("Shader look-ahead stats: %llu queued, %llu published, %llu taken (%llu ready), %llu rejected; "
                    "%.1f ms required wait; %zu outstanding\n",
                    (unsigned long long)lookahead->shader_queued.exchange(0),
                    (unsigned long long)lookahead->shader_published.exchange(0),
                    (unsigned long long)lookahead->shader_used.exchange(0),
                    (unsigned long long)lookahead->shader_ready.exchange(0),
                    (unsigned long long)lookahead->shader_failed.exchange(0),
                    lookahead->shader_wait_ns.exchange(0) / 1e6, lookahead->shaders.size());
    }
}

void PipelineCache::PrepareAheadShader(const Program& program, const Shader::Info& info,
                                      const Shader::ShaderParams& params,
                                      const Shader::StageSpecialization& spec) {
    if (!lookahead || !lookahead->ready.load(std::memory_order_acquire) || !AsyncCompilation() ||
        (info.hw_stage != HwStage::Vertex && info.hw_stage != HwStage::Fragment)) {
        return;
    }
    const auto exists = [&] {
        return std::ranges::any_of(lookahead->shaders, [&](const auto& stage) {
            return stage.program == &program && stage.build->spec == spec;
        });
    };
    {
        std::scoped_lock lock{lookahead->mutex};
        if (lookahead->shaders.size() >= GraphicsLookahead::Capacity || exists() || !compiler->HasCapacity()) {
            return;
        }
    }
    auto build = std::make_shared<AsyncShaderCompilation>();
    auto* previous_recovery = runtime_fault_recover;
    sigjmp_buf recover;
    if (sigsetjmp(recover, 0)) {
        runtime_fault_recover = previous_recovery;
        return;
    }
    runtime_fault_recover = &recover;
    build->device = instance.GetDevice();
    build->code.assign(params.code.begin(), params.code.end());
    build->user_data.assign(params.user_data.begin(), params.user_data.end());
    build->flat_snapshot = info.flattened_ud_buf;
    build->info = Shader::Info(info.hw_stage, info.sw_stage, params);
    build->info.user_data = build->user_data;
    build->info.compilation_flat_snapshot = Shader::Info::CompilationSnapshot{build->flat_snapshot, info.srt_info};
    build->runtime = spec.runtime_info;
    build->binding = spec.start;
    build->spec = spec; // immutable identity until GPU-thread adoption
    build->spec.info = &build->info;
    build->pool_cache = &compiler_pools;
    // GetProgram's guest-read recovery guard still covers this capture. After
    // it, translation/emission cannot follow guest resource/fetch pointers.
    build->fetch.Capture(info);
    build->fetch.info = &build->info;
    runtime_fault_recover = previous_recovery;
    for (const auto& buffer : build->fetch.buffers) {
        const auto fmt = buffer.GetDataFmt();
        const auto number = buffer.GetNumberFmt();
        if ((LiverpoolToVK::TrySurfaceFormat(fmt, number) == vk::Format::eUndefined &&
             fmt != AmdGpu::DataFormat::FormatInvalid) ||
            (number == AmdGpu::NumberFormat::SnormNz &&
             fmt != AmdGpu::DataFormat::Format8 && fmt != AmdGpu::DataFormat::Format8_8 &&
             fmt != AmdGpu::DataFormat::Format8_8_8_8 && fmt != AmdGpu::DataFormat::Format16 &&
             fmt != AmdGpu::DataFormat::Format16_16 && fmt != AmdGpu::DataFormat::Format16_16_16_16)) {
            ++lookahead->shader_failed;
            return;
        }
    }
    std::scoped_lock lock{lookahead->mutex};
    if (lookahead->shaders.size() >= GraphicsLookahead::Capacity || exists()) {
        return;
    }
    if (build->job.Start(*compiler, [this, build] {
        Common::ScopedSpeculativeCompilation speculative;
        PhaseTimer compile{g_gow3_async_ns};
        auto [pools, reused] = compiler_pools.Acquire();
        build->pools = std::move(pools);
        if (reused) ++g_gow3_pool_reuses;
        Shader::Gcn::ScopedFetchShaderSnapshot fetch{build->fetch};
        build->ir.emplace(Shader::TranslateProgram(build->code, *build->pools, build->info,
                                                   build->runtime, profile));
        const Shader::StageSpecialization compiled(build->info, build->runtime, profile, build->spec.start);
        if (compiled.buffers.size() != build->spec.buffers.size() ||
            compiled.images.size() != build->spec.images.size() ||
            compiled.samplers.size() != build->spec.samplers.size() ||
            !(compiled == build->spec)) {
            throw std::runtime_error("Speculative shader specialization changed");
        }
        build->binary = Shader::Backend::SPIRV::EmitSPIRV(profile, build->runtime, *build->ir, build->binding);
        build->module = CompileSPV(build->binary, instance.GetDevice());
        build->ir.reset();
        compiler_pools.Recycle(std::move(build->pools));
        return build;
    })) {
        lookahead->shaders.push_back({&program, std::move(build)});
        ++lookahead->shader_queued;
    }
}

bool PipelineCache::TakeAheadShader(Program& program, const Shader::StageSpecialization& spec) {
    if (!lookahead) return false;
    std::shared_ptr<AsyncShaderCompilation> build;
    {
        std::scoped_lock lock{lookahead->mutex};
        const auto it = std::ranges::find_if(lookahead->shaders, [&](const auto& stage) {
            return stage.program == &program && stage.build->spec == spec;
        });
        if (it == lookahead->shaders.end()) return false;
        build = it->build;
        lookahead->shaders.erase(it);
    }
    auto result = build->job.Poll();
    if (result) {
        ++lookahead->shader_ready;
    } else {
        const auto started = std::chrono::steady_clock::now();
        {
            RequiredWaitTimer waiting{g_gow3_compile_wait_ns, "ahead stage", program.info.pgm_hash};
            build->job.Wait();
        }
        lookahead->shader_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        result = build->job.Poll();
    }
    if (!result) {
        ++lookahead->shader_failed;
        LOG_WARNING(Render_Vulkan, "Speculative shader {:#x} rejected: {}; compiling required stage",
                    program.info.pgm_hash, build->job.Error());
        return false;
    }
    PublishAheadShader(program, build);
    ++lookahead->shader_used;
    return true;
}

void PipelineCache::PublishAheadShader(Program& program, const std::shared_ptr<AsyncShaderCompilation>& build) {
    // The requesting draw or periodic GPU pump is the only writer. Never hold
    // lookahead->mutex while acquiring programs_mutex: preparation takes them
    // in the other order while reading program templates.
    if (std::ranges::find(program.modules, build->spec, &Program::Module::spec) != program.modules.end()) {
        return;
    }
    const size_t permutation = program.modules.size();
    const auto hash = program.info.pgm_hash;
    ExportShaderRecipe(build->info, build->spec, permutation, build->binary);
    RegisterShaderBinary(std::move(build->binary), hash, permutation);
    std::unique_lock lock{programs_mutex};
    build->spec.info = &program.info;
    RegisterShaderMeta(program.info, build->spec.fetch_shader_data, build->spec,
                       HashCombine(hash, permutation), permutation);
    program.AddPermut(build->module, std::move(build->spec));
    build->module = nullptr;
    NotifyPreparation();
    ++lookahead->shader_published;
}

void PipelineCache::PublishAhead() {
    if (!lookahead) return;
    std::vector<GraphicsLookahead::Stage> shaders;
    std::vector<std::shared_ptr<AsyncGraphicsCompilation>> pipelines;
    {
        std::scoped_lock lock{lookahead->mutex};
        for (auto it = lookahead->shaders.begin(); it != lookahead->shaders.end();) {
            auto result = it->build->job.Poll();
            if (result) {
                shaders.push_back(std::move(*it));
                it = lookahead->shaders.erase(it);
            } else if (it->build->job.Failed()) {
                ++lookahead->shader_failed;
                LOG_WARNING(Render_Vulkan, "Speculative shader preparation rejected: {}", it->build->job.Error());
                it = lookahead->shaders.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = lookahead->builds.begin(); it != lookahead->builds.end();) {
            auto result = it->second->job.Poll();
            if (result) {
                pipelines.push_back(std::move(it->second));
                lookahead->known.insert(it->first);
                it = lookahead->builds.erase(it);
            } else if (it->second->job.Failed()) {
                ++lookahead->failed;
                LOG_WARNING(Render_Vulkan, "Speculative pipeline preparation rejected: {}", it->second->job.Error());
                it = lookahead->builds.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& stage : shaders) {
        PublishAheadShader(*const_cast<Program*>(stage.program), stage.build);
    }
    for (auto& build : pipelines) {
        const auto& key = build->selection.graphics_key;
        build->pipeline->PublishStages(build->live_infos);
        RegisterPipelineData(key, std::hash<GraphicsPipelineKey>{}(key), build->data);
    ExportPipelineRecipe(key, build->data);
        const auto [it, inserted] = graphics_pipelines.emplace(key, std::move(build->pipeline));
        num_new_pipelines += inserted;
        ++lookahead->published;
    }
}

bool PipelineCache::PrepareGraphicsPipeline(PipelineSelection& worker_sel) {
    // Tessellation stages read constant buffers from memory at selection time: not prepared.
    if (worker_sel.regs->stage_enable.hs_en) {
        return false;
    }
    if (!RefreshGraphicsKey(worker_sel) || worker_sel.worker->failed) {
        return false;
    }
    PrepareAheadPipeline(worker_sel);
    return true;
}

void PipelineCache::PrepareAheadPipeline(const PipelineSelection& selection) {
    if (!lookahead || !lookahead->ready.load(std::memory_order_acquire) || !AsyncCompilation()) {
        return;
    }
    const auto& key = selection.graphics_key;
    {
        std::scoped_lock lock{lookahead->mutex};
        if (lookahead->known.contains(key) || lookahead->builds.contains(key) ||
            lookahead->builds.size() >= GraphicsLookahead::Capacity || !compiler->HasCapacity()) {
            return;
        }
    }
    auto build = std::make_shared<AsyncGraphicsCompilation>();
    build->selection = selection;
    build->selection.regs = nullptr;
    build->selection.worker = nullptr;
    for (const auto& stage : selection.worker->stages) {
        const auto logical = selection.worker->infos.at(stage.program).sw_stage;
        build->live_infos[u32(logical)] = &stage.program->info;
    }
    // Capture the fetch program while speculative guest reads are still guarded.
    // The compiler will subsequently read only these owned copies.
    sigjmp_buf recover;
    if (sigsetjmp(recover, 0)) {
        runtime_fault_recover = nullptr;
        return;
    }
    runtime_fault_recover = &recover;
    for (u32 i = 0; i < MaxShaderStages; ++i) {
        if (!selection.infos[i]) {
            continue;
        }
        build->infos[i] = std::make_unique<Shader::Info>();
        build->fetch[i].CapturePipeline(*selection.infos[i], *build->infos[i], build->user_data[i]);
        build->selection.infos[i] = build->infos[i].get();
    }
    runtime_fault_recover = nullptr;
    std::scoped_lock lock{lookahead->mutex};
    if (lookahead->known.contains(key) || lookahead->builds.contains(key) ||
        lookahead->builds.size() >= GraphicsLookahead::Capacity) {
        return;
    }
    if (build->job.Start(*compiler, [this, build] {
        Common::ScopedSpeculativeCompilation speculative;
        Shader::Gcn::ScopedFetchShaderSnapshot snapshot{build->fetch[u32(SwStage::Vertex)]};
        PhaseTimer compile{g_gow3_async_ns};
        auto& s = build->selection;
        // This cache was created without EXTERNALLY_SYNCHRONIZED_BIT. Vulkan
        // serializes cache access internally, including concurrent pipeline builds.
        build->pipeline = std::make_unique<GraphicsPipeline>(
            instance, scheduler, desc_heap, profile, s.graphics_key, *pipeline_cache,
            s.infos, s.runtime_infos, s.fetch_shader, s.modules, build->data, false);
        return build;
    })) {
        lookahead->builds.emplace(key, std::move(build));
        ++lookahead->queued;
    }
}

void PipelineCache::MarkPipelineKnown(const GraphicsPipelineKey& key) {
    if (lookahead) {
        std::scoped_lock lock{lookahead->mutex};
        lookahead->known.insert(key);
    }
}

const GraphicsPipeline* PipelineCache::TakeAheadPipeline(const PipelineSelection& selection) {
    if (!lookahead) {
        return nullptr;
    }
    std::shared_ptr<AsyncGraphicsCompilation> build;
    {
        std::scoped_lock lock{lookahead->mutex};
        const auto it = lookahead->builds.find(selection.graphics_key);
        if (it == lookahead->builds.end()) {
            lookahead->known.insert(selection.graphics_key);
            return nullptr;
        }
        build = std::move(it->second);
        lookahead->builds.erase(it);
        lookahead->known.insert(selection.graphics_key);
    }
    auto result = build->job.Poll();
    if (result) {
        ++lookahead->ready_on_use;
    } else {
        const auto started = std::chrono::steady_clock::now();
        {
            RequiredWaitTimer waiting{g_gow3_compile_wait_ns, "ahead pipeline", std::hash<GraphicsPipelineKey>{}(selection.graphics_key)};
            build->job.Wait();
        }
        lookahead->wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        result = build->job.Poll();
    }
    if (!result) {
        ++lookahead->failed;
        LOG_WARNING(Render_Vulkan, "Speculative pipeline {:#x} failed: {}; building required draw",
                    std::hash<GraphicsPipelineKey>{}(selection.graphics_key), build->job.Error());
        return nullptr;
    }
    // RefreshGraphicsKey has validated the live program hashes, specializations,
    // vertex formats and full render state. A speculative result is never used
    // merely because a worker's earlier register checksum happened to match.
    build->pipeline->PublishStages(selection.infos);
    RegisterPipelineData(selection.graphics_key,
                         std::hash<GraphicsPipelineKey>{}(selection.graphics_key), build->data);
    ExportPipelineRecipe(selection.graphics_key, build->data);
    const auto [it, inserted] = graphics_pipelines.emplace(selection.graphics_key, std::move(build->pipeline));
    num_new_pipelines += inserted;
    ++lookahead->used;
    return it->second.get();
}

const GraphicsPipeline* PipelineCache::TryPreparedPipeline(const PreparedDraw& prepared) {
    if (prepared.state.load(std::memory_order_acquire) != PreparedDraw::Ready ||
        prepared.reg_checksum != liverpool->gfx_reg_checksum) {
        return nullptr;
    }
    // Same registers; the stage programs and their flattened user data (which also covers
    // everything read from guest memory for the specialization) must match as well. This is
    // the per-stage work GetProgram does on the regular path.
    const auto& regs = liverpool->regs;
    for (u32 i = 0; i < prepared.num_stages; ++i) {
        const auto& stage = prepared.stages[i];
        const auto* pgm = regs.ProgramForStage(static_cast<u32>(stage.hw_stage));
        if (!pgm || !pgm->Address<u32*>()) {
            return nullptr;
        }
        const auto params = AmdGpu::GetParams(*pgm);
        if (params.hash != stage.hash) {
            return nullptr;
        }
        auto& info = const_cast<Program*>(stage.program)->info;
        info.pgm_base = params.Base();
        info.user_data = params.user_data;
        info.RefreshFlatBuf();
        if (info.pgm_base != stage.pgm_base || info.flattened_ud_buf.size() != stage.flat_size ||
            std::memcmp(info.flattened_ud_buf.data(), stage.flat,
                        stage.flat_size * sizeof(u32)) != 0) {
            return nullptr;
        }
    }
    const auto it = graphics_pipelines.find(prepared.key);
    return it != graphics_pipelines.end() ? it->second.get() : nullptr;
}

const GraphicsPipeline* PipelineCache::GetGraphicsPipeline(const DrawIndirectParams params,
                                                           const PreparedDraw* prepared) {
    SeedWorkerCaches();
    PublishShaders();
    PublishGraphics();
    PumpWarmUp();
    if (lookahead && (++ahead_pump_draws & 255) == 0) {
        PublishAhead();
    }
    used_prepared = nullptr;
    if (prepared) {
        if (const auto* pipeline = TryPreparedPipeline(*prepared)) {
            used_prepared = prepared;
            return pipeline;
        }
    }
    sel.draw_indirect_params = params;
    if (!RefreshGraphicsKey(sel)) {
        ASSERT_MSG(AllowsIncompleteDraws() || !sel.compilation_pending,
                   "Required graphics stages did not become ready");
        if (sel.compilation_pending) {
            ++g_gow3_pending_draws;
        }
        return nullptr;
    }
    if (AsyncCompilation() && !sel.regs->stage_enable.hs_en && !EmulatorSettings.IsShaderCollect()) {
        const auto found = graphics_pipelines.find(sel.graphics_key);
        if (found != graphics_pipelines.end()) {
            return found->second.get();
        }
        if (const auto* ahead = TakeAheadPipeline(sel)) {
            return ahead;
        }
        if (!pending_graphics.contains(sel.graphics_key) && compiler->HasCapacity()) {
            PhaseTimer capture{g_gow3_snapshot_ns};
            auto build = std::make_shared<AsyncGraphicsCompilation>();
            build->selection = sel;
            build->selection.regs = nullptr;
            build->live_infos = sel.infos;
            for (u32 i = 0; i < MaxShaderStages; ++i) {
                if (sel.infos[i]) {
                    build->infos[i] = std::make_unique<Shader::Info>();
                    build->fetch[i].CapturePipeline(*sel.infos[i], *build->infos[i], build->user_data[i]);
                    build->selection.infos[i] = build->infos[i].get();
                }
            }
            const bool queued = build->job.Start(*compiler, [this, build] {
                // Only the vertex stage uses fetch-shader guest pointers during pipeline construction.
                Shader::Gcn::ScopedFetchShaderSnapshot snapshot{build->fetch[u32(SwStage::Vertex)]};
                PhaseTimer compile{g_gow3_async_ns};
                auto& s = build->selection;
                // Complete mode waits for each requested pipeline, so these builds
                // do not overlap. Reuse the warmed main cache: rotating through
                // private worker caches recompiles shared Metal shader libraries.
                const auto cache = AllowsIncompleteDraws()
                    ? *worker_pipeline_caches[AsyncCompiler::WorkerIndex()] : *pipeline_cache;
                build->pipeline = std::make_unique<GraphicsPipeline>(
                    instance, scheduler, desc_heap, profile, s.graphics_key,
                    cache,
                    s.infos, s.runtime_infos, s.fetch_shader, s.modules, build->data, false);
                return build;
            });
            if (queued) {
                pending_graphics.emplace(sel.graphics_key, std::move(build));
            }
        }
        if (AllowsIncompleteDraws()) {
            ++g_gow3_pending_draws;
            return nullptr;
        }
        // Background jobs keep their captured guest inputs. Wait only for this
        // pipeline, not the rest of the queue, before submitting its draw.
        if (const auto pending = pending_graphics.find(sel.graphics_key);
            pending != pending_graphics.end()) {
            auto build = pending->second;
            {
                RequiredWaitTimer waiting{g_gow3_compile_wait_ns, "pipeline", std::hash<GraphicsPipelineKey>{}(sel.graphics_key)};
                build->job.Wait();
            }
            PublishGraphics();
            if (const auto ready = graphics_pipelines.find(sel.graphics_key);
                ready != graphics_pipelines.end()) {
                return ready->second.get();
            }
            if (build->job.Failed()) {
                UNREACHABLE_MSG("Required graphics pipeline {:#x} failed: {}",
                                std::hash<GraphicsPipelineKey>{}(sel.graphics_key), build->job.Error());
            }
            // A completed job with a cache lookup miss is not a compiler failure.
            // Preserve the draw via the synchronous path rather than aborting the
            // application with an empty compiler error (as 0.4.3 did).
            LOG_WARNING(Render_Vulkan,
                        "Completed pipeline {:#x} was not published under snapshot {:#x}; "
                        "rebuilding synchronously",
                        std::hash<GraphicsPipelineKey>{}(sel.graphics_key),
                        std::hash<GraphicsPipelineKey>{}(build->selection.graphics_key));
        }
        // A full queue must not discard a draw: build it on the requesting
        // thread using the existing synchronous path below.
    }
    const auto [it, is_new] = graphics_pipelines.try_emplace(sel.graphics_key);
    if (is_new) {
        MarkPipelineKnown(sel.graphics_key);
        const auto pipeline_hash = std::hash<GraphicsPipelineKey>{}(sel.graphics_key);
        LOG_INFO(Render_Vulkan, "Compiling graphics pipeline {:#x}", pipeline_hash);
        CompileTimer timer;

        GraphicsPipeline::SerializationSupport sdata{};
        it.value() = std::make_unique<GraphicsPipeline>(
            instance, scheduler, desc_heap, profile, sel.graphics_key, *pipeline_cache, sel.infos,
            sel.runtime_infos, sel.fetch_shader, sel.modules, sdata, false);

        RegisterPipelineData(sel.graphics_key, pipeline_hash, sdata);
    ExportPipelineRecipe(sel.graphics_key, sdata);
        ++num_new_pipelines;

        if (EmulatorSettings.IsShaderCollect()) {
            for (auto stage = 0; stage < MaxShaderStages; ++stage) {
                if (sel.infos[stage]) {
                    auto& m = sel.modules[stage];
                    module_related_pipelines[m].emplace_back(sel.graphics_key);
                }
            }
        }
        sel.fetch_shader.reset();
    }
    return it->second.get();
}

const ComputePipeline* PipelineCache::GetComputePipeline() {
    PublishShaders();
    PublishGraphics();
    PumpWarmUp();
    if (!RefreshComputeKey()) {
        return nullptr;
    }
    const auto [it, is_new] = compute_pipelines.try_emplace(compute_key);
    if (is_new) {
        const auto pipeline_hash = std::hash<ComputePipelineKey>{}(compute_key);
        LOG_INFO(Render_Vulkan, "Compiling compute pipeline {:#x}", pipeline_hash);
        CompileTimer timer;

        ComputePipeline::SerializationSupport sdata{};
        it.value() = std::make_unique<ComputePipeline>(instance, scheduler, desc_heap, profile,
                                                       *pipeline_cache, compute_key, *sel.infos[0],
                                                       sel.modules[0], sdata, false);
        RegisterPipelineData(compute_key, sdata);
        ++num_new_pipelines;

        if (EmulatorSettings.IsShaderCollect()) {
            auto& m = sel.modules[0];
            module_related_pipelines[m].emplace_back(compute_key);
        }
    }
    return it->second.get();
}

bool PipelineCache::RefreshGraphicsKey(PipelineSelection& sel) {
    std::memset(&sel.graphics_key, 0, sizeof(GraphicsPipelineKey));
    const auto& regs = (*sel.regs);
    auto& key = sel.graphics_key;

    // gow3: a depth/stencil buffer that cannot affect the draw is not attached: on the PC a
    // smaller one would crop a full-screen pass (eltutz, vk_depth_stencil_state.h).
    const auto db = GetEffectiveDepthStencilState(
        regs, Gow3Toggle::SplitUnusedDepthEnabled(),
        instance.IsDepthBoundsSupported());
    const bool db_enabled = db.needs_attachment;
    // Keep the backing image's combined format for stencil-only rendering.
    key.omit_depth_attachment = db_enabled && !db.needs_depth;
    // Process-start diagnostics must not take libc's environment lock for every
    // draw on the command thread and all preparation workers.
    static const bool depth_diagnostics = std::getenv("GOW3_DEPTH_DIAGNOSTICS") != nullptr;
    if (depth_diagnostics) {
        static std::mutex mutex;
        static std::vector<std::array<u32, 3>> seen;
        const std::array data{std::bit_cast<u32>(regs.depth_control),
                              std::bit_cast<u32>(regs.depth_render_control),
                              std::bit_cast<u32>(regs.depth_render_override)};
        std::scoped_lock lock(mutex);
        if (seen.size() < 32 && std::ranges::find(seen, data) == seen.end()) {
            seen.push_back(data);
            std::printf("Depth decision: omit=%u control=%08x render=%08x override=%08x\n",
                        key.omit_depth_attachment, data[0], data[1], data[2]);
        }
    }

    key.z_format = db_enabled && regs.depth_buffer.DepthValid()
                       ? regs.depth_buffer.z_info.format
                       : AmdGpu::DepthBuffer::ZFormat::Invalid;
    key.stencil_format = db_enabled && regs.depth_buffer.StencilValid()
                             ? regs.depth_buffer.stencil_info.format
                             : AmdGpu::DepthBuffer::StencilFormat::Invalid;
    key.depth_clamp_enable = !regs.depth_render_override.disable_viewport_clamp;
    key.depth_clip_enable = regs.clipper_control.ZclipEnable();
    key.clip_space = regs.clipper_control.clip_space;
    key.provoking_vtx_last = regs.polygon_control.provoking_vtx_last;
    key.prim_type = regs.primitive_type;
    key.polygon_mode = regs.polygon_control.PolyMode();
    key.patch_control_points =
        regs.stage_enable.hs_en ? regs.ls_hs_config.hs_input_control_points : 0;
    key.logic_op = regs.color_control.rop3;
    key.depth_samples = db_enabled ? regs.depth_buffer.NumSamples() : 1;
    key.num_samples = key.depth_samples;
    key.cb_shader_mask = regs.color_shader_mask;

    const bool skip_cb_binding =
        regs.color_control.mode == AmdGpu::ColorControl::OperationMode::Disable;

    // Only potentially animated G-buffer draws may use the extra motion attachment. The
    // ordinary stage variant is needed first to inspect the vertex shader's resources.
    bool motion_possible = false;
    {
        u32 bound = 0;
        for (s32 cb = 0; cb < AmdGpu::NUM_COLOR_BUFFERS && !skip_cb_binding; ++cb) {
            bound += (regs.color_buffers[cb] && regs.color_target_mask.GetMask(cb)) ? 1 : 0;
        }
        motion_possible = Shader::MotionVectors::enabled &&
                          (bound >= 5 || (bound>0 && db_enabled && regs.depth_control.depth_enable &&
                           (regs.num_indices>6 || regs.num_instances.NumInstances()>1) &&
                           FrameGen::GeometryDepth(regs.depth_buffer.DepthAddress()))) &&
                          !regs.color_buffers[Shader::MotionVectors::Output] &&
                          regs.depth_buffer.DepthValid() &&
                          regs.depth_buffer.NumSamples() == 1 && !regs.IsClipDisabled() &&
                          regs.stage_enable.raw == AmdGpu::ShaderStageEnable::VgtStages::Vs;
    }
    sel.motion = false;

    // First pass to fill render target information needed by shader recompiler
    for (s32 cb = 0; cb < AmdGpu::NUM_COLOR_BUFFERS && !skip_cb_binding; ++cb) {
        const auto& col_buf = regs.color_buffers[cb];
        if (!col_buf || !regs.color_target_mask.GetMask(cb)) {
            // No attachment bound or writing to it is disabled.
            continue;
        }

        // Fill color target information
        auto& color_buffer = key.color_buffers[cb];
        color_buffer.data_format = col_buf.GetDataFmt();
        color_buffer.num_format = col_buf.GetNumberFmt();
        color_buffer.num_conversion = col_buf.GetNumberConversion();
        color_buffer.export_format = regs.color_export_format.GetFormat(cb);
        color_buffer.swizzle = col_buf.Swizzle();

    }

    // Compile and bind shader stages
    if (!RefreshGraphicsStages(sel)) {
        return false;
    }
    if (motion_possible) {
        const auto* vs = sel.infos[static_cast<u32>(Shader::SwStage::Vertex)];
        if (vs) {
            // Shaders with a bone palette (motion_history.h). Small skeletons (weapons,
            // props) also include static world pieces: the rasterizer gives those
            // history only while their constants change.
            for (const auto& resource : vs->buffers) {
                if (resource.IsSpecial()) continue;
                const auto buffer = resource.GetSharp(*vs);
                if (buffer.Valid() && buffer.GetStride() == 16 &&
                    Motion::ClassifyBuffer(buffer.GetSize()) != Motion::BufferRole::Other) {
                    sel.motion = true;
                    break;
                }
            }
        }
        // GOW3_MOTION_SELECT_LOG=1: each G-buffer vertex shader once, with its buffer sizes
        // and the selection, to find animated models that the size rule leaves out.
        static const bool select_log = [] {
            const char* value = std::getenv("GOW3_MOTION_SELECT_LOG");
            return value && value[0] == '1';
        }();
        if (select_log && vs) {
            std::string sizes;
            for (const auto& resource : vs->buffers) {
                if (resource.IsSpecial()) continue;
                const auto buffer = resource.GetSharp(*vs);
                sizes += fmt::format(" {}/{}", buffer.GetSize(), buffer.GetStride());
            }
            static std::mutex log_mutex;
            static std::unordered_set<u64> logged;
            std::scoped_lock lock{log_mutex};
            if (logged.insert(vs->pgm_hash ^ std::hash<std::string>{}(sizes)).second) {
                std::printf("Motion select: vs %016llx %s, buffers (size/stride):%s\n",
                            static_cast<unsigned long long>(vs->pgm_hash),
                            sel.motion ? "ON " : "off", sizes.c_str());
            }
        }
        // Keep the old broad path available for visual A/B tests.
        static const bool all_motion = [] {
            const char* value = std::getenv("GOW3_OBJECT_MOTION_ALL");
            return value && value[0] == '1';
        }();
        if (all_motion || FrameGen::GeometryDepth(regs.depth_buffer.DepthAddress())) {
            sel.motion = true;
        }
        // A guest varying (including the clip-distance emulation shift) must
        // never alias the two extra vectors. Keep such draws on the real path.
        const auto* fs=sel.infos[static_cast<u32>(Shader::SwStage::Fragment)];
        for(u32 i=Shader::MotionVectors::CurrentLocation-1;i<Shader::IR::NumParams;++i){
            const auto attr=Shader::IR::Attribute::Param0+i;
            if((vs && vs->stores.GetAny(attr)) || (fs && fs->loads.GetAny(attr)))sel.motion=false;
        }
        if (sel.motion && !RefreshGraphicsStages(sel)) {
            return false;
        }
    }

    // Second pass to mask out render targets not written by shader and fill remaining info
    u8 color_samples = 0;
    bool all_color_samples_same = true;
    for (s32 cb = 0; cb < key.num_color_attachments && !skip_cb_binding; ++cb) {
        const auto& col_buf = regs.color_buffers[cb];
        const u32 target_mask = regs.color_target_mask.GetMask(cb);
        if (!col_buf || !target_mask) {
            continue;
        }
        if ((key.mrt_mask & (1u << cb)) == 0) {
            std::memset(&key.color_buffers[cb], 0, sizeof(Shader::PsColorBuffer));
            continue;
        }

        // Fill color blending information
        if (regs.blend_control[cb].enable && !col_buf.info.blend_bypass) {
            key.blend_controls[cb] = regs.blend_control[cb];
        }

        // Apply swizzle to target mask
        key.write_masks[cb] =
            vk::ColorComponentFlags{key.color_buffers[cb].swizzle.ApplyMask(target_mask)};

        // Fill color samples
        const u8 prev_color_samples = std::exchange(color_samples, col_buf.NumSamples());
        all_color_samples_same &= color_samples == prev_color_samples || prev_color_samples == 0;
        key.color_samples[cb] = color_samples;
        key.num_samples = std::max(key.num_samples, color_samples);
    }

    if (sel.motion) {
        constexpr u32 mv = Shader::MotionVectors::Output;
        key.motion_vectors = 1;
        key.mrt_mask |= 1u << mv;
        key.num_color_attachments = mv + 1;
        auto& color_buffer = key.color_buffers[mv];
        color_buffer.data_format = FrameGen::CompactMotion() ? AmdGpu::DataFormat::Format16_16 : FrameGen::Requested()?AmdGpu::DataFormat::Format16_16_16_16:AmdGpu::DataFormat::Format32_32_32_32;
        color_buffer.num_format = AmdGpu::NumberFormat::Float;
        color_buffer.swizzle = AmdGpu::IdentityMapping;
        key.write_masks[mv] = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        if (FrameGen::CompactMotion())
            key.write_masks[mv] = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG;
        key.color_samples[mv] = 1;
    }

    // Force all color samples to match depth samples to avoid unsupported MSAA configuration
    if (color_samples != 0) {
        const bool depth_mismatch = db_enabled && color_samples != key.depth_samples;
        if (!all_color_samples_same && !instance.IsMixedAnySamplesSupported() ||
            all_color_samples_same && depth_mismatch && !instance.IsMixedDepthSamplesSupported()) {
            key.color_samples.fill(key.depth_samples);
            key.num_samples = key.depth_samples;
        }
    }

    return true;
}

bool PipelineCache::RefreshGraphicsStages(PipelineSelection& sel) {
    const auto& regs = (*sel.regs);
    auto& key = sel.graphics_key;
    sel.fetch_shader = std::nullopt;
    sel.compilation_pending = false;

    Shader::Backend::Bindings binding{};
    const auto bind_stage = [&](HwStage stage_in, SwStage stage_out) -> bool {
        const auto stage_in_idx = static_cast<u32>(stage_in);
        const auto stage_out_idx = static_cast<u32>(stage_out);
        if (!regs.stage_enable.IsStageEnabled(stage_in_idx)) {
            key.stage_hashes[stage_out_idx] = 0;
            sel.infos[stage_out_idx] = nullptr;
            return false;
        }

        const auto* pgm = regs.ProgramForStage(stage_in_idx);
        if (!pgm || !pgm->Address<u32*>()) {
            key.stage_hashes[stage_out_idx] = 0;
            sel.infos[stage_out_idx] = nullptr;
            return false;
        }

        const auto params = AmdGpu::GetParams(*pgm);
        std::optional<Shader::Gcn::FetchShaderData> fetch_shader_;
        std::tie(sel.infos[stage_out_idx], sel.modules[stage_out_idx], fetch_shader_,
                 key.stage_hashes[stage_out_idx]) =
            GetProgram(sel, stage_in, stage_out, params, binding);
        if (!sel.infos[stage_out_idx]) {
            sel.compilation_pending = true;
            return false;
        }
        if (fetch_shader_) {
            sel.fetch_shader = fetch_shader_;
        }
        return true;
    };

    sel.infos.fill(nullptr);
    sel.modules.fill(nullptr);

    bind_stage(HwStage::Fragment, SwStage::Fragment);
    if (sel.compilation_pending) {
        return false;
    }

    const auto* fs_info = sel.infos[static_cast<u32>(SwStage::Fragment)];
    key.mrt_mask = fs_info ? fs_info->mrt_mask : 0u;
    key.num_color_attachments = std::bit_width(key.mrt_mask);

    switch (regs.stage_enable.raw) {
    case AmdGpu::ShaderStageEnable::VgtStages::EsGs:
        if (!instance.IsGeometryStageSupported()) {
            LOG_WARNING(Render_Vulkan, "Geometry shader stage unsupported, skipping");
            return false;
        }
        if (regs.vgt_gs_mode.onchip || regs.vgt_strmout_config.raw) {
            LOG_WARNING(Render_Vulkan, "Geometry shader features unsupported, skipping");
            return false;
        }
        if (!bind_stage(HwStage::Export, SwStage::Vertex)) {
            return false;
        }
        if (!bind_stage(HwStage::Geometry, SwStage::Geometry)) {
            return false;
        }
        break;
    case AmdGpu::ShaderStageEnable::VgtStages::LsHs:
        if (!instance.IsTessellationSupported()) {
            return false;
        }
        if (!bind_stage(HwStage::Hull, SwStage::TessellationControl)) {
            return false;
        }
        if (!bind_stage(HwStage::Vertex, SwStage::TessellationEval)) {
            return false;
        }
        if (!bind_stage(HwStage::Local, SwStage::Vertex)) {
            return false;
        }
        break;
    case AmdGpu::ShaderStageEnable::VgtStages::LsHsEsGs:
        if (!instance.IsTessellationSupported()) {
            return false;
        }
        if (!instance.IsGeometryStageSupported()) {
            LOG_WARNING(Render_Vulkan, "Geometry shader stage unsupported, skipping");
            return false;
        }
        if (regs.vgt_gs_mode.onchip || regs.vgt_strmout_config.raw) {
            LOG_WARNING(Render_Vulkan, "Geometry shader features unsupported, skipping");
            return false;
        }
        if (!bind_stage(HwStage::Hull, SwStage::TessellationControl)) {
            return false;
        }
        if (!bind_stage(HwStage::Export, SwStage::TessellationEval)) {
            return false;
        }
        if (!bind_stage(HwStage::Local, SwStage::Vertex)) {
            return false;
        }
        if (!bind_stage(HwStage::Geometry, SwStage::Geometry)) {
            return false;
        }
        break;
    case AmdGpu::ShaderStageEnable::VgtStages::Vs:
        bind_stage(HwStage::Vertex, SwStage::Vertex);
        if (sel.compilation_pending) {
            return false;
        }
        break;
    default:
        LOG_WARNING(Render_Vulkan, "unimplemented shader stage {}", (u32)regs.stage_enable.raw);
        return false;
    }

    const auto* vs_info = sel.infos[static_cast<u32>(SwStage::Vertex)];
    if (vs_info && sel.fetch_shader && !instance.IsVertexInputDynamicState()) {
        // Without vertex input dynamic state, the pipeline needs to specialize on format.
        // Stride will still be handled outside the pipeline using dynamic state.
        u32 vertex_binding = 0;
        for (const auto& attrib : sel.fetch_shader->attributes) {
            const auto& buffer = attrib.GetSharp(*vs_info);
            // Preparation is speculative: the guest may still be filling the V#.
            // Match PrepareVertexInputs' fallback before the fixed-input path
            // asserts. The submitted draw is checked again by the GPU thread.
            if (sel.worker &&
                Vulkan::LiverpoolToVK::TrySurfaceFormat(buffer.GetDataFmt(),
                                                        buffer.GetNumberFmt()) ==
                    vk::Format::eUndefined &&
                buffer.GetDataFmt() != AmdGpu::DataFormat::FormatInvalid) {
                return false;
            }
            ASSERT_MSG(vertex_binding < MaxVertexBufferCount,
                       "Vertex attribute binding count exceeded limit: {} >= {}", vertex_binding,
                       MaxVertexBufferCount);
            key.vertex_buffer_formats[vertex_binding++] =
                Vulkan::LiverpoolToVK::SurfaceFormat(buffer.GetDataFmt(), buffer.GetNumberFmt());
        }
    }

    return true;
}

bool PipelineCache::RefreshComputeKey() {
    Shader::Backend::Bindings binding{};
    const auto& cs_pgm = liverpool->GetCsRegs();
    const auto cs_params = AmdGpu::GetParams(cs_pgm);
    std::tie(sel.infos[0], sel.modules[0], sel.fetch_shader, compute_key.value) =
        GetProgram(sel, HwStage::Compute, SwStage::Compute, cs_params, binding);
    return sel.infos[0] != nullptr;
}

vk::ShaderModule PipelineCache::CompileModule(Shader::Info& info, Shader::RuntimeInfo& runtime_info,
                                              const std::span<const u32>& code, size_t perm_idx,
                                              Shader::Backend::Bindings& binding) {
    LOG_INFO(Render_Vulkan, "Compiling {} shader {:#x} {}", info.hw_stage, info.pgm_hash,
             perm_idx != 0 ? "(permutation)" : "");
    DumpShader(code, info.pgm_hash, info.hw_stage, perm_idx, "bin");
    CompileTimer timer;

    const auto start_binding = binding;
    const auto ir_program = Shader::TranslateProgram(code, pools, info, runtime_info, profile);
    auto spv = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime_info, ir_program, binding);
    DumpShader(spv, info.pgm_hash, info.hw_stage, perm_idx, "spv");

    vk::ShaderModule module;

    auto patch = GetShaderPatch(info.pgm_hash, info.hw_stage, perm_idx, "spv");
    const bool is_patched = patch && EmulatorSettings.IsPatchShaders();
    if (is_patched) {
        LOG_INFO(Loader, "Loaded patch for {} shader {:#x}", info.hw_stage, info.pgm_hash);
        module = CompileSPV(*patch, instance.GetDevice());
    } else {
        module = CompileSPV(spv, instance.GetDevice());
    }

    if (!is_patched) {
        const Shader::StageSpecialization recorded{info, runtime_info, profile, start_binding};
        ExportShaderRecipe(info, recorded, perm_idx, spv);
    }
    RegisterShaderBinary(std::move(spv), info.pgm_hash, perm_idx);

    const auto name = GetShaderName(info.hw_stage, info.pgm_hash, perm_idx);
    Vulkan::SetObjectName(instance.GetDevice(), module, name);
    if (EmulatorSettings.IsShaderCollect()) {
        DebugState.CollectShader(name, info.sw_stage, module, spv, code,
                                 patch ? *patch : std::span<const u32>{}, is_patched);
    }
    return module;
}



PipelineCache::Result PipelineCache::GetProgram(PipelineSelection& sel, HwStage hw_stage,
                                                SwStage sw_stage,
                                                const Shader::ShaderParams& input_params,
                                                Shader::Backend::Bindings& binding, bool allow_alias) {
    auto params = input_params;
    if (allow_alias && program_aliases_ready.load(std::memory_order_acquire)) {
        const auto alias = program_aliases.find(params.hash);
        if (alias != program_aliases.end() && alias->second.stage == hw_stage) {
            const auto& code = *alias->second.code;
            if (code.size() == params.code.size() &&
                std::memcmp(code.data(), params.code.data(), params.code.size_bytes()) == 0) {
                params.hash = alias->second.canonical;
            } else {
                ++program_alias_rejected;
            }
        }
    }
    const bool aliased = params.hash != input_params.hash;
    const auto matches = [aliased](const Shader::StageSpecialization& cached,
                                   const Shader::StageSpecialization& current, bool material) {
        return cached == current && (!(aliased || material) ||
            (cached.buffers.size() == current.buffers.size() &&
             cached.images.size() == current.images.size() &&
             cached.samplers.size() == current.samplers.size() &&
             cached.bitset == current.bitset && current == cached));
    };
    const auto safe_module = [&](size_t index) {
        if (!aliased) return true;
        const auto safe = alias_safe_modules.find(params.hash);
        return safe != alias_safe_modules.end() && safe->second.contains(index);
    };
    const auto safe_fetch = [](const Shader::Info& info, const Shader::StageSpecialization& spec) {
        if (!spec.fetch_shader_data) return true;
        for (const auto& attribute : spec.fetch_shader_data->attributes) {
            const auto sharp = attribute.GetSharp(info);
            const auto format = AmdGpu::NumberFormat(sharp.num_format);
            // Neutral catalogue reconstruction has no shader-side fetch number
            // conversion. Other conversions keep their original program ID.
            if (format == AmdGpu::NumberFormat::Uscaled || format == AmdGpu::NumberFormat::Sscaled ||
                format == AmdGpu::NumberFormat::SnormNz) return false;
        }
        return true;
    };
    const auto material_at = [&](size_t index) {
        const auto found = material_modules.find(params.hash);
        return found != material_modules.end() && found->second.contains(index);
    };
    const auto compatible = [&](const auto& modules, const auto& module,
                                const Shader::StageSpecialization& spec, const Shader::Info& info) {
        const size_t index = &module - modules.data();
        const bool material = material_at(index);
        return safe_module(index) && matches(module.spec,spec,material) &&
            (!(aliased || material) || safe_fetch(info,spec));
    };
    auto runtime_info = BuildRuntimeInfo(sel, hw_stage, sw_stage);
    if (sel.worker) {
        // gow3: draw-preparation worker: look up only, with the worker's own Info copy.
        auto& worker = *sel.worker;
        std::shared_lock lk{programs_mutex};
        const auto found_program = program_cache.find(params.hash);
        if (found_program == program_cache.end() || !found_program->second->info_template) {
            worker.failed = true;
            return {};
        }
        const Program* program = found_program->second.get();
        auto [it_info, new_info] = worker.infos.try_emplace(program, *program->info_template);
        auto& info = it_info->second;
        info.pgm_base = params.Base();
        info.user_data = params.user_data;
        // The walk of resource tables and the fetch shader parse read guest memory through
        // pointers in the registers; ahead of the GPU thread that memory may already be
        // reused. A fault returns here (runtime_fault_recover) and the draw is left to the GPU
        // thread. A jump out of the specialization leaks its partial allocations (rare).
        sigjmp_buf recover;
        if (sigsetjmp(recover, 0)) {
            worker.failed = true;
            return {};
        }
        runtime_fault_recover = &recover;
        info.RefreshFlatBuf();
        auto spec = Shader::StageSpecialization(info, runtime_info, profile, binding);
        const auto it = std::ranges::find_if(program->modules,
            [&](const auto& module) { return compatible(program->modules,module,spec,info); });
        if (aliased && (it == program->modules.end() || !safe_fetch(info, spec))) {
            runtime_fault_recover = nullptr;
            lk.unlock();
            ++program_alias_rejected;
            return GetProgram(sel, hw_stage, sw_stage, input_params, binding, false);
        }
        if (it == program->modules.end()) {
            PrepareAheadShader(*program, info, params, spec);
            runtime_fault_recover = nullptr;
            worker.failed = true;
            return {};
        }
        runtime_fault_recover = nullptr;
        if (aliased) ++program_alias_hits;
        info.AddBindings(binding);
        const size_t perm_idx = std::distance(program->modules.begin(), it);
        worker.stages.push_back(
            {program, input_params.hash, hw_stage, info.pgm_base, &info.flattened_ud_buf});
        return std::make_tuple(&info, it->module, it->spec.fetch_shader_data,
                               HashCombine(params.hash, perm_idx));
    }

    auto it_pgm = program_cache.find(params.hash); // this thread is the only writer
    const bool compile_async = AsyncCompilation() && hw_stage != HwStage::Compute &&
                               !sel.regs->stage_enable.hs_en &&
                               !EmulatorSettings.IsShaderCollect();
    const bool required = !AllowsIncompleteDraws();
    if ((!compile_async || (required && it_pgm != program_cache.end() &&
                           it_pgm->second->modules.empty())) &&
        it_pgm != program_cache.end() && it_pgm->second->pending) {
        {
            RequiredWaitTimer waiting{g_gow3_compile_wait_ns, "stage", params.hash};
            it_pgm->second->pending->job.Wait();
        }
        PublishShaders();
    }
    if ((!compile_async || required) && it_pgm != program_cache.end() && it_pgm->second->modules.empty() &&
        !it_pgm->second->compilation_failed) {
        std::unique_lock lock{programs_mutex};
        program_cache.erase(it_pgm);
        it_pgm = program_cache.end();
    }
    if (it_pgm == program_cache.end()) {
        auto new_program = std::make_unique<Program>(hw_stage, sw_stage, params);
        if (compile_async) {
            const bool queued = QueueShader(*new_program, hw_stage, sw_stage, params,
                                            runtime_info, binding, 0, true);
            if (queued || !required) {
                {
                    std::unique_lock lock{programs_mutex};
                    program_cache.emplace(params.hash, std::move(new_program));
                }
                // Re-enter after insertion so the normal lookup waits and
                // publishes the result without retaining job-owned pointers.
                return required ? GetProgram(sel, hw_stage, sw_stage, params, binding) : Result{};
            }
            ASSERT_MSG(!new_program->compilation_failed,
                       "Required shader {:#x} capture failed", params.hash);
            // Queue saturation falls through to synchronous compilation.
        }
        auto start = binding;
        const auto module =
            CompileModule(new_program->info, runtime_info, params.code, 0, binding);
        auto spec = Shader::StageSpecialization(new_program->info, runtime_info, profile, start);
        const auto perm_hash = HashCombine(params.hash, 0);

        RegisterShaderMeta(new_program->info, spec.fetch_shader_data, spec, perm_hash, 0);
        new_program->AddPermut(module, std::move(spec));
        new_program->info_template = std::make_unique<Shader::Info>(new_program->info);
        Program* program = new_program.get();
        {
            std::unique_lock lk{programs_mutex};
            program_cache.emplace(params.hash, std::move(new_program));
            NotifyPreparation();
        }
        return std::make_tuple(&program->info, module, program->modules[0].spec.fetch_shader_data,
                               perm_hash);
    }

    auto& program = it_pgm.value();
    ASSERT_MSG(!required || !program->compilation_failed,
               "Required shader {:#x} compilation failed", params.hash);
    if (program->compilation_failed || (program->modules.empty() && program->pending)) {
        return {};
    }
    if (program->modules.empty() && compile_async) {
        QueueShader(*program, hw_stage, sw_stage, params, runtime_info, binding, 0, true);
        return {};
    }
    if (!program->info_template) {
        // Programs loaded by the pipeline cache warm-up get their template on first use.
        std::unique_lock lk{programs_mutex};
        program->info_template = std::make_unique<Shader::Info>(program->info);
    }
    auto& info = program->info;
    info.pgm_base = params.Base(); // Needs to be actualized for inline cbuffer address fixup
    info.user_data = params.user_data;
    info.RefreshFlatBuf();
    auto spec = Shader::StageSpecialization(info, runtime_info, profile, binding);
    if (aliased && !safe_fetch(info, spec)) {
        ++program_alias_rejected;
        return GetProgram(sel, hw_stage, sw_stage, input_params, binding, false);
    }

    size_t perm_idx = program->modules.size();
    u64 perm_hash = HashCombine(params.hash, perm_idx);

    vk::ShaderModule module{};

    // gow3: consecutive draws of a program almost always use the same permutation.
    auto it = program->last_used < program->modules.size() &&
                      compatible(program->modules,program->modules[program->last_used],spec,info)
                  ? program->modules.begin() + program->last_used
                  : std::ranges::find_if(program->modules,
                        [&](const auto& module) { return compatible(program->modules,module,spec,info); });
    if (it == program->modules.end() && aliased) {
        ++program_alias_rejected;
        return GetProgram(sel, hw_stage, sw_stage, input_params, binding, false);
    }
    if (it != program->modules.end()) {
        if (aliased) ++program_alias_hits;
        program->last_used = std::distance(program->modules.begin(), it);
    }
    if (it == program->modules.end()) {
        if (TakeAheadShader(*program, spec)) {
            info.AddBindings(binding);
            perm_idx = program->modules.size() - 1;
            return std::make_tuple(&info, program->modules[perm_idx].module,
                                   program->modules[perm_idx].spec.fetch_shader_data,
                                   HashCombine(params.hash, perm_idx));
        }
        if (program->pending && required) {
            {
                RequiredWaitTimer waiting{g_gow3_compile_wait_ns, "stage", params.hash};
                program->pending->job.Wait();
            }
            PublishShaders();
            return GetProgram(sel, hw_stage, sw_stage, params, binding);
        }
        const bool failed = std::ranges::find(program->failed_permutations, spec) !=
                            program->failed_permutations.end();
        ASSERT_MSG(!required || !failed,
                   "Required shader {:#x} permutation compilation failed", params.hash);
        if (program->pending || failed) {
            return {};
        }
        if (compile_async) {
            const bool queued = QueueShader(*program, hw_stage, sw_stage, params, runtime_info,
                                            binding, perm_idx, false, spec);
            if (queued || !required) {
                return required ? GetProgram(sel, hw_stage, sw_stage, params, binding) : Result{};
            }
            ASSERT_MSG(std::ranges::find(program->failed_permutations, spec) ==
                           program->failed_permutations.end(),
                       "Required shader {:#x} permutation capture failed", params.hash);
        }
        auto new_info = Shader::Info(hw_stage, sw_stage, params);
        module = CompileModule(new_info, runtime_info, params.code, perm_idx, binding);

        RegisterShaderMeta(info, spec.fetch_shader_data, spec, perm_hash, perm_idx);
        std::unique_lock lk{programs_mutex};
        program->AddPermut(module, std::move(spec));
        NotifyPreparation();
    } else {
        info.AddBindings(binding);
        module = it->module;
        perm_idx = std::distance(program->modules.begin(), it);
        perm_hash = HashCombine(params.hash, perm_idx);
    }
    return std::make_tuple(&program->info, module,
                           program->modules[perm_idx].spec.fetch_shader_data, perm_hash);
}

std::optional<vk::ShaderModule> PipelineCache::ReplaceShader(vk::ShaderModule module,
                                                             std::span<const u32> spv_code) {
    PublishShaders(true);
    PublishGraphics(true);
    std::optional<vk::ShaderModule> new_module{};
    for (const auto& [_, program] : program_cache) {
        for (auto& m : program->modules) {
            if (m.module == module) {
                const auto& d = instance.GetDevice();
                d.destroyShaderModule(m.module);
                m.module = CompileSPV(spv_code, d);
                new_module = m.module;
            }
        }
    }
    if (module_related_pipelines.contains(module)) {
        auto& pipeline_keys = module_related_pipelines[module];
        for (auto& key : pipeline_keys) {
            if (std::holds_alternative<GraphicsPipelineKey>(key)) {
                auto& graphics_key = std::get<GraphicsPipelineKey>(key);
                graphics_pipelines.erase(graphics_key);
            } else if (std::holds_alternative<ComputePipelineKey>(key)) {
                auto& compute_key = std::get<ComputePipelineKey>(key);
                compute_pipelines.erase(compute_key);
            }
        }
    }
    return new_module;
}

std::string PipelineCache::GetShaderName(Shader::HwStage stage, u64 hash,
                                         std::optional<size_t> perm) {
    if (perm) {
        return fmt::format("{}_{:#018x}_{}", stage, hash, *perm);
    }
    return fmt::format("{}_{:#018x}", stage, hash);
}

void PipelineCache::DumpShader(std::span<const u32> code, u64 hash, Shader::HwStage stage,
                               size_t perm_idx, std::string_view ext) {
    if (!EmulatorSettings.IsDumpShaders()) {
        return;
    }

    using namespace Common::FS;
    const auto dump_dir = GetUserPath(PathType::ShaderDir) / "dumps";
    if (!std::filesystem::exists(dump_dir)) {
        std::filesystem::create_directories(dump_dir);
    }
    const auto filename = fmt::format("{}.{}", GetShaderName(stage, hash, perm_idx), ext);
    const auto file = IOFile{dump_dir / filename, FileAccessMode::Create};
    file.WriteSpan(code);
}

std::optional<std::vector<u32>> PipelineCache::GetShaderPatch(u64 hash, Shader::HwStage stage,
                                                              size_t perm_idx,
                                                              std::string_view ext) {

    using namespace Common::FS;
    const auto patch_dir = GetUserPath(PathType::ShaderDir) / "patch";
    if (!std::filesystem::exists(patch_dir)) {
        std::filesystem::create_directories(patch_dir);
    }
    const auto filename = fmt::format("{}.{}", GetShaderName(stage, hash, perm_idx), ext);
    const auto filepath = patch_dir / filename;
    if (!std::filesystem::exists(filepath)) {
        return {};
    }
    const auto file = IOFile{patch_dir / filename, FileAccessMode::Read};
    std::vector<u32> code(file.GetSize() / sizeof(u32));
    file.Read(code);
    return code;
}
} // namespace Vulkan
