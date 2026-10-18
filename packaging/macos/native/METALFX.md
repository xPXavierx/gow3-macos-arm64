# Native MetalFX spatial upscaling

MetalFX spatial is an opt-in macOS renderer path. It exports the game's completed
Vulkan presentation view to Metal, runs Apple's MTLFXSpatialScaler at a fixed output
size, and returns the result to Vulkan for presentation. The game HUD is included
in the spatially upscaled image. This integration supports SDR output; HDR is
rejected explicitly. The CPU continues to use strict ARM64 AOT.

The original native-resolution launch remains the reference. A 2560x1440 input
upscaled to 3840x2160 must be described as upscaled 4K, not native 4K.

## Requirements and build

An Apple Silicon Mac with MetalFX spatial support and a macOS SDK containing
MetalFX is required (SDK/API macOS 13 or newer). The tested machine is an M3 Max
on macOS 26.5.2. CMake enables Objective-C++ on Apple and links the system MetalFX
and Foundation frameworks. There is no third-party MetalFX download.

Build the renderer normally; CMAKE_OBJCXX_COMPILER can explicitly select the same
Apple clang++ toolchain as CMAKE_CXX_COMPILER. The optional `metalfx-test` target
checks production Vulkan/Metal interop with synthetic pixels, rather than game
files. Enable GOW3_VK_VALIDATION and the validation layer path for that test.

## Launch

Prepare a separate strict-AOT profile at the intended **input** resolution.
Use `prepare_game.py` with `--patch 'Resolution Patch - 1440p'` and the same other
code patches as the native profile, then `compile_game.py --opt-level 2`.
A 1080p performance input can reuse the existing 1080p AOT profile. Resolution
patches must match the compiled input; changing output size does not patch CPU
code at run time.

Set the profile's native-display.ini to `render_resolution=1440p`,
`output_res=2560x1440`, `engine_fps=60`, `upscaler=none`, `live_resolution=0` for
1440p input. The legacy temporal upscaler remains off. Select that file with
GOW3_CONFIG and select its matching GOW3_NATIVE_AOT_DIR/GOW3_RECOMP_LIB/GOW3_USER_DIR
using the normal native launcher. Add:

```
export GOW3_METALFX_MODE=spatial
export GOW3_METALFX_OUTPUT=3840x2160
export MTL_HUD_ENABLED=1
```

These variables do not change the game's render-resolution patch. Resizing the
window only changes the presentation size; the input profile and MetalFX output
stay fixed. Unset GOW3_METALFX_MODE to return to the native reference. Unsupported
modes fail explicitly; there is no silent substitution with another upscaler.

Keep the existing native launcher's readback, texture, input and save policies.
In particular, GOW3_READBACK_LINEAR=1 remains required for texture correctness.
MSAA remains off for compatibility. MetalFX spatial does not add temporal AA.

## GPU synchronization and color

VK_EXT_metal_objects exports the actual Metal device and textures. Two imported
MTLSharedEvents, exposed as Vulkan timeline semaphores, order input rendering,
MetalFX encoding, and Vulkan consumption. The external result waits at all
pipeline stages; ordinary presentation wait scopes remain unchanged. No image
is copied through the CPU and no per-frame CPU completion wait is used.

The SDR output is RGBA8 UNORM with a matching sRGB sampling view where the game's
display is sRGB. Spatial processing uses perceptual color. The input is exported
with VkExportMetalTextureInfoEXT.imageView, preserving the presentation format
and component mapping. Only the sRGB decode is removed via a matching UNORM
Vulkan view. The backing image is not a valid substitute: the tested game uses
R8G8B8A8Srgb storage with a B8G8R8A8Srgb presentation view. Exporting storage alone
swapped red and blue in the first MetalFX implementation. Recreating a Metal
texture view after export could also discard MoltenVK's native swizzle, so the
exported perceptual view is passed directly to MetalFX. This is a view-only
change, with no additional per-frame normalization/copy pass. Presentation forces
opaque alpha, as the original path does.
The output image uses half the storage of RGBA16F (31.64 versus 63.28 MiB at 4K).
GOW3_METALFX_FLOAT_OUTPUT=1 selects the float diagnostic reference, with explicit
sRGB decoding at presentation. Synthetic tests compare both formats at every
pixel: maximum difference from quantized float was 2/255 for the tested patterns.
A replay of one captured active-combat frame differed by at most 2/255 in
99.9969% of components, with a 28/255 maximum outlier and 0.301/255 mean difference.
The saturated Pause frame had a 36/255 maximum outlier. These are output-format
comparisons, not a proof of equivalence to native 4K or a complete fidelity audit.
Those initial comparisons used the same incorrectly interpreted backing image
for both outputs and did not detect the red/blue regression. They must not be
used as evidence of correct game colors.

GOW3_METALFX_TIMING=1 uses separate in-order native command buffers for
input waiting, MetalFX work, and output signaling. GPU times from the work buffer
exclude the input-event wait. This changes submission behavior for profiling only. Keep it off during
clean FPS measurements. GOW3_FRAME_DUMP_TRIGGER and GOW3_DUMP_DIR can capture the
original display bytes, full-resolution MetalFX output, and window presentation after
a measurement. Dumps are private diagnostics and must stay outside repositories.
With MetalFX enabled, the trigger also renders a same-frame reference through
the original display view into `reference-presentation`. That extra diagnostic
pass is absent during ordinary gameplay and clean FPS measurements. Raw output
bytes must be interpreted using the logged presentation format (BGRA in the
tested game); storage byte order alone does not describe displayed colors.

