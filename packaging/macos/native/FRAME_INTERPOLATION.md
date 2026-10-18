# Experimental MetalFX frame interpolation

This is an opt-in, native ARM64 implementation of Apple's public
`MTLFXFrameInterpolator` API. It requires macOS 26, a supported Apple GPU, and
MetalFX frame-interpolation headers from SDK 26 to build the effect. The M3 Max
used for validation supports it. Ordinary launchers leave it disabled.

The feature creates an actual intermediate image; it does not repeat the real
frame or advance the game's simulation. Rendered FPS and generated FPS are
reported separately. A higher presentation rate is not a measurement of input
latency, which has not been measured here. Sustained 120 FPS and a complete
campaign fidelity/stability qualification remain unverified.

## Build and launch

Use the existing native build and prepared, matching AOT resolution profile.
No CPU translator, interpreter or Rosetta fallback is enabled. The renderer
detects the active SDK and also checks the Command Line Tools SDK for the new
header. On a machine whose selected Xcode still uses SDK 15, the effect can be
compiled separately against an installed SDK 26:

```sh
cmake -S gpu -B out/gpu-arm64-release \
  -DGOW3_FRAMEGEN_SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk
cmake --build out/gpu-arm64-release --target gow3gpu \
  frame-interpolator-test motion-shader-test texture-detile-test motion-history-test \
  depth-stencil-blit-test -j6
```

Keep the native dependencies and configuration from the existing build
instructions. This command updates that build directory; it is not a complete
dependency bootstrap. The rest of the renderer retains its existing SDK and
deployment target. Dynamic Objective-C class lookup and OS/device checks gate
the new API at runtime.

Add these variables to the normal native launch environment:

```sh
export GOW3_FRAMEGEN=1
export GOW3_FRAMEGEN_INPUT_DIVISOR=2
export MTL_HUD_ENABLED=1
```

`GOW3_FRAMEGEN_INPUT_DIVISOR=2` halves depth/motion width and height, with
nearest-foreground depth selection and matching motion. It preserves full-size
current/previous color, HUD and interpolation output. Motion is scaled into the
depth/motion input's pixel coordinates. Omit it or set it to `1` for full-size
temporal inputs. Only `1` and `2` are implemented.

For optional spatial 4K output, also set `GOW3_METALFX_MODE=spatial` and
`GOW3_METALFX_OUTPUT=3840x2160`. Select the matching 1080p or 1440p AOT input
profile through the normal launcher; these variables do not change the game
resolution patch. A 4K MetalFX texture is distinct from physical scanout: the
tested internal panel presents a 3456x1944 game image with letterboxing.

Unset `GOW3_FRAMEGEN` to use the ordinary renderer. Keep the original launcher's
texture readback, patch verification, save backup and process supervision rules,
including `GOW3_READBACK_LINEAR=1`. Retina/fullscreen and automatic Game Mode
eligibility remain available. No private Game Mode API is used.

## Temporal inputs and image ownership

The GoW depth-copy pass provides the active projection and the scene's depth
addresses. Projection values are checked for finite ranges and aspect ratio;
near/far and vertical field of view are derived from the active coefficients.
The scene color and depth are captured after post processing and before the
game clears scene depth to draw its HUD. Unsupported or missing inputs use the
real frame only; skipped interpolation invalidates native history. Interpolation
waits for two warm-up encodes before presenting an intermediate result.

Motion comes from actual current and previous vertex clip positions. History is
matched using geometry, index topology, draw offsets, instances and occurrence;
bounds and capacity checks reject unsafe accesses. The extra attachment is
restricted to geometry with active scene depth, rather than full-screen effects
that merely retain an old depth register. Two explicit storage bindings in set 1
provide the parameter ring and position history. Duplicate indexed vertex
invocations keep atomic stores; only the consumed X, Y and W components are
stored. Draws using the reserved varying range are excluded from this path.
Indirect and unmatched geometry have incomplete motion coverage and remain a
quality limitation. No fabricated Bloodborne camera vectors are used.

The full current display is supplied as a pre-composited UI texture alongside
HUD-free scene color. MetalFX handles UI decomposition/recomposition. View export
preserves BGRA/RGBA channel interpretation; output is private BGRA8 with matching
UNORM/sRGB Vulkan views. Spatial AA and the optional spatial scaler process real
and generated output consistently. This does not enable TAA for real frames.

Imported Metal shared events explicitly order Vulkan production, native MetalFX
work and Vulkan consumption. Image pixels are not transferred through the CPU
in ordinary operation. Each presentation slot owns a separate generated image
and completion fence. Generated presents do not increment game flips or consume
game input. VSync/FIFO queues each generated image before its real image, with no additional
CPU sleep. Turning VSync off disables generation until VSync is restored. The
reported display rate counts distinct, nonzero presentation timestamps. Behavior
on variable-refresh displays remains experimental.

## General renderer corrections made during integration

Sparse color outputs must declare `VK_FORMAT_UNDEFINED` for unused slots. Adding
motion in slot 7 exposed stale guest color formats in the intervening holes;
Metal API validation identified the inconsistent pipeline/attachment declaration.

