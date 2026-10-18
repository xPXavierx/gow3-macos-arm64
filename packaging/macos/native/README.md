# Experimental native ARM64 loader and renderer

A local macOS application launcher is now available. See [desktop launcher build and validation](../launcher/README.md) for the UI, relocatable runtime bundle, persistent dump selection and private AOT profiles.

## Native presentation and material allocations — 2026-10-18

Frame pacing is now evaluated with direct Metal drawable timestamps, separately
from VideoOut submissions. Optional FIFO minimum-duration presentation reduced
the tested five-minute native-4K p95 interval from 32.91 to 20.83 ms. The final
two-buffer run had one approximately 25 ms interval among about 18,000 frames. It does not establish perfectly flat
16.67 ms presentation. Build and measurement instructions are in
[FRAME_PACING.md](FRAME_PACING.md).

The shared native launcher now defaults GOW3_LAZY_COLOR to 1: supported single-level,
single-layer RGBA8 images use restricted linear/sRGB views and omit speculative
shader-write access. A real storage or incompatible integer view promotes the
image, preserving pixels and in-flight views. This extends the earlier MIN-only
mode (3) to the material and screen-effect targets. Explicit environment overrides
remain honored. Original linear/deferred/stale texture-readback rules are retained;
no shader mathematics, internal resolution, mipmaps or effect quality are reduced.

## Native 4K scalar shader optimization — 2026-10-17

Native MoltenVK vertex/fragment programs now emit the exact explicit fma bodies
used by SPIRV-Cross precise scalar arithmetic helpers, avoiding optnone function
boundaries. Other shader stages, divisions, float64, texture filtering and memory
coherency rules remain unchanged. Cache generation 26 invalidates older programs.
The normal strategy is fixed for the session. GOW3_SHADER_AB_TEST=1 is required
for the opt-in frozen-scene diagnostic; active-combat switching produced an invalid
guest descriptor assertion and is not certified. Standard shortcuts clear it.

Paired frozen-scene GPU profiles measured material fragment intervals of
3.20 to 0.78 ms, six depth comparisons 2.21 to 2.04 ms, and the 32-tap screen pass
2.06 to 0.60 ms. A separate paired vertex experiment, retaining the fragment
optimization in both variants, measured material vertex intervals of
1.86–2.00 to 0.68–0.86 ms. Clock-dependent variation was observed; the separate
vertex and fragment intervals overlap and must not be added.

The final clean, HUD-enabled native-4K run averaged 60.00 FPS (60.0–60.0)
in 30 seconds of verified living combat, with zero shader compiles. Earlier
same-process frozen-scene A/B was about 40.4 versus 60 FPS. The completed final
window is bounded; full-campaign or every-scene 60 FPS is not certified. Two longer
automated runs reached character death, and their aggregate FPS is excluded.
One live-switching diagnostic failed and is recorded separately. GPU percentage
is system-wide; frequency was not measured. Frame generation stays off.

Eight GPU regression checks pass with Metal/Vulkan validation: one million
scalar-operation comparisons, normalized/full-range color conversion under both
strategies, constant aliases, detiling and depth/stencil blitting. Six synthetic
SPIR-V modules pass spirv-val. Game-owned frozen-pose captures preserve the
character/world appearance; numerical pixel deltas and diagnostic caveats are
reported separately. See [GPU profiling and precision checks](GPU_PROFILING.md).


## Native MetalFX frame interpolation — experimental — 2026-10-17

Opt-in `MTLFXFrameInterpolator` is compiled and tested on M3 Max. It uses actual
God of War depth/projection, vertex history, HUD separation and explicit native
GPU synchronization. Rendered/generated frame rates are separate; it does not
advance simulation twice. Ordinary launches still leave it disabled. It has
not reached sustained 120 FPS, and 4K output still costs substantial real FPS.
Build requirements, measured limitations and verification are in
[frame interpolation](FRAME_INTERPOLATION.md).

## Retina presentation and automatic Game Mode — 2026-10-17

The native graphical host is now an ARM64 macOS application bundle, recognized
as a game by its own Info.plist. It starts in native fullscreen. System logs
confirmed automatic Game Mode activation for the actual game process. Retina
presentation is enabled by default: the tested game image uses 3456x1944 physical
pixels, rather than the old approximately 1728x972 presentation. The fixed 4K
MetalFX texture and physical presentation size are distinct.

With FXAA Strong and 1440p-to-4K MetalFX, corrected Retina/fullscreen run 163
averaged 58.925 FPS in approximately 40 seconds of active gameplay (57.0–60.0),
with zero shader compiles. This is a bounded scene, not a campaign-wide result.
Earlier FPS numbers below used low-density presentation. All five native
runtime/loader/AOT checks and nine shared-launcher tests passed. No Rosetta, CPU
JIT or generated frames are used.

See [Retina and Game Mode implementation and limits](RETINA_GAME_MODE.md).

## Spatial antialiasing — 2026-10-17

SMAA 1x High and FXAA Quality/Strong are compiled and tested, preserving the
corrected display channels. They use no temporal history or jitter. Same-frame
1440p GPU costs were 0.680 / 0.224 / 0.298 ms, respectively. The pre-Retina Strong
1440p-to-4K combat run averaged 59.425 FPS (58.0–60.0); full-campaign 60 FPS
and complete aliasing elimination are not certified. AA is selectable live in
the port's Image menu and remains off by default in reference configurations.
See [spatial AA integration](SPATIAL_AA.md) for build instructions, measurements,
limitations and retained upstream credits.

## MetalFX spatial output — 2026-10-17

Native MetalFX spatial upscaling is compiled and tested on M3 Max. The initial
RGBA/BGRA color regression is fixed by exporting the presentation view, with
2,880 synthetic color samples and same-frame game-reference checks. Corrected
1440p to 4K averaged 59.825 FPS (59.6–60 samples) during approximately 40 seconds
of combat, with no shader compiles. The older 1080p and GPU-utilization figures
in the integration report precede that color fix and are historical.
These are upscaled outputs, with no generated frames; native 4K/60 remains unmet.
The inherited temporal camera/motion hooks are not calibrated for God of War III.

See [MetalFX integration](METALFX.md) for the opt-in spatial 4K output, input profiles and temporal/frame-interpolation limits.

## Static bounded constants — 2026-10-17

The retained MoltenVK path aliases small, statically addressed read-only buffers
as std140 uvec4 uniform blocks. The original storage descriptor remains the exact
runtime bounds authority; short/null descriptors and index overflow still return
zero. Shared/coherent guest constants keep storage loads, with that choice in the
shader specialization key. Mixed/dynamic access and stages with potentially
aliasing writes are excluded. Uniform spans include descriptor alignment slack
and are limited to 4 KiB. Serial and helper descriptor layouts/writes count the
extra binding without changing logical buffer-offset indices. Hazard tracking
includes UniformRead for the new access class, retaining the existing range and
barrier scheduling policy.

