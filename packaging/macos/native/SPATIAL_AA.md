# Spatial antialiasing on native ARM64

The presenter can apply SMAA 1x High, FXAA Quality, or FXAA Strong to the completed
SDR game image before MetalFX spatial upscaling. No temporal history, velocity
buffer, camera jitter, MSAA samples or generated frames are added. The CPU remains
strict ARM64 AOT. The original AA-off path remains available.

## Retina presentation

Native macOS presentation now requests Retina density. Older run 161's 4K
MetalFX texture was fitted into a low-density drawable. Corrected Retina run 163
with FXAA Strong, fullscreen and automatic Game Mode averaged 58.925 FPS
(57.0–60.0) during approximately 40 seconds of active gameplay. Its game image
uses 3456x1944 physical pixels on the tested panel. No compiles or captures were
inside the timing window. These are bounded game runs with varying poses, not
controlled estimates of any individual feature's FPS effect.
See [Retina and Game Mode](RETINA_GAME_MODE.md) for measured sizes and OS evidence.

## Selection

The in-game port menu (Insert, or L2 + R3 on a controller), Image tab, exposes
Spatial anti-aliasing. Changes apply to the next frame, without rebuilding the
game. It also reports whether the selected filter was actually applied. The
setting is saved as `spatial_aa` in the selected configuration. The environment
override is read only at startup:

```ini
spatial_aa=smaa
```

Valid keys are `off`, `smaa`, `fxaa`, and `fxaa_strong`. The corresponding variable
is `GOW3_SPATIAL_AA`. None is enabled implicitly in native reference launches.
This pass supports RGBA8/BGRA8 sRGB display views. It is skipped for HDR, 10-bit
display formats and the inherited temporal display override, with applied status
reported as false. This is not a universal HDR antialiasing implementation.

## Image processing

SMAA uses the authors' High preset and all three spatial passes: color edge
detection (including chroma-only edges), area/search-table blend weights, then
neighborhood blending. Detection uses perceptual UNORM display values; blending
uses the matching sRGB view for linear-light samples. Only blended edge pixels
are re-encoded; unaffected pixels copy their encoded RGB directly. Every edge
and weight texel is written on every frame, including zero values. No discarded
invocation leaves stale edges behind. This is SMAA 1x, not SMAA T2x or S2x.

FXAA uses NVIDIA's PC Quality algorithm, search preset 39. Quality parameters
(subpixel / relative edge / minimum edge) are 0.75 / 0.125 / 0.0312; Strong uses
1.0 / 0.063 / 0.016. Full RGB luminance is evaluated inline, rather than using
green as a proxy or treating the game's alpha as luminance. Red/blue game and
HUD edges therefore participate without an extra luminance prepass. Strong also
softens more texture and HUD detail. It is not guaranteed to look better.

The original display's format and component mapping are retained before either
filter. Filter output is canonical RGBA8 UNORM, with a separate sRGB sampling
view for ordinary Vulkan presentation. MetalFX receives the exported perceptual
view directly, with the existing native GPU event ordering. No image is copied
through the CPU during gameplay. The filter runs at the internal input size;
1440p to 4K does not run the AA filters on all 4K output pixels.

Temporary storage is three RGBA8 input-sized images plus the small SMAA lookup
tables (approximately 42.2 MiB plus tables at 2560x1440). Allocation/resizing
drains the owning scheduler, while mode changes reuse these images. Source,
scratch and presentation layouts have explicit read/write barriers. The native
MetalFX output is transitioned to the Vulkan shader-read layout after native
processing and any requested raw dump.

The diagnostic frame-dump trigger additionally writes `spatial-aa` as canonical
RGBA8 at input resolution. Game data and raw dumps must remain in private local
test directories and are never included in the source archive.

## Build and regression tests

Build `gow3gpu`, `spatial-aa-test`, `graphics-settings-test`, and `metalfx-test` in
the existing native GPU build directory. The AA shaders are GLSL compute kernels
compiled through the renderer's usual Vulkan/MoltenVK toolchain. There is no new
binary dependency or separate GPU runtime.