Motion originally used physical buffer addresses. MoltenVK conservatively marks
all BDA-capable buffers as readable/writable for such shaders. Metal validation
then identified an idle staging allocation being released while a later draw
still referenced that residency set. Staging allocations no longer advertise
unused device addresses; ordinary buffer usages retain their default behavior.
Motion itself now uses explicit descriptors and no embedded session addresses.
The latter also permits caching its shader binaries across sessions.

A separate, pre-existing color-to-depth helper also used a combined D32S8 target
with only its depth aspect bound. Warm-cache Metal validation reproduced this
at the first menu render. On macOS the helper now declares and binds both
physical aspects, with stencil tests/writes disabled and existing stencil
loaded/stored. A production-helper GPU regression verifies 4,096 depth values
and all 4,096 preserved stencil bytes with both validators and no errors.
Warm-cache game validation then reached the menu and combat without a Metal
assertion. This was a helper-pass issue, not a reason to discard the shader cache.

Varyings reserve locations 26/27, leaving room for built-ins within the M3 Max's
124-component limits. Shader/pipeline cache versions were bumped for the changed
interface and attachment declarations. These corrections preserve the original
texture readback policy.

## Verification and limits

The native interop test covers eight combinations: RGBA/BGRA output, separate/
pre-composited UI, and full/half depth-motion dimensions. A moving rectangle's
generated centroid is 43.5, between previous 39.5 and current 47.5; HUD color stays
fixed, and invalid projection is rejected. Metal and Vulkan validation reported
zero errors in these synthetic tests. The production shader backend's plain and
motion modules pass SPIR-V validation; actual Vulkan module creation with both
validators passes. The motion vertex example has no physical-address capability.
The production detiler test verifies 1,196,800 bytes across seven dispatches,
including BC1/BC3 image roundtrips. Motion-history bounds tests also pass.
`depth-stencil-blit-test` covers the combined-aspect regression described above.

Game tests use isolated saves and only game-owned captures. Full-game Vulkan
validation still reports the known imported-timeline presentation warning
`03268`; the layer cannot see native Metal's shared-event signal. Earlier runs
also found color-hole, staging-lifetime and varying-limit faults described above;
those failures are not presented as passing tests. Runtime checks and bounded
gameplay do not certify the full campaign, every camera cut, thin foliage,
particles, disocclusions, HDR, other GPUs, external displays or input latency.

`GOW3_FRAMEGEN_TIMING=1` isolates native effect work from the upstream event wait
for profiling and changes command-buffer submission. Keep it off for clean FPS
tests. `GOW3_FRAMEGEN_INPUTS_ONLY=1` captures temporal inputs without encoding or
presenting generated frames. Game-derived raw captures belong outside the source
tree and source archive.