## Temporal reconstruction and frame interpolation

The M3 Max supports `MTLFXTemporalScaler` and `MTLFXFrameInterpolator`.
Spatial upscaling remains the ordinary mode. A separate, opt-in native frame
interpolation path now captures God of War scene depth, projection, vertex
history and pre-composited HUD. It is compiled and tested, with performance and
fidelity limits recorded in [FRAME_INTERPOLATION.md](FRAME_INTERPOLATION.md).
The legacy Bloodborne temporal-camera hooks remain uncalibrated for God of War
and are not used as substitute temporal inputs. Real-frame TAA is not enabled.

References: [Apple spatial scaling](https://developer.apple.com/documentation/metalfx/mtlfxspatialscaler),
[Apple frame interpolation](https://developer.apple.com/documentation/metalfx/mtlfxframeinterpolator),
[Khronos Metal object interop](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_metal_objects.html).

## Color correction verification — 2026-10-17

The initial MetalFX integration incorrectly exported RGBA backing storage instead
of the game's BGRA display view, swapping red and blue. The corrected input is
the exported UNORM presentation view with the same channel interpretation and
opaque-alpha mapping as the original presenter. The expanded native interop test
passes 2,880 color samples with Vulkan core and Metal API validation and no
validation errors. It includes base/view format mismatches and native swizzles.

Clean case 157 reached the same saved combat on M3 Max: 2560x1440 to 3840x2160
spatial output averaged 59.825 FPS (59.6–60 five-second samples) for approximately
40 seconds; Pause averaged 60.0. Shader compile counts were zero. Case 156 before
the fix averaged 59.9 FPS in a 30-second combat window. Poses vary; no performance
gain or exact equivalence is inferred from that small difference. GPU utilization
and isolated scaler cost were not remeasured after the color correction.

Game-owned captures confirm the red studio logo, red/gold menu and weapons, red
clothing/tattoo, green HP and blue mana. A same-frame diagnostic through the
original presentation view agrees with the corrected color interpretation.
Absolute RGB differences from that reference averaged 0.566/255, with a 99th
percentile of 6/255 and maximum of 69/255. Resampling and sharpening differ at
edges, so these are not pixel-identical outputs or a native-4K quality guarantee.
Previously red regions averaged 1.536/255 difference with the fix, versus
66.915/255 when red/blue are deliberately reversed in the corrected output.
The initial format-only comparisons did not certify the display view's colors.

## Bounded game measurements — 2026-10-17 — initial version (before color correction)

All game CPU execution was strict ARM64 AOT; shader/pipeline compile counts in the
clean timing windows were zero. HUD was on, counters/captures were off in those
windows. Saves were isolated and only one game ran at a time.

| Input / output | Combat FPS mean | Five-second sample range | System GPU mean |
|---|---:|---:|---:|
| Native 3840x2160, current renderer | 38.33 | 37.2–39.6 | 98.88% |
| 2560x1440 / MetalFX 3840x2160, SDR | 59.91 | 59.4–60.0 | 92.38% |
| 1920x1080 / MetalFX 3840x2160, SDR | 59.75 | 58.0–60.0 | 77.63% |

Combat windows were approximately 40 seconds. The earlier float-output 1440p
trial averaged 59.50 FPS over 30 seconds. Poses/draw counts vary; the 0.41 FPS
change cannot be attributed solely to the output format. The prior renderer also
averaged 37.69 FPS in the new native-4K comparison (range 29.8–41.6), while its
previous 30-second run averaged 41.17 FPS. No consistent native-path regression
or native-4K optimization gain is established by these differing poses.

A separate profiling run measured MetalFX at 0.676 ms on average across its last
five 120-frame work-buffer batches in combat. Three tracked screen effects were
2.061/1.603/1.852 ms at native 4K in profile 131 and 0.951/0.733/0.846 ms with
1440p input in profile 155. This reduction comes from rendering fewer input
pixels; no claim of faster native-4K shader math is made. Profiling changes
submission/recording behavior and is not a clean FPS measurement.

Eight native test groups passed: MetalFX interop, gamma, bounded constants,
texture detiling, render areas, readback queue, graphics settings and performance
overlay. The expanded interop test covers 2,880 color samples, four backing
formats, independent RGBA/BGRA presentation aliases, explicit component swizzles,
resize,
zero input alpha, and float/SDR output comparisons at every synthetic pixel.
Vulkan core and Metal API validation reported no errors in that synthetic test.
The initial full-presentation validation prototype also exposed a VVL limitation:
native shared-event signals are not tracked as resolving Vulkan timeline signals
(VUID-03268). The prototype's plane-export errors were fixed. Asynchronous
full-game Vulkan validation is not certified clean by the synthetic test.

The native-4K/60 goal remains unmet. Upscaled output approaches 60, with occasional
missed frame deadlines. Native 4K still retains more fine detail. The inherited
foliage/material issues and full-campaign fidelity remain outside this bounded
check. Temporal reconstruction and frame interpolation stay unimplemented and
are not counted in any FPS result.