Shader binary generation 20 invalidates earlier caches. The native GPU test
`constant-alias-test` is an explicit EXCLUDE_FROM_ALL target: 100 exact cases cover
short/null bounds, offsets, overflow, coherent fallback and GPU-written data.
Set GOW3_VK_VALIDATION=1 and VK_LAYER_PATH for optional Vulkan core validation.
Build the existing GPU test executables again when Info/Resource layouts change.

The measured material fragment interval was 3.251 to 2.992 ms (54 pipeline pairs
in both short profiles); its vertex interval was 1.998 to 2.044 ms. This is about
8% of that fragment stage, not the whole frame. Main command-buffer intervals
were 26.329 and 25.131 ms, with differing poses. Clean 4K runs vary with draw count;
an aggregate FPS increase is not established and native 4K/60 is not achieved.

The 4K static ARM64 CPU library now uses O2, matching 1080p, with FP contraction
off. CPU use fell in bounded tests but the GPU remains saturated. Both profiles
remain strict AOT with no CPU JIT, interpreter or Rosetta. Output/render dimensions,
texture payloads, filtering and the previous single-sample compatibility default
are unchanged. Full campaign and foliage correctness remain unverified.

Dynamic-index uniform promotion regressed vertex cost and was removed. Restricted
render-target allocations had no demonstrated FPS benefit and were removed.
Forced Metal fast math did not consistently improve the dominant stages and is
not enabled. The default math policy and image allocator remain unchanged.

The color-conversion results below describe the preceding generation-18 baseline.

## Normalized texture color conversion — 2026-10-17

The native renderer now uses a two-interval degree-six FMA approximation of the
post-filter sRGB curve for a whitelist of UNORM texture formats. The original
linear threshold, filtering order and alpha are preserved. Float/HDR, BC6,
integer/scaled formats and custom-border samplers keep the full-range log2/exp2
path. This is a numerical approximation, not a bit-exact identity. On the M3 Max,
65,536 independent GPU inputs, including threshold and interval-join neighbours,
had maximum absolute error 1.91696296e-7 against the double-precision reference,
within the 5e-7 test limit. The full-range test also passed unchanged.

Image sample range and custom-border status participate in shader specialization;
rebinding these resources cannot reuse a shader with incompatible assumptions.
Shader binary generation 18 invalidates older disk and driver pipeline caches.
No texture files, internal resolutions, filter settings or MSAA defaults change.
The coefficients fit 10,000 evenly spaced samples in each nonlinear interval
[0.04045, 0.35] and [0.35, 1], converted from a Chebyshev fit to the power basis.
GPU testing measures error; it is not a formal proof over every possible float.

In native 3840x2160, two targeted postprocess stages measured 2.872 to 2.054 ms
and 2.381 to 1.745 ms. An unmodified two-output stage stayed 1.909 to 1.903 ms.
The complete main command buffer measured 28.733 to 26.329 ms in these short
instrumented combat windows, whose poses/pass counts differ. Stages overlap and
must not be summed as hardware utilization.

Clean 4K game-present tests with the HUD enabled measured 37.10 FPS in a repeated
reference combat run and 40.37 FPS in the updated run, each over 30 seconds of the
same scripted route. Average draw counts were 1,762 and 1,764. These are not exact
frame replays; the observed 8.8% FPS increase is scoped to this autosave. The GPU
remains saturated in 4K, and 4K/60 FPS is not achieved. The CPU libraries and
strict static ARM64 execution are unchanged (1080p O2; 4K O1).


The final 1080p check retained 60 FPS in all eight reported samples of 40 seconds
of combat and all samples of a 25-second Pause; system GPU utilization averaged
73.1% during combat (67–76%, system-wide, not per-process peak throughput).
Warm-cache audit 127 checked 126 images / 164,428,584 CPU-owned bytes and 1,199 mip
instances with zero mismatches above tolerance and zero Vulkan core errors.
These bounded checks do not certify full campaign coverage or the outstanding
foliage geometry artifacts. Both native CPU profiles report zero translated blocks
and zero instructions interpreted from translated code.

## Previous single-sample baseline — 2026-10-17

MoltenVK now selects the existing single-sample compatibility path by default.
With guest MSAA enabled, the main-menu background has a regular grid of seams;
the single-sample path removes it in the inspected captures. This reduces edge
antialiasing. The multisample depth/resolve path remains unresolved. Internal
resolution stays 1920x1080 with no upscaler. `GOW3_NO_MSAA=0` is an experimental
override. Shader-cache generation now includes this mode, preventing reuse of
programs from the opposite image-sample configuration.

The final native build produced 60 FPS in all reported samples of a 30-second Pause
and 40-second combat test, without captures, profiler counters or shader compiles
in the timing window. The prior renderer also reaches this measured 60-FPS result
when using the same single-sample path. This result must not be attributed to the
rejected allocation or grouped-read experiments.

A separate bounded command-buffer timing test covered 19.999 seconds and 8,401
buffers, with no invalid timestamps. Merging overlapping intervals across queues
yielded 14.17 ms of command-buffer time per 60-Hz frame and 2.49 ms of free intervals.
This includes in-buffer waits and possible preemption; it is not execution-unit
utilization or a measurement of peak GPU throughput. Substantial GPU headroom and
higher-resolution 60 FPS remain unproven. Full-game coverage and all foliage
artifacts remain unverified.

Texture audit 116 matched 164,428,584 bytes across 126 CPU-owned images and 1,199
sampled mip instances within tolerance. Five 2D blade maps matched all 45 mips and
1,048,608 bytes exactly. Cubemap verification covers one base face only. Vulkan
core validation reported zero errors. Eight native GPU test groups, the MSAA
default/register-size test and SPIR-V validation passed. Executable, renderer and
recompiled game library are ARM64; logs report no translated or interpreted CPU
instructions. Raw game-derived material stays outside source repositories.

Command-buffer timing is available as the opt-in `metal_busy_probe.mm` described
in GPU_PROFILING.md; normal launches do not inject it. Historical results below
belong to their stated build and configuration.


## Native GPU profiling and measured corrections (2026-10-16)

Native Metal stage-boundary counters isolated two costs: repeated post-filter
force-degamma pow operations (up to 96 per pixel in one 960x540 pass), and
attachmentless vertex-only rendering scopes requesting 16384x16384 framebuffers.
The positive nonlinear sRGB branch now uses exp2(log2(base)*2.4), retaining the
threshold, linear branch, filtering order and alpha. Shader binary version 11
invalidates older programs; no blanket fast-math or memory-guard relaxation is used.