The production-filter test checks RGB/BGRA presentation aliases, pure colors,
diagonal edge filtering, odd-size resize, zero input alpha, switching modes,
direct-Vulkan layout transitions, and a following black frame with no residual
pixels. It performs 187,512 assertions/checks in its normal run. The settings
test covers parse, persistence and environment override. MetalFX's 2,880 color
samples protect against the previous RGBA/BGRA regression. Optional Vulkan core
and Metal API validation are enabled with the existing environment options.

The test also accepts a private, synthetic or captured canonical RGBA8 image:

```
spatial-aa-test image-rgba8.raw 2560 1440 filtered.raw
```

This writes `m1-filtered.raw`, `m2-filtered.raw`, and `m3-filtered.raw` beside the
input and reports GPU timestamps over 40 repetitions after warmup. Such isolated
timings exclude the game and MetalFX; they must not be described as game FPS or
as an in-game profile. Keep validation and GPU debug layers off for timing runs.

## Bounded measurements — 2026-10-17

On Apple M3 Max, the same captured 2560x1440 combat image cost 0.680 ms
for SMAA High, 0.224 ms for FXAA Quality, and 0.298 ms for FXAA Strong.
These are isolated AA GPU timestamps over 40 repetitions after warmup, with
validation off. The normal game does not read back these images or timestamps.
Same-frame comparison images apply each filter and then the same MetalFX spatial
upscale to 3840x2160, with correct RGBA input interpretation. Enlarged crops use
nearest-neighbor scaling only to expose the output pixels. They cannot certify
temporal shimmer or full-campaign quality.

Historical low-density game run 161, FXAA Strong + MetalFX 1440p to 4K, averaged 59.425 FPS
in approximately 40 seconds of active combat (five-second samples 58.0–60.0),
with zero shader compiles and no captures/counters inside the timing window.
Pause averaged 59.600 FPS. The final build combines the native-Metal
completion dependency and Vulkan shader-read layout into one ordinary barrier;
only an explicit raw-dump trigger needs the extra transfer transition.

The earlier exploratory runs averaged 51.988 FPS with SMAA, 57.575 with FXAA
Strong, and 55.525 with AA off. Their average draw counts were approximately
2,452 / 1,776 / 2,128 per frame, respectively. The differing visible actors and
fight phases make those FPS averages unsuitable for isolating filter overhead.
No FPS improvement is attributed solely to the barrier consolidation. The
same-frame isolated timings above are the controlled filter-cost comparison;
sustained 60 FPS over the campaign remains unverified.

The local test launchers provide separate SMAA and FXAA Strong configurations,
with RCAS disabled so optional sharpening does not counteract the smoothing when
the window reaches the output resolution. The port's Image menu switches AA modes
live. Full game Vulkan/Metal synchronization validation retains the interop
limitations documented in METALFX.md; only the synthetic regression tests are
certified as validation-clean here.

## Limits

Spatial AA reduces visible stair-step contours. It cannot recover information
never sampled by the game, guarantee removal of subpixel foliage/specular
shimmer in motion, repair incomplete geometry, or prove full-campaign fidelity.
The game HUD is already in the final display image and is also filtered. Strong
FXAA's greater softness is a tradeoff, not elimination of aliasing at no cost.

Sources: [SMAA authors and paper](https://iryoku.com/smaa/),
[SMAA source](https://github.com/iryoku/smaa),
[NVIDIA FXAA white paper](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf),
[FXAA 3.11 source in Intel CMAA2](https://github.com/GameTechDev/CMAA2/blob/071c6b0857559f4e36f614362e6d2aab1b61938a/Projects/CMAA2/FXAA/Fxaa3_11.h).
Pinned revisions, local wrapper changes and license notices are recorded in
`docs/CREDITS.md` and the vendored headers. Existing bbport and shadPS4 credits
are retained.
