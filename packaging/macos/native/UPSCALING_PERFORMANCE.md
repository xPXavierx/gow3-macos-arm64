# Upscaling and presentation, 0.4.13-local

The live launcher keeps the existing 4K logical AOT profile, rasterizes scene
targets at the selected physical input size, then spatially upscales once with
MetalFX. The final image includes the game's HUD. Native mode uses the same
presentation and sharpening policy. All CPU code remains ARM64 AOT; frame
generation is independent and was disabled in these measurements.

## Redundant work found

RCAS is enabled by default. `FsrPass::Render` previously coupled it to EASU,
including when FSR 1 was disabled and input/output dimensions were identical.
Consequently MetalFX output went through EASU at 1:1, an RGBA16F intermediate,
RCAS into another RGBA16F image, and the presentation/gamma fragment pass.
Native output also paid the identity EASU cost. This was a presentation cost,
not additional game drawing or evidence of a slow CPU instruction translator.

The renderer now runs EASU only for an actual size change. On macOS, SDR RCAS
at identical input/output sizes is fused with the presentation fragment pass.
It uses the same AMD half-precision RCAS implementation and strength. This
removes two dependent compute dispatches and both intermediate image writes
from the ordinary path, retaining sharpening, alpha, gamma and cutscene bars.
HDR, ten-bit input and other sizes retain the separate path. Interpolation
keeps its own presentation settings; a real frame's fused settings do not leak
into a generated frame. Game rendering and required draws are unchanged.

The change does **not** make upscaling free. MetalFX still writes the output
pixels and crosses the existing Vulkan/Metal shared-event boundary. At a CPU
or frame-rate ceiling, reducing input pixels cannot increase FPS indefinitely.
Compare against native rendering at the input size as well as the output size.

## Verification

`rcas-direct-test` runs the production Vulkan filters on synthetic pixels. It
checks RGBA/BGRA sRGB presentation aliases, zero input alpha, color blocks, a
width not divisible by the dispatch size, and opaque presentation. Fused RCAS
must differ by at most 1/255 from direct compute RCAS on every tested byte.
Against the old identity-EASU + RCAS path, mean absolute error was 0.021823/255
over 153,600 bytes; the largest edge/border difference was 25/255. Removing EASU
is not a promise of bit-identical reconstruction at edges.

The same-process game comparison switches legacy/fused presentation while
keeping native CPU code, save, camera, preset and sharpening strength fixed.
Combat windows are bounded and scene dependent. Pause is verified by OCR after
each static interval. No GPU stage profiler or compilation runs in the clean
FPS windows. Separate stage profiling measured the spatial scaler itself at
about 0.67 ms for 1440p to 4K on this M3 Max. Its isolated work-buffer timing
excludes the input event wait and is not the complete end-to-end overhead.

Maintainer diagnostics (not launcher preferences):

- `GOW3_FSR_LEGACY_RCAS_RESAMPLE=1` retains EASU + RCAS for comparison.
- `GOW3_FSR_RCAS_CONTROL=/absolute/file` selects `0` fused presentation, `1`
  legacy EASU + RCAS, or `2` direct compute RCAS. With no variable, no control
  file is accessed. Use a private test directory.
- `GOW3_METALFX_TIMING=1` separates native wait/work/signal submissions for
  stage timing only. Keep it off for clean FPS comparisons.

## Bounded M3 Max measurements, 2026-10-25

CUSA01623 v1.02, external power, warm cache, no user controls during the run,
real game-flip timestamps, no frame generation or CPU fallback. The real-frame
ceiling was 120 FPS, with the existing 240 Hz engine/vblank profile. Native
below means actual native scene rendering; MetalFX outputs are upscaled 4K.

A 60-second combat test alternated legacy/fused/fused/legacy/fused/legacy in
10-second windows. The first 250 ms after each switch was excluded:

| 1440p to 4K, MetalFX Quality | Three window means | Mean |
| --- | --- | ---: |
| Legacy presentation | 78.9, 86.8, 83.9 | 83.2 FPS |
| Fused presentation | 93.1, 92.3, 91.0 | 92.1 FPS |

This is **+10.8%** for that bounded combat. The earlier 18-second version had
only +3.3%, so the gain must not be described as universal across scenes.
All three longer fused windows exceeded all three legacy windows; zero draws
were omitted and CPU execution remained strict ARM64 AOT.

The same frozen frame was also measured for eight seconds per setting:

| Setting | Legacy | Fused |
| --- | ---: | ---: |
| MetalFX Quality, 1440p to 4K | 86.3 / 86.1 | 94.8 / 94.9 |
| MetalFX Performance, 1080p to 4K | 101.8 | 116.3 |
| Native 4K | 71.2 | 77.1 |

Fused Balanced (2272x1278 to 4K) reached 105.5 FPS in this frozen frame. Native
1080p and 1440p reached 109.7 and 103.9 FPS. The 1080p measurement had longer
p99 intervals near the 120 FPS ceiling; it cannot establish that upscaling is
faster than native rendering of its input size. Quality repeated at the end
reached 90.6 FPS, a reminder that absolute results can drift during a run.
Use the repeated interleaved comparisons for attribution, not cross-run poses.

A world/Kratos mask covering 1,816,620 pixels had mean RGB absolute difference
0.43–0.45/255 versus legacy, with component p99 3/255. About 0.05–0.06% of pixels
had any channel differ by more than 8/255. Captures were visually reviewed;
this is not a full-campaign fidelity guarantee or bit-identical reconstruction.

The installed 0.4.13-local app then completed 30 seconds of capped combat with
MetalFX Quality / 4K output at 60.000 FPS: p99 17.219 ms, maximum 20.830 ms,
zero intervals over 33 ms, zero omitted draws and zero CPU fallback. Both app
signatures passed deep/strict verification. The community audit checked 108
thin ARM64 Mach-Os and 21,019 files; the AOT preparation recipe and game-derived
CPU library did not change. Personal saves/settings/caches were not touched.

## Can FSR 4.1.1 replace MetalFX here?

AMD's current official FSR Upscaling page lists 4.1.1 and DirectX 12 support;
it does not supply an Apple Silicon/Metal implementation. It is not a Windows
DLL that can be added to this ARM64 application.
[AMD's requirements](https://gpuopen.com/amd-fsr-upscaling/).

A separate community project implements FSR 4.1.1 in native Metal and exposes
a C frame encoder accepting the caller's Metal command buffer and textures.
That makes a native GOW3 adapter technically possible without using its
CrossOver hook or Rosetta. Its M3 path uses SIMD half-matrix operations; the
author's hardware validation/performance measurements are on M5, not M3.
[Hardware](https://github.com/UzenUPoZiTiV4ik/fsr4-native-macos/blob/main/docs/HARDWARE.md),
[encoder API](https://github.com/UzenUPoZiTiV4ik/fsr4-native-macos/blob/main/src/fsr4_live.h).

It is a temporal reconstruction system, requiring calibrated color, depth,
motion-vector units/sign, camera jitter, exposure and history resets. The
existing GOW3 frame-interpolation inputs are not automatically proof that all
of those temporal-upscaling contracts are correct. Validate them before using
either temporal MetalFX or FSR, including object motion, disocclusion, particles,
camera cuts and the HUD. The project's input contract documents these fields.
[Input contract](https://github.com/UzenUPoZiTiV4ik/fsr4-native-macos/blob/main/docs/FSR411_INPUT_CONTRACT.md).

FSR 4.1.1 is **assessed, not integrated or benchmarked in GOW3** in this version.
No faster-M3 claim or native-4K-quality claim is supported yet. A useful next
experiment is an isolated M3 model benchmark followed by a calibrated GOW3
temporal adapter, sharing validated inputs with future frame generation.