No-fragment/no-attachment draws discard rasterization and use a 1x1 scope while
retaining vertex/tessellation storage effects. A stricter pure-draw elimination
requires no observable outputs, memory writes, atomics, DMA or active queries.
Both persistent image/buffer resource descriptors and transient summary flags are
checked, including cached shaders. Liverpool still uses its original synthetic
occlusion counters; adding real queries requires gating these rules on them.

The reference and optimized native stage profiles contain 43 and 48 complete
main command buffers: mean GPU duration 26.779
to 15.715 ms; fragment sum 22.075
to 11.957 ms, with 89 to 75 render scopes. The half-resolution
shader 107d87f9d56574cf fell from 2.445 to 0.811 ms; full-resolution shader
1b4f84d1cc5f5f9e from 1.701 to 0.646 ms. These are instrumented profiles, not FPS
benchmarks; vertex/fragment stages overlap. Active combat still averaged 16.994 ms
for the main GPU command buffer. Remaining heavy material passes are identified;
sustained 60 FPS in combat is not established.

Clean game-present benchmarks used the same initial AUTOSAVE, native 1920x1080,
no upscaler, O2 AOT CPU library, engine 60 and Metal HUD. After 20 seconds warm-up,
60-second Pause runs measured 37.346 FPS with the pre-change renderer and 60.000
in two optimized runs. A later warm-cache Pause run measured 57.100 FPS,
and a subsequent cold-cache run measured 59.633 FPS. Draw-command counts varied
with enemy poses (1,903 to 2,129 per frame), so this is not an exact-frame replay.
A separate warm-cache GPU profile retained 75 passes and 15.703 ms. No screenshots, counters, builds or shader compilation
occurred in those windows. A 40-second scripted combat run averaged 58.425 FPS;
the latest warm-cache combat check averaged 56.000, range 54.8–57.4.
Pause enemy poses are not bit-identical. These bounded initial-save tests do not
certify prolonged combat, other levels, 4K or independent Windows parity.

The native renderer was compiled and eight GPU regression groups passed, including
a 65,536-input independent sRGB reference and exact vertex storage-write/guard
preservation across discarded-area reduction. Warm-cache game auditing with core
Vulkan validation reported zero errors: 126 images / 166,176,208 bytes matched
memory-to-GPU; sampling checked 1,199 mip instances / 1,227,776 components with
zero mismatches above the stated tolerance. Five 2D blade maps matched all 45 mips
and 1,048,608 bytes against the dump; only one cubemap face base level is certified.
Required linear/deferred readbacks and native CPU execution remain enabled.

The optional source-only probes and reproduction steps are in
[GPU_PROFILING.md](GPU_PROFILING.md). They are not injected by normal launches.
All game assets, game shader binaries, generated CPU code and memory captures remain
outside the source tree and archive. Earlier CPU-only results below are historical;
they did not improve FPS, whereas the GPU changes above do.

## ARM64 CPU optimization and Windows comparison (2026-10-16)

