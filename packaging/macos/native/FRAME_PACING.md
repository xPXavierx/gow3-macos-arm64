# Native 4K presentation timing

The game flip counter measures work queued by VideoOut, before display. A stable
60 flips/s is not sufficient evidence of smooth display. The 2026-10-18 native
4K reference queued 60.0 FPS for five minutes, while direct Metal callbacks
confirmed about 57.64 distinct presentations/s, with a 32.91 ms p95 interval.

FIFO alone removed unconfirmed presentations in the tested combat, but its p95
remained 29.17 ms. An opt-in MoltenVK extension calls the public Metal
`presentAfterMinimumDuration:` method for FIFO presentation. With a duration of
1/60 second, the compiled driver completed a five-minute combat run at about
60.00 confirmed presentations/s, p95/p99 20.83 ms, worst 20.84 ms, no zero Metal
timestamps, and no intervals above 25 ms. Internal rendering remained 3840x2160,
with no upscaling, generated frames, shader-math changes or runtime CPU translation.

This reduces stutter; it does **not** establish perfectly uniform 16.67 ms
presentation or campaign-wide performance. The tested panel presents the 4K game
image at 3456x1944 physical pixels. The interval still alternated between roughly
12.5 and 20.8 ms. A view-bound 60 Hz display-link request and explicit timed
presentation did not establish an additional consistent gain and are not enabled.

The acquire-to-display interval increased with FIFO minimum-duration pacing;
it is a pipeline-stage measurement, not controller-to-display input latency.
Use the recorded raw timestamps to check both cadence and queue depth. Do not
infer input-latency improvements from the higher confirmed presentation rate.

## Bound the presentation queue

The final native 4K profile also selects `GOW3_SWAPCHAIN_IMAGES=2` when the Vulkan
surface supports it. Five minutes of combat kept about 60 confirmed presentations/s,
with p95/p99 near 20.83 ms and one approximately 25 ms interval among 18,014
confirmed presentations. Acquire-to-display averaged about 26.75 ms, compared
with 43.38 ms for three buffers with the same RGBA8 allocation strategy. This is
not an end-to-end input measurement. In matched initial 80-second windows, two buffers used about 15.98 W, versus
15.33 W for three; whole-run averages varied with the evolving combat. the native shortcut favors the shorter queue. An explicit value of
3 retains the original swapchain allocation for comparison. Frame-generation
shortcuts do not select the two-buffer option.

The extended RGBA8 allocation default was separately compared with the prior
MIN-only mode under the same three-buffer pacing. In matched initial 80-second
combat windows, GPU power was 15.33 W versus 17.45 W and 16.67 W in two controls.
GPU frequency was 1055 MHz versus 1171/1368 MHz. These are system measurements
of evolving combat, not deterministic frame replays or exact spare-capacity
percentages. Shader mathematics and texture readback rules are unchanged.

## Build the optional driver

Use an ARM64 Python, Apple toolchain, CMake and Ninja. No game data is needed:

```sh
python3 packaging/macos/native/build_moltenvk.py \
  --source-directory /absolute/private/MoltenVK \
  --output-directory /absolute/private/native-driver --frame-pacing
```

The script pins MoltenVK v1.4.2, applies the included patches, builds ARM64-only
and writes an ICD and hash manifest. It installs nothing system-wide. For the
prepared 60 FPS native profile, set `vsync=1`, `fps_limit=60`, `engine_fps=60`,
and launch the shared native launcher with:

```sh
VK_DRIVER_FILES=/absolute/private/native-driver/MoltenVK_icd.json \
GOW3_METAL_PRESENT_INTERVAL=0.016666666666666667 \
GOW3_SWAPCHAIN_IMAGES=2 python3 run.py
```

The setting is opt-in. Immediate presentation, an explicit Vulkan presentation
deadline and frame generation keep their existing behavior. Changing the interval
requires restarting the process. Retain the normal texture-readback settings.
The extension follows Apple's [variable refresh presentation guidance](https://developer.apple.com/videos/play/wwdc2021/10147/).

## Measure actual presentation

Build the optional observer. It changes no presentation method and reads no game
memory, pixels or shaders:

```sh
clang++ -arch arm64 -dynamiclib -O2 -std=c++20 -fblocks \
  -framework Foundation -framework Metal -framework QuartzCore \
  packaging/macos/native/metal_presentation_probe.mm \
  -o /absolute/private/presentation-probe.dylib
```

Add these variables to a fresh native game launch:

```sh
DYLD_INSERT_LIBRARIES=/absolute/private/presentation-probe.dylib \
GOW3_DRAWABLE_ACTIVE=/absolute/private/timing-active \
GOW3_DRAWABLE_OUTPUT=/absolute/private/presentation.jsonl python3 run.py
```

After loading and warming the combat, create `timing-active`. Remove it to stop
collecting; allow two seconds for the final callbacks and buffered output. Keep
screenshots, GPU counters and validation outside performance windows. The tested
observer also works with the Metal HUD enabled. Ordinary launches do not load it.

Sort nonzero `actual` values, deduplicate them, then calculate consecutive
intervals and p50/p95/p99/worst. Report zero timestamps separately. Never substitute
the callback's CPU time for an unavailable display timestamp. MoltenVK 1.4.2 does
that substitution internally for `VK_GOOGLE_display_timing`; a discarded timed
presentation experiment produced apparently perfect driver timing while direct
Metal timestamps were all zero. Historical extension-only FPS reports do not have
this independent confirmation. The direct observer is the reference for this audit.