References: [Apple frame interpolator](https://developer.apple.com/documentation/metalfx/mtlfxframeinterpolator),
[Apple descriptor](https://developer.apple.com/documentation/metalfx/mtlfxframeinterpolatordescriptor),
[MoltenVK BDA residency](https://skia.googlesource.com/external/github.com/KhronosGroup/MoltenVK/+/3f6a3c28f740a1474f34178df77f79de9d55d50b/MoltenVK/MoltenVK/GPUObjects/MVKDevice.mm).
The underlying motion-history and renderer work retains bbport/shadPS4 credits.

## Bounded game measurements — 2026-10-17

FXAA Strong, Retina fullscreen, isolated autosave; approximately 40 seconds of
active gameplay per run, with eight five-second real-FPS samples and zero shader
compiles. No screenshots or GPU counter collection ran in the timing windows.

| Case | Input / optional spatial output | Frame interpolation | Real FPS mean (range) | Actual presentation FPS mean |
|---|---|---|---|---|
| 179 | 1920x1080 / display size | Off (earlier reference) | 59.80 (58.4–60.0) | 59.78 |
| 187 | 1920x1080 / display size | On, depth/motion 960x540 | 56.58 (54.8–59.8) | 111.35 |
| 188 | 1920x1080 / MetalFX 3840x2160 | On, depth/motion 960x540 | 49.05 (47.2–54.0) | 94.94 |

These are distinct runs of a varying fight, not identical frame replays. The
presentation means use MoltenVK's `actualPresentTime` buckets; the first bucket
can partly overlap the scene transition. Generated frames are skipped during
missing/invalid inputs and history warm-up. These figures cannot be interpreted
as twice the simulation rate, native 4K rendering or reduced input latency.
A prior 1440p-to-4K candidate averaged 39.54 real / 79.09 presented FPS; it used
the earlier fixed presentation spacing and is not a final pacing measurement.
The reference predates the final helper-format correction; the generated-frame
runs use the final FIFO pacing. The 60-real-FPS condition was met in the tested
1080p reference without frame
generation; enabling interpolation has a measurable rendering cost.


## Reduced-cost path — 2026-10-18

Frame generation remains an opt-in developer experiment. The standard Swift
launcher keeps it off. The 0.4.1 desktop adapter does not expose a frame-generation argument or launcher
option and explicitly keeps generation disabled. The native renderer source retains
the experiment for future developer work. If generation is used through the native
research path, the display policy never requests 60 Hz. Unlimited rendering also
leaves refresh unchanged.

The research path can select half-width/half-height interpolation color/output and temporal
inputs. `GOW3_FRAMEGEN_COLOR_DIVISOR=2` affects generated frames only: native real
4K remains 3840x2160, while interpolation runs at 1920x1080. Without spatial
MetalFX the generated image uses the existing linear presentation scaler; it can
look softer than the real frame. This is not full-resolution 4K interpolation.
The HUD names real-render dimensions, spatial MetalFX, rendered/generated FPS and
interpolation dimensions separately. Its FPS rows are submission counters; actual
presentation measurements use distinct nonzero Metal drawable timestamps.

`GOW3_FRAMEGEN_COMPACT_MOTION=1` (default when generation is requested) stores RG16F
motion instead of RGBA16F. Invalid interpolated history is rejected before the
write, preserving the previous half-float validity threshold. The independent
production compute-shader test checks 16 combinations of compact/legacy motion,
full/half colors, full/half depth-motion and UI-only packing. Colors, nearest-depth
selection, corresponding vectors and output-buffer guards pass on M3 Max with
Metal and Vulkan validation and no errors. The shader cache version is 28.

Real and generated frames keep separate persistent spatial MetalFX scaler objects.
A single scaler was being reconfigured twice per game frame when the processing
sizes differed. Reusing two objects eliminates that initialization churn. Pixels
remain GPU-resident; the original texture coherence/readback rules stay enabled.

Diagnostic timing reduced the native interpolation encode from approximately
6.2-6.6 ms at 4K to 1.9-2.2 ms at 1080p. These are different processing resolutions,
not a free equal-quality speedup. Isolating the encode changes command submission;
do not use those profiling runs as clean game-FPS benchmarks.

Clean scene measurements and their limitations are recorded below. The 60 real /
120 presented target remains unmet. Increasing queued frames did not establish a
benefit and is not enabled in the adapter. Input latency, all scene transitions
and campaign-wide interpolation quality remain unqualified.


### Clean game windows on M3 Max

All rows use the same isolated Gaia autosave (Titan difficulty), V-Sync, Retina,
frame cap 60, AA off and ProMotion retained as the common display control. The
sequence alternates attack, block and dodge inputs; the scene/enemy counts vary.
These are bounded gameplay observations, not identical frame replays or a claim
of uninterrupted heavy combat. Captures and GPU profiling run outside FPS windows.
All listed FPS windows logged zero shader/pipeline compiles.

| Configuration | Real rendering | Interpolation | Window | Real FPS | Actual presents/s |
|---|---|---|---|---:|---:|
| Full-color legacy motion reference | 3840x2160 | 3840x2160 | 30 s | 47.12 | 93.99 |
| Compact motion / reduced interpolation | 3840x2160 | 1920x1080 | 30 s | 51.42 | 102.59 |
| MetalFX Balanced, full-color interpolation | 2272x1278 to 4K | 2272x1278 | 60 s | 51.44 | 102.97 |
| MetalFX Performance, reduced interpolation | 1920x1080 to 4K | 960x540 to 4K | 120 s | 52.40 | 104.73 |
| Native 4K, two queued frames (not retained) | 3840x2160 | 1920x1080 | 60 s | 51.82 | 105.89 |

The native-4K real-frame comparison improves by approximately 9.1%, with a lower
resolution generated image. The native framegen pair has median presentation
intervals near 8.33 ms but p95/p99 near 16.67 ms: it does not sustain a regular
120 Hz cadence. Neither Balanced nor Performance reached 60 real FPS. Do not
label the upscaled rows native 4K rendering. The first earlier reduced-input
60-second run overlapped a fixture build/GPU check and is excluded from this table.
The isolated-profile FPS values are likewise excluded. The ordinary renderer's
60 FPS reference does not make interpolation a zero-cost feature.

The original full-resolution input-preparation-only experiment (no interpolation
or generated presents) averaged about 54.8 real FPS over 60 seconds, compared with
60 in the ordinary renderer. This bounds a separate integration cost in producing
history/motion/depth. Reducing only MetalFX's encode cannot remove that cost.
Further work needs to measure and reduce that path and its queue/event waits;
merely reducing the scene resolution did not meet the real-frame requirement.


The final 0.4.1 community app also completed a cold automatic native-4K preparation
in 121.402 seconds using its bundled ARM64 tools. It then averaged 60.00 real /
60.00 actual presentations/s for 60 seconds with frame generation off and zero
shader compiles. This validates the packaged ordinary path after cleanup, not
60 real FPS with interpolation enabled. The final six renderer/motion fixtures
reported no Vulkan/Metal errors, and four production SPIR-V modules passed
spirv-val. Six native runtime/loader checks and 195 Python tests passed (59
Python cases skipped for unavailable compiled/platform prerequisites).