The Windows runtime directly executes the PS4 x86-64 executable, as documented in
[upstream How it works](https://github.com/FelipePrado0/gow3_pc#how-it-works).
The ARM64 build statically recompiles those instructions before launch. It still
preserves guest register, flag and memory-order semantics; it is not a source-level
engine port. Strict AOT rejects missing code instead of falling back to a CPU JIT
or interpreter. Graphics retain PS4-to-Vulkan reconstruction and MoltenVK-to-Metal.
This architectural difference explains possible costs, not the measured GPU gap by
itself. The user's Windows figures have not been independently reproduced here.

A process/thread CPU-time measurement in the same initial-save Pause isolated a
large compiler cost. An A-B-A comparison used the original O1 library, the existing
O2 library and O1 again, with source/header/compiler fingerprints checked. Each run
had 20 seconds warm-up and 40 seconds measurement. The host and renderer stayed
unchanged, with no builds during measurements. Periodic game-only captures and
one four-second stack sample were enabled equally in all three runs.

| Library | Process CPU, normalized over 14 cores | Game-present FPS |
| --- | ---: | ---: |
| O1 reference, run 74 | 29.29% | 37.35 |
| O2, run 75 | 16.75% | 37.59 |
| O1 repeat, run 76 | 30.11% | 38.25 |

O2 reduced process CPU time by approximately 43.6% against the two O1 runs.
The five game worker threads together used 2.10
core-seconds per second with O1 and 0.80 with O2.
The command processor and recording-thread costs stayed similar. GPU system
utilization remained around 98%; no meaningful FPS gain was established. The
previous Metal encoder report pointed to fragment-stage cost. The later GPU
profiling section above identifies measured shader and render-scope costs; complete
Windows/Mac parity is still unverified.

The compiler now defaults to O2, retaining `-ffp-contract=off`,
`-fno-strict-aliasing` and the existing memory fences. O1 remains selectable for
bisects. The local 1080p launch library was replaced with the tested O2 build;
the existing 4K library remains O1. No bounded run reported a native fault, and the
O2 Pause capture shows the scene and blade materials. Existing AOT/loader fixtures
already compile at O2. Full-game behavior and sustained combat still need testing.

## Apple Silicon resource monitoring (2026-10-16)

The performance overlay now reads process CPU time with `getrusage`, per-thread
CPU time with Mach, process physical footprint with `task_info`, and the optional
AGX `Device Utilization %` counter through IOKit. CPU total is normalized across
available cores; the busiest thread uses one core as 100%. The GPU percentage is
explicitly labelled **system** because it covers the entire GPU, including other
apps. An unavailable driver counter stays `n/a`; FPS is never used as a substitute
for utilization. GPU allocation budget is labelled shared on unified-memory Macs.

The hardware check observed 7.1% total CPU and 100% busiest-thread usage for one
busy thread on the 14-core M3 Max, compared with 0.0% while sleeping. The overlay
format test, controller tests and five RelWithDebInfo native loader CTests passed.
The Python compiler fixtures inherit CMake's selected SDK instead of relying on
the standalone Xcode compiler to locate its own sysroot.

The original host sampler's non-Windows fallback only supplied a GPU name. This
was a missing monitoring implementation, not evidence of zero CPU or GPU usage.
The local launcher now uses the compiled RelWithDebInfo ARM64 host runtime; the
AOT game library initially remained at O1; the later CPU comparison above
selects O2 for the local 1080p build. No CPU-only FPS gain was established.

In initial-combat diagnostics, the GPU counter reached 96–99%, with game CPU near
29–34% of the machine and the busiest thread near 77–80%. Metal's five-second
encoder report measured approximately 23.46 ms fragment, 3.82 ms vertex and
0.42 ms compute time, against 2.71 ms command-buffer CPU encoding. Encoder timing,
insights and sampling add overhead; their FPS values are diagnostic only. The
results point to graphics work in this scene, not a shortage of CPU worker threads.

A controlled Pause comparison of a clamped-load shader variant measured 37.74 FPS
for the reference and 37.56 FPS for the candidate. Both passed GPU bounds checks,
but the variant showed no benefit and was removed. Production memory guards remain
unchanged. These single-pair numbers are not a combat benchmark or a claimed gain.

`GOW3_HOST_STATS=1` emits sampled resource values every five seconds. The ordinary
panel uses the same sampler without logging. `GOW3_PAD_TRACE_FILE` is an optional
bounded delivery trace; its timestamps measure pad sampling, not animation onset.
The reported dodge animation delay remains unresolved.

A separate native 4K AOT image was compiled with the upstream `Resolution Patch -
4K` and video-skip patches, using the required 11200 MiB direct-memory pool. It
reached combat and the user confirmed the sharper image and higher cost. Its first
FPS comparison was invalidated by a scene change and is excluded. The normal 1080p
profile remains the performance reference. The original texture-memory patch and
the 4K resolution patch are mutually exclusive.

The opt-in resolution trace now distinguishes the upscaler's proxy **setting**
from actual color/depth render-target extents; the previous `scene` label could
remain at 1080p with an upstream resolution code patch. Geometry target and render
area logging reads actual renderer state without changing it.

## Shared launcher policy and measured comparison (2026-10-16)

The ARM64 path now runs the original `run.py`. Its native adapter verifies prepared
inputs instead of relinking or applying new instructions behind the AOT compiler.
Graphics option parsing, failed-restart rollback, patch selection/requirements,
content/update/mod views, glyph setup and per-start save backups use the original
functions. Code patches must already be baked into the native image; data-only
initialization patches still use the original runtime patch table. This restores
`Bug Fix - Startup Black Screen`, previously absent from direct native launches.
The supervisor re-reads options after native restart exit 75; lock release/re-entry
and the shared startup policy are covered by synthetic integration tests.

Linear image readbacks, deferred/stale readbacks and async shader compilation now
follow upstream startup rules. Draw preparation and the draw pipeline use upstream
automatic sizing (7 preparation workers on this Mac). Full barriers are opt-in again.
The engine rate is derived from patches actually applied: 60 FPS in the tested AOT
build. The session preference remains fixed 1920x1080 render/output with Metal HUD.

Enabling deferred readbacks exposed a port bug: the queue rounded ranges to 4 KiB,
while Apple Silicon page protection uses 16 KiB. A CPU write on another 4 KiB subpage
kept faulting without releasing the copy's watcher. The queue now uses the same
`TRACKER_PAGE_BITS` as PageManager. A regression covers sibling-subpage release,
preservation of delayed publication and large-copy waits on 16 KiB pages, alongside
the original 4 KiB cases. The first stalled test is excluded from timings.

A-B-A-B tests used the same AUTOSAVE and Pause, 20 seconds of warm-up and 60 seconds
of timing per run. The old configuration already had the texture readback fix.
Its means were 14.99 and 15.01 FPS; restored-policy runs
were 36.48 and 35.04 FPS. Group means: **15.00 -> 35.76 FPS
(2.38x)**. This recovers the approximately 36 FPS the user had already observed
before the texture fix; it is not a demonstrated gain beyond that historical rate.
These are paused-scene game-present measurements, not active-combat
FPS. Enemy poses differ slightly because Pause is automated; periodic game-target
screenshots remain enabled equally in both groups. Individual switch costs were
not isolated. No CPU compilation or texture audit ran during timing windows.

Two additional native boots, cold and warm pipeline cache, each matched five blade
maps / 45 mips / 1,048,608 GPU bytes directly to the local DDS payloads: zero
mismatches and zero Vulkan core validation errors. Scene captures were inspected.
The sixth cube map's sampling and the full game are not certified by this check.
89 unique Python checks, six renderer groups, the readback-queue regression and
five native CTests passed. The memory-allocation CTest required normal macOS
shared-memory access after failing in the sandbox. Execution logs retain zero
translated blocks and zero interpreted instructions from translated code.

A final normal boot exercised movement and attacks, confirmed Pause afterward and
rejected a real concurrent second launch without changing the active log or PID.
It used the Metal HUD without the heavy texture audits and exited intentionally
with code 0. Game-target captures of combat and the final Pause were inspected.

The remaining portability limits, including nonzero sampler LOD bias and unsupported
centroid/sample barycentric reads, remain explicit. This is a tested launch-policy
alignment with the previous approximately 36 FPS recovered, not full-game completion
or a 60 FPS guarantee.


## Texture upload correction (2026-10-16)

The cause of the recurring world and blade corruption in the initial save is now
established. The original `run.py` sets `GOW3_READBACK_LINEAR=1` and explicitly
documents that the game needs linear-image readbacks for correct textures. The
native direct launcher bypassed that setup and the renderer default was false.

GOW3 prepares tiled texture bytes in a staging allocation, then runs a compute
copy through large linear `R32G32B32A32Uint` storage images over its texture pool.
Compressed material images are populated from guest memory at offsets within that
pool. With linear-image readbacks disabled, those guest destinations retained old
data. Comparing GPU images against that stale RAM could pass without comparing
the actual asset intended for the material.

Private residency checks found all six blade-resource markers. The five 2D maps
were verified through all nine mips in staging: 1,048,608 bytes, zero mismatches
against the player's DDS data. Their draw destinations still held unrelated old
data. The shader's flattened resource table matched a fresh walk of the live
command registers, excluding that particular stale-table hypothesis.

Changing only `GOW3_READBACK_LINEAR` to 1 in the existing build corrected the scene
and made all five bound 2D maps match the dump through every mip (case 50). The
game-specific renderer now defaults this setting to true, so direct launches match
the original launcher. Explicit `GOW3_READBACK_LINEAR=0` is a negative control and
reproduces the defective upload path.

The compiled default was then tested with the variable unset and persistent
pipeline caching disabled (case 51). Both corrected boots reached combat and Pause,
matched five maps / 45 mip instances / 1,048,608 bytes with zero differences, and
reported zero Vulkan core validation errors. Game-target screenshots show the
world without the previous rainbow stripes. All six renderer regression groups
and all five native-loader CTests passed with the final binaries.

A third boot used the default renderer, requested the Metal HUD, and ran movement
and attack inputs without the memory/texture audits. Inspected game-target frames
retained correct world textures. Automated combat reached the player death screen;
there was no application fault, and the test was stopped intentionally with exit 0.

This establishes the correction for the captured corruption and tested material;
it does not certify every scene, shader effect or the sixth map's cube sampling.
Further performance work and full-game validation remain pending; see the shared-launcher comparison above. The temporary
whole-memory, file-I/O, transfer and live-register probes were removed from the
shipping sources after collecting evidence. Bounded opt-in image tracing remains.


## Dependencies and builds

Start from gow3_pc `7180a7bafa51afbe991523f6392f69ea1cf03e1e` with the
consolidated port patch applied. Install ARM64 SDL3, FFmpeg, Vulkan headers/loader,
CMake >=3.24 and Ninja; Apple Clang, Python 3 and Git are required.
A Homebrew installation under `/opt/homebrew` can provide the host dependencies:

```sh
brew install cmake ninja sdl3 ffmpeg vulkan-headers vulkan-loader
```

Follow [the AOT dependency instructions](../aot/README.md#reproduce) to obtain
and patch the pinned recompiler, and build Zydis/Zycore into
`out/aot-deps/install`. Then run the following from the gow3_pc root:

```sh
git submodule update --init --recursive
python3 packaging/macos/native/build_graphics_deps.py \
  --source-directory out/graphics-deps --prefix out/aot-deps/install

cmake -S gpu -B out/gpu-arm64 -G Ninja \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DGOW3_LTO=OFF \
  -DCMAKE_PREFIX_PATH="$PWD/out/aot-deps/install;/opt/homebrew"
cmake --build out/gpu-arm64 --target gow3gpu srt-arm64-test buffer-bounds-test barycentric-test null-storage-test texture-detile-test irq-concurrency-test perf-overlay-test macos-host-stats-test --parallel 6

cmake -S packaging/macos/native -B out/macos-native -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=/usr/bin/clang \
  -DCMAKE_ASM_COMPILER=/usr/bin/clang \
  -DCMAKE_PREFIX_PATH="$PWD/out/aot-deps/install;/opt/homebrew" \
  -DGOW3_RECOMP_SOURCE_DIRECTORY="$PWD/out/aot-deps/recomp" \
  -DGOW3_GPU_LIBRARY="$PWD/out/gpu-arm64/libgow3gpu.dylib"
cmake --build out/macos-native --parallel 6
ctest --test-dir out/macos-native -V
out/gpu-arm64/srt-arm64-test
```

For a CPU-only development build, use `-DGOW3_GPU_LIBRARY=`; the small headless
adapter then satisfies graphics exports without pretending to render anything.
The full graphics build uses fmt12.0.0, robin-map1.4.0, VMA3.3.0,
xxHash0.8.3, Boost1.89.0 and glslang16.1.0, as pinned in the helper script.
Use ARM64 CMake/Ninja/Python and verify their architecture before building.
For universal tools, invoke them with `/usr/bin/arch -arm64`; set
`-DCMAKE_MAKE_PROGRAM=/absolute/path/to/native/ninja` when configuring.
Boost.Context explicitly selects AArch64 assembly. This Mac's latest loader and
renderer builds used local CMake 3.31.8 and Ninja 1.13.1 in native ARM64 mode.

Both patches were also applied to clean pinned source trees, and the loader,
CPU tools and native tests were rebuilt from those patched sources. The full
renderer was built from the main modified checkout, not from that second tree.
The commands were exercised on this Mac with the dependencies installed in a
local prefix outside the repository. A clean installation on another Mac has
not been tested. Tests need normal desktop-process access to shared memory.

## Prepare a local dump and attempt a diagnostic boot

The input must be an extracted, plaintext dump containing `eboot.bin`,
`sce_module/libc.prx`, `sce_module/libSceFios2.prx` and the game's data files.
Encrypted retail PKGs are not handled by these scripts. Keep generated game
source and binaries local. Allow several minutes and several GB of workspace.

```sh
GAME="/absolute/path/to/CUSA01623"
PRIVATE="$PWD/../gow3-private-game"
python3 packaging/macos/native/prepare_game.py "$GAME" \
  --out "$PRIVATE" --tools out/macos-native/aot \
  --patch "Skip Any Video With X Button" \
  --patch "Bug Fix - Texture Corruption Fix"
python3 packaging/macos/native/compile_game.py "$PRIVATE/aot" \
  --recomp-source out/aot-deps/recomp \
  --decoder-include out/aot-deps/install/include --jobs 6

GOW3_NATIVE_AOT_DIR="$PRIVATE" GOW3_DATA_DIR="$PRIVATE" \
  GOW3_GAME_DIR="$GAME" GOW3_USER_DIR="$PRIVATE/user" \
  GOW3_PROBE="$PWD/out/macos-native/gow3-probe-arm64" \
  GOW3_RECOMP_LIB="$PRIVATE/aot/libgow3-recompiled.dylib" \
  GOW3_TIMEOUT=1800 python3 run.py --cpu-only
```

The CPU-only command exercises startup without rendering. Use the graphics run
below for menu/gameplay testing; reaching the menu does not establish full playability.
`--user` isolates experimental state from existing saves. The dump is read only.
After changing loader patches or the dump, regenerate and recompile the image;
the loader rejects stale code hashes. The compiler caches objects using source,
header and compiler-version hashes.

For graphics startup, use the shared launcher rather than invoking the probe directly.
Create the initial test profile once, using the original settings writer:

```sh
python3 - "$PRIVATE/gow3.ini" <<'PY'
import sys
sys.path.insert(0, 'scripts')
from graphics_options import write_keys
write_keys(sys.argv[1], {'render_resolution':'native', 'engine_fps':'60',
                       'output_res':'1920x1080', 'upscaler':'none', 'preset':'0',
                       'live_resolution':'0'})
PY
export VK_DRIVER_FILES="/absolute/path/MoltenVK/MoltenVK/dynamic/dylib/macOS/MoltenVK_icd.json"
export DYLD_LIBRARY_PATH="$PWD/out/gpu-arm64"
export GOW3_NATIVE_AOT_DIR="$PRIVATE" GOW3_DATA_DIR="$PRIVATE"
export GOW3_GAME_DIR="$GAME" GOW3_USER_DIR="$PRIVATE/user"
export GOW3_GPU_USER_DIR="$PRIVATE/user" GOW3_CONFIG="$PRIVATE/gow3.ini"
export GOW3_PROBE="$PWD/out/macos-native/gow3-probe-arm64"
export GOW3_RECOMP_LIB="$PRIVATE/aot/libgow3-recompiled.dylib"
export GOW3_TIMEOUT=1800 MTL_HUD_ENABLED=1
# These were diagnostic overrides; normal runs use original automatic/default policy.
unset GOW3_PREP_WORKERS GOW3_DRAW_PIPE GOW3_FULL_BARRIERS GOW3_READBACK_LINEAR GOW3_RENDER_RES
python3 run.py
```

`run.py` derives the memory/VBlank requirements and actual engine rate from applied
patches and performs the original save backup before starting. Native code patches
must match the prepared AOT image. An update/mod changing `eboot.bin` or a linked
module is rejected until prepared and compiled again; content-only overlays reuse
the original mod/update machinery. Changed settings that do not alter code use the
same graphics state/rollback flow. The ARM64 supervisor handles restart exit 75
without opening a second GPU process. Host Python, loader and AOT library must be
ARM64; no Rosetta or CPU fallback is selected. Direct probe invocation remains a
low-level diagnostic and does not apply the launcher's game policy.

Preserve the archive directory structure. The tested MoltenVK-macos.tar SHA256
is `f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e`.
The renderer uses explicit shader buffer bounds and ordinary descriptors when
`robustBufferAccess2` is absent; it no longer stops merely because that feature is
missing. Full-game compatibility remains unverified; see the current assessment.
Exit30 is a strict CPU coverage stop; exit20 is an unresolved PS4 import.
A timeout only shows the process was still running, not that the game booted.

`GOW3_GPU_USER_DIR` must be set before launch because the renderer initializes
its paths when the dynamic library loads. The capture option writes the selected
new game frame to `user/screenshots`; it never captures the desktop. A black
initial image is not evidence of a rendered menu or scene.

## Separate native 4K profile

Prepare a separate directory outside the source checkout. Do not reuse a 1080p
AOT library with new executable resolution patches. The shared launcher rejects
runtime instruction patches that differ from the prepared image.

```sh
PRIVATE="$PWD/../gow3-private-game-4k"
python3 packaging/macos/native/prepare_game.py "$GAME" \
  --out "$PRIVATE" --tools out/macos-native/aot \
  --patch "Skip Any Video With X Button" --patch "Resolution Patch - 4K"
python3 packaging/macos/native/compile_game.py "$PRIVATE/aot" \
  --recomp-source out/aot-deps/recomp \
  --decoder-include out/aot-deps/install/include --jobs 6
cat > "$PRIVATE/native-display.ini" <<'INI'
engine_fps=60
upscaler=none
preset=0
output_res=3840x2160
render_resolution=4K
live_resolution=0
show_fps=1
overlay_gpu=1
overlay_cpu=1
INI
```

Use the graphics launch environment from the 1080p example with this `PRIVATE`
and `GOW3_CONFIG="$PRIVATE/native-display.ini"`. The 4K patch already supplies the
larger video arena; do not also select the 1080p texture-arena patch. The original
patch requirement parser supplies 11200 MiB DMEM automatically. Copy only the
wanted test save into the new user directory before its first launch, then keep
the two profiles independent. Code derived from the dump is never redistributed.

## Native 4K above 60 FPS

The existing upstream 120 FPS patch (kvicken) adjusts the engine's time clamp
and swing-energy calculation. It must be baked before AOT compilation; changing
only VBlank or the presentation limit is insufficient. Prepare a separate profile
with all three patches below, then compile it with the same ARM64 compiler command
as the 4K example:

```sh
PRIVATE="$PWD/../gow3-private-game-4k-120"
python3 packaging/macos/native/prepare_game.py "$GAME" \
  --out "$PRIVATE" --tools out/macos-native/aot \
  --patch "Skip Any Video With X Button" --patch "Resolution Patch - 4K" \
  --patch "Frame Rate Patch - 120 FPS"
```

Use `engine_fps=120`, `fps_limit=120`, `vsync=0` and the other fixed-4K settings
from the preceding example. Keep `GOW3_FRAMEGEN=0`, `GOW3_UPSCALER=none` and
`GOW3_CAPTURE_FRAME=0`. The shared launcher derives 120 Hz VBlank from the baked
patch and verifies the prepared instructions. This removes the 60 FPS ceiling;
it deliberately retains a 120 FPS ceiling. Rates above 120 and complete-game
physics are not certified. The swapchain logs both supported modes and the
actual selection; the tested MoltenVK exposes FIFO and Immediate. Immediate
permits tearing. Game-owned screenshots cannot certify physical display scanout.

## Optional independent stencil attachment

`GOW3_SPLIT_DEPTH_PLANE=1` enables removal of a depth attachment that has no
effect, while preserving the stencil attachment, comparisons, color blending,
pending depth clears and the combined backing image format. Unsupported host
depth-bounds testing cannot require an otherwise unused depth plane. Meaningful
depth tests/writes/bounds, stencil zfail dependencies and HTILE maintenance keep it.

Stock MoltenVK 1.4.2 infers a Metal pipeline depth format from a combined stencil
format even when dynamic rendering explicitly declares no depth attachment.
The optional renderer path requires the included driver correction; leave the
environment option unset with the stock driver. Build a separate ARM64 driver:

```sh
python3 packaging/macos/native/build_moltenvk.py \
  --source-directory "$PWD/../moltenvk-source" \
  --output-directory "$PWD/../moltenvk-arm64"
```

The script pins public MoltenVK v1.4.2, applies the small Apache-2.0 source patch,
builds with CMake/Ninja, verifies ARM64 and writes an ICD and build manifest.
It does not install system libraries. Xcode 16.4/macOS SDK 15.5 was tested.
Use that ICD as `VK_DRIVER_FILES`, together with `GOW3_SPLIT_DEPTH_PLANE=1`.
Other drivers retain the previous renderer path unless explicitly opted in.
GPU pipeline key generation is bumped so old combined-only pipelines are not reused.

Build `stencil-plane-test` and `depth-stencil-state-test` explicitly. The Metal
test compares 4096 masked MIN-blend pixels with/without a depth attachment and
checks every preserved depth value and stencil byte. Run with `MTL_DEBUG_LAYER=1`.

## Compiler coverage and synchronization diagnostics

Preparation emits `aot/coverage-summary.json` grouped by mnemonic and
`coverage-sites.local.json` with local addresses. These include data mistaken for
code and unreachable paths; the counts do not measure game completion.
`GOW3_CAPTURE_EVERY=N` optionally captures every Nth newly presented game frame
for bounded diagnostic runs; leave it unset for normal testing.
`GOW3_TRACE_GPU_WAITS=1` reports an unmet graphics memory wait after one second.
`GOW3_TRACE_EQUEUES=1` traces event creation, delivery and waiting; it changes timing
and must not be treated as a synchronization fix. The latest strategy is described
in `docs/macos-arm64-strategy.md`.

The optional `irq-concurrency-test` renderer target exercises simultaneous first use
of 65 interrupt IDs and persistent/one-shot handler delivery and removal. Run it with
`GOW3_GPU_USER_DIR` set to an isolated directory, alongside the SRT and bounds tests.

## Opt-in code patches and graphics regressions

`--patch` (alias `--code-patch`) accepts an exact upstream patch name and may be repeated. It bakes
validated writes into both the local loader image and the scanner ELF before AOT
generation. Recompile afterward; applying a code patch only at runtime leaves native
functions stale. Overlaps, loader relocations, linker-modified sites and non-file-backed destinations are rejected. The original dump is never changed.
The skip-video patch is credited to cuesta4 and illusion0001/illusionyy in the
upstream XML. Press Cross (Space on the default keyboard mapping) to skip a video.

```sh
python3 packaging/macos/native/verify_aot_patches.py
cmake --build out/gpu-arm64 --target barycentric-test null-storage-test --parallel 6
# Use the same MoltenVK manifest and isolated GPU user directory as above.
out/gpu-arm64/barycentric-test 32
out/gpu-arm64/barycentric-test 128
out/gpu-arm64/barycentric-test 512
out/gpu-arm64/null-storage-test
```

The Metal barycentric path reconstructs linear weights from perspective weights
and the derivatives of their affine numerators, at the shader prologue before
guest branches. Three nonuniform-W triangle sizes were checked against analytic
pixel values (68,878 pixels, absolute tolerance 0.0001). This is FP32 reconstruction,
not a guarantee for all extreme geometry. Centroid/sample barycentric reads remain
explicitly unsupported on Metal. Per-vertex fragment inputs remain a separate limit.

True null images use separate shader specializations. Loads and atomics return zero;
image stores are omitted. Null/live shaders cannot share a cache permutation. GPU
readback checks that the bound stand-in remains unchanged while a live image remains
writable. Invalid nonzero storage-image addresses and null depth images still stop
explicitly. Shader binary and pipeline cache versions were bumped.

The preparation manifest `aot-patches.json` includes required runtime settings
from the upstream patch metadata. The texture patch requires `GOW3_DMEM_MB=7104`
(retail 5056 MiB plus 2048 MiB). Apply these requirements before starting the loader.

## Gameplay validation and current display settings

The initial combat is reached from New Game with video skipping or from the local
AUTOSAVE. The texture upload correction above resolves the captured scene and blade corruption.
Before that correction, world textures and blade materials were incorrect in repeated runs.
Full GPU barriers, disabling persistent shader caching, and disabling deferred uploads
have each failed to eliminate the corruption. An earlier visually improved run could
not be reproduced with the same optimization mask; none of those switches is a proven fix.
Full barriers now follow the upstream opt-in default. The earlier diagnostic default
was removed after restoring the required texture-upload path.

Use native output 1920x1080, preset 0 and upscaler none in the test configuration;
keep `GOW3_RENDER_RES` unset because it selects the older patched-resolution path.
For the native-size test, the original game allocations are already 1920x1080,
so no resolution patch is needed for this setting. Resizing the window still scales
the final presentation. `MTL_HUD_ENABLED=1` enables Apple's performance HUD for the
child process only. No global system preference is changed.

Run `out/gpu-arm64/texture-detile-test` with the same Vulkan driver manifest. It checks
production detiling against synthetic Morton layouts (four mips/two layers), including
64/128-bit compressed-block payloads; macro tiling is not covered by that fixture.

The optimized RelWithDebInfo renderer passed all six GPU/runtime checks. The user
reported about 36 FPS during combat versus about 13 FPS with Debug; this is an
observed comparison, not an isolated compiler-optimization benchmark. The Metal HUD
was confirmed by the user. `GOW3_TRACE_RESOLUTION=1` verified scene and display
stayed at 1920x1080 while the presentation surface changed with the window.
Run `texture-detile-test --batch` to check 224 queued conversions (38,297,600 bytes)
without waiting between images. This passes on M3 Max but does not reproduce the
remaining game corruption or cover every macro/depth/MSAA layout.

## Historical texture diagnostics (superseded)

These notes describe tests before the confirmed upload correction above.
Their unresolved-cause statements are historical, not current texture status.

### Texture diagnostics (2026-10-16)

`GOW3_VERIFY_MICRO_DETILE=512` enables a bounded CPU/GPU comparison of real
Thin1DThin uploads. It records counts, not texture payloads. It forces GPU waits
and covers only validated whole micro-tile ranges; it cannot exclude scheduling
races, wrong source data, other tiling modes, or shader sampling errors. The real
combat run checked 362 uploads / 300,835,840 bytes with zero differences while
world corruption remained visible.

The expanded `texture-detile-test --batch` queues 224 conversions and also tests
BC1/BC3 upload/download through production image transfers. It passed on M3 Max.
These are diagnostic advances, not a visual fix. Disabling renderer worker threads
and asynchronous shaders also failed to remove the corruption in the real game.

For pass-by-pass diagnosis, combine `GOW3_DIAG_IMAGES=1`, a private
`GOW3_DIAG_DIR`, `GOW3_CAPTURE_TRIGGER=<file>` and
`GOW3_CAPTURE_DISPLAY_PASSES=4`. Creating the trigger records across four display
pass boundaries, because this game can revisit a display target before drawing the
scene. This can span more than one presented frame. Keep raw captures private; they
contain game images and buffer contents and do not belong in source packages.

### Historical texture investigation: measured boundaries (2026-10-16)

The following records the investigation before the texture upload correction above.
Its unresolved conclusions describe the earlier builds, not the current fix.


The visual defect is still reproduced. The port is not complete, and none of the
changes below is claimed to be the fix for the corrupted scene or blades.
Historical pre-alignment state: performance work was deferred. Execution remains native ARM64, with the default O1
AOT game library, fixed 1920 x 1080 rendering and no Rosetta or CPU fallback.

The investigation now checks separate boundaries instead of judging settings by
one screenshot. In a single-process, paused gameplay capture, 126 CPU-owned BC
images matched their source at all captured mips: 185,225,848 valid compressed
bytes, with zero mismatches. A separate host compute shader then fetched through
the actual bound image views. Across 126 views and 1,171 mip instances, 1,199,104
RGBA components agreed with independent BC1/BC3 decoding. The maximum difference
was 2/255; no sample exceeded the declared 0.02 tolerance. The scene was visibly
corrupted before that diagnostic was armed.

This is bounded evidence. Sampling checks use a 16 x 16 grid at each mip with
texelFetch, not the material shader's coordinates, derivatives or sampler state.
They do not establish that the correct resource was selected for a material.
GPU-generated render targets, GPU-owned buffers, pipeline vertex formats/strides,
all push constants and the material calculation remain outside that proof.
A separate bounded readback of 512 CPU-owned buffer bindings, including 84 vertex
ranges, matched 2,911,628 bytes. Each range is limited to its first 64 KiB; that
sample included earlier depth passes and is not proof for every affected draw.

Vulkan core validation identified and guided concrete compatibility corrections:

- Enable the advertised portability-subset features, including image-view swizzle
  and fragment interpolation. Merely enabling the extension left them disabled.
- Preserve storage-image descriptor types for absent/rejected texture resources.
  The old default descriptor classified these as sampled images, contradicting
  the pipeline layout. The null-resource GPU regression now uses this fallback.
- Remove speculative storage usage from multisampled images on devices without
  multisampled storage support. This preserves the guest sample count.
- Clamp sampler LOD bias to zero when the portability feature is absent. Shader
  emulation of the guest's nonzero sampler bias remains a fidelity limitation.
- Auxiliary rectangle/quad tessellation stages copy only fragment inputs actually
  read, avoiding reads from vertex locations which have no matching export.
- Include shader-binary, metadata and pipeline-key revisions in the cache profile,
  so incompatible generations are rebuilt together. A prior warm-up exposed
  incompatible vertex numeric types; cache-off gameplay still had corruption.

The revised renderer compiled as ARM64 and passed six renderer test groups:
portable SRT evaluation, buffer limits/aliases, barycentric interpolation,
null storage images, texture detiling and concurrent IRQ registration.
Additional graphics checks exercise 256 consecutive draws with split-stage
resource bindings and pushed buffer offsets. The typed-buffer regression covers
8/16/32-bit aliases, range limits, nonzero offsets and the null stand-in.
The first real gameplay run after the API corrections had zero reported Vulkan
core validation errors, but its screenshot still showed the original corruption.
This does not imply synchronization or GPU-assisted shader validation passed.

Metal GPU validation previously reported invalid device loads. Matching the
emitted code placed the first reported source line at a push-parameter read.
The small graphics reproduction passed both normal and Metal validation runs;
the game instrumentation also affected system-HUD shaders. These observations
are inconclusive and are not presented as the root cause.

A separate ARM64 KosmicKrisp/Mesa 26.2.4 ICD loaded on this Mac, but did not
advertise VK_KHR_fragment_shader_barycentric. The required-feature probe therefore
stopped before drawing. No gameplay comparison was performed and this driver is
not promoted as an immediate replacement. A separate synthetic PerVertexKHR
probe was also rejected by MoltenVK; this remains an unsupported feature, not a
proven explanation for the captured material defect.

The upstream stale-readback correction is already present. The captured corrupt
frame did not show the specific 0.25 linear-depth tile pattern documented in the
upstream multisampling report. Similar screenshots alone are not a causal match:
[stale readback](https://github.com/FelipePrado0/gow3_pc/issues/138),
[depth/MSAA fault](https://github.com/FelipePrado0/gow3_pc/issues/65).
For the independent driver path see the
[LunarG SDK documentation](https://vulkan.lunarg.com/doc/view/latest/mac/getting_started.html).

The remaining investigation should freeze and replay one demonstrably broken
material draw with its complete pipeline, descriptors, vertex/index buffers,
push data and inputs. That would separate incorrect resource/state selection
from shader evaluation without rebooting the whole game for every hypothesis.
GFXReconstruct was reviewed as an existing capture/replay option; a replay has not
yet been recorded or tested. The root cause remains unproven.

All raw game textures, memory snapshots, shaders and captures stay private outside
the source archive. Earlier overlapping runs 22/23 were invalidated. The local
harness now uses an exclusive instance lock, a surviving-child PID check and a
unique log per run; the second-launch refusal was exercised.

For private diagnostics only, set GOW3_DIAG_BOUND_IMAGES=1, GOW3_DIAG_DIR to a
private directory, GOW3_CAPTURE_DISPLAY_PASSES=8, and arm GOW3_CAPTURE_TRIGGER.
At most 128 compressed images are captured, with all mips for single-layer 2D
images, source/layout metadata and 16 x 16 RGBA view-readback grids per mip.
GOW3_DIAG_BOUND_BUFFERS enables at most 512 CPU-owned range checks (64 KiB each).
GOW3_DIAG_BUFFER_TARGET may select an effective color target from a private pass
capture. These diagnostics force GPU completion, change timing and write game
memory. Leave them unset during normal use and never package their output.

compile_game.py --opt-level 2 --output-directory PRIVATE_DIR builds a separate
comparison library. Compiler flags participate in its incremental cache key and
build-settings.json records them. This historical comparison had not established
a gain and retained O1. The later CPU comparison above selected O2 as the default.


The final warm-cache reopening loaded 216 cached pipelines, reached the same save
and Pause, and completed a bounded capture with zero Vulkan core validation
errors. It matched 1,510,296 bytes from 512 CPU-owned bindings of the effective
scene color pass, including 47 vertex ranges. The associated texture-view audit
again matched 126 views, 1,155 mip instances and 1,182,720 RGBA components within
2/255 maximum error. A game-only screenshot before capture still showed widespread
scene and blade corruption. The test ended intentionally with exit 0. These
results verify the repaired API contracts and sampled data boundaries, not a
successful visual port.


## MIN target allocation follow-up — 2026-10-17

Native Darwin launches now default to `GOW3_LAZY_COLOR=3` through the shared
`run.py`; `GOW3_LAZY_COLOR=0` is the explicit reference. Windows defaults are
unchanged. This restricts compatible single-level/single-layer RGBA8 2D backing
usage and view formats when a target is first used by a MIN blend. A transfer
preserves all samples/pixels. Actual storage or integer views promote the backing;
old descriptors remain alive and a target is not compacted repeatedly afterward.
All original texture readback/startup rules remain enabled. Shader generation 26,
formats, equations, precision, resolution and sampling remain unchanged.

On M3 Max, 180-second initial-AUTOSAVE combat runs at internal 3840x2160, engine
60, V-Sync off, interpolation/upscaling off measured reference 59.9917 FPS and
candidate 60.0000 FPS. Candidate five-second samples were all 60.0, with no shader
compilation and a worst reported frame of 16.7 ms. System GPU clock/power readings
averaged 1329.6 MHz / 23.18 W versus 984.2 MHz / 14.59 W. The 90-second reverse
reference returned to 1370.2 MHz / 23.98 W and 60 FPS. System raw GPU utilization
was 86.3% versus 90.5%; do not present the frequency-scaled activity proxy of
93.9% versus 71.0% as an exact capacity or spare-headroom percentage.

Frozen-scene counters with DrawPipe active measured the six projections near
2.15 versus 1.15 ms. Main-command-buffer intervals stayed near 14.2 ms; GPU DVFS
and the 60 FPS limit make these insufficient by themselves to quantify capacity.
Unlike earlier marker profiles, command markers were off; object names identified
pipelines without serializing recording. Counter probes and screenshots were
absent from FPS windows. Telemetry used macmon 0.10.0 IOReport/SMC system readings.
These are separate evolving fights, not deterministic frame replays or certification
of every frame/area in the campaign. Final game-owned captures were reviewed.

The synthetic MSAA/copy/view/storage tests preserved exact bytes with Vulkan and
Metal validation. Broad RGBA8 allocation, invariant motion and early-fragment-test
experiments were not promoted. A speculative draw-preparation vertex-format guard
also defers incomplete descriptors to the submitted-draw path, retaining strict
validation there. Whole-game Metal debug-layer lifetime diagnostics are tracked
separately from the normal-release performance tests.

Final no-probe Metal API validation also passed boot/menu/AUTOSAVE/Pause. An earlier
instrumented reference asserted on a screenshot-sized readback buffer; it was
excluded and not reproduced in that final validation. The intermittent diagnostic
cause is not claimed fixed, nor is whole-campaign API validation claimed.
