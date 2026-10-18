# Bounded native Metal profiling

`metal_pass_probe.mm` is an optional, process-local diagnostic, built from this
port's source. It uses public Metal stage-boundary timestamp counters and the
Objective-C runtime. It never captures the desktop or changes the game's assets.
It is not loaded by an ordinary launch. GPU-counter instrumentation changes timing;
measure FPS in a separate run without this library or periodic screenshots.

Build with the ARM64 Apple toolchain:

```sh
clang++ -arch arm64 -dynamiclib -O2 -std=c++20 -fobjc-arc \
  -framework Foundation -framework Metal \
  packaging/macos/native/metal_pass_probe.mm -o /absolute/private/metal-pass-probe.dylib
```

Use the same prepared native image, renderer and launch environment documented in
README.md. Run `run.py`, adding these variables only to the diagnostic process:

```sh
MTL_HUD_ENABLED=0 GOW3_VK_MARKERS=0 \
DYLD_INSERT_LIBRARIES=/absolute/private/metal-pass-probe.dylib \
GOW3_METAL_PASS_TRIGGER=/absolute/private/metal-pass-trigger \
GOW3_METAL_PASS_OUTPUT=/absolute/private/metal-passes.jsonl python3 run.py
```

Load the isolated save, pause in the chosen scene and warm up. In a separate
terminal, create the trigger file with `touch /absolute/private/metal-pass-trigger`.
The probe samples render, compute and blit encoders for up to 1.5 seconds or 4,096
encoders, whichever limit is reached first. One shared timestamp
buffer per command buffer avoids Apple's small concurrent-buffer limit. It waits
for command-buffer completion before resolving each encoder's samples. The
output's `kind` distinguishes render, compute and blit records. It contains
stage/encoder intervals, command-buffer GPU duration, target
sizes and sample counts, and pipeline object names. Keep command markers disabled: `GOW3_VK_MARKERS=1`
disables DrawPipe and serializes draw recording, changing the workload. Object
names still identify the pipelines with command markers off. Unsupported
stage counters return without installing hooks. The tested device is an M3 Max.
The HUD must be off here because its Metal wrappers interfere with these hooks.

Group records by command buffer and complete-frame pass sequence. Compare the
same shader hashes/targets across builds, because removing empty passes changes
ordinal indices. Vertex and fragment work overlap; do not add their durations and
call that the frame time. Encoder intervals may include dependency waits and are
not active ALU utilization. Zero timestamps from empty encoders are omitted;
vertex and fragment validity are independent, so a fragment-only pass is retained.
Vulkan timestamps in the tested MoltenVK build were not usable per pass.

Disable periodic screenshots before sampling. Use the game-only trigger helper
above for navigation, stop its requests after Pause is verified, and request the
counter window after warm-up. Keep screenshots outside the sampling interval.

## Native performance follow-up — 2026-10-17

Frame interpolation is off throughout this investigation. Two candidates were
not retained: a single-polynomial normalized sRGB conversion and fixed-LOD sampler
lowering. Both passed their numerical/GPU tests, but neither established a
reproducible combat FPS gain. The restored renderer's `__text`, `__const` and
`__cstring` sections match the reference exactly. Its color test now covers
1,048,576 inputs rather than 65,536.

Separate 40-second combat runs of the reference in native 3840x2160 measured
40.775 and 37.2125 real FPS. Shader compilation counts were zero during timing.
Even the paused scenes differed: 1,962 versus 2,001 draw commands per frame.
Candidate pause scenes had 1,985 and about 1,804 draws. These are not exact frame
replays; differences in those averages must not be presented as causal speedups.

The expanded Metal diagnostic was tested through menu/load/combat with Metal API
validation, and with a standalone synthetic render/compute/blit workload. The
final callback preserved 32 integer results and 1,024 RGBA components and reported
ordered, nonzero timestamps for all three encoder kinds. It is not loaded by
ordinary launchers.

The game diagnostic reached the 4,096-encoder limit after about 0.89 seconds.
Thirty-seven complete main command buffers shared the sequence counts of 73
render, 13 compute and 13 blit encoders. Mean intervals per main buffer were:

| Measurement | Milliseconds |
|---|---:|
| Fragment-stage interval sum | 18.450 |
| Compute-encoder interval sum | 0.371 |
| Blit-encoder interval sum | 0.088 |
| Main command-buffer GPU interval | 22.317 |

Fragment and vertex work overlap. Counter instrumentation and API validation
affect timing; these numbers are a diagnostic budget, not a clean FPS result.
The largest identified fragment groups were materials (3.232 ms), six depth
comparison/projection passes together (2.245 ms), a 32-tap postprocess (2.056 ms),
a composition using a 3D texture (1.845 ms), and final composition (1.641 ms).
No campaign-wide performance or new FPS gain is certified by this sample.

For FPS, start a fresh game process with the ordinary Metal HUD enabled and without
`DYLD_INSERT_LIBRARIES`, shader dumping, GPU counters, validation layers or periodic
captures. Use native 1920x1080 output/render, no upscaling, the same AUTOSAVE and
20 seconds warm-up followed by 60 seconds timing. `GOW3_FRAME_STATS=1` reports game
presents; retain all samples and shader compilation counts. Verify Pause before
and after; reject runs that change scene. A combat run needs its own description
of inputs, scene changes and duration, rather than being labelled a Pause test.

`game_screenshot_trigger.m` is another opt-in diagnostic that requests only the
renderer-owned game image through its existing exported screenshot function.
Compile it with `clang -arch arm64 -dynamiclib -O2 -fobjc-arc -framework Foundation`.
Set `GOW3_SCREENSHOT_TRIGGER` and inject that library only into the game supervisor.
Creating the named file requests one image. Request images before/after timing;
do not request them in a clean FPS window. No GPU counter or desktop capture is
involved. The symbol and enum are tied to this renderer version.

Synthetic regression targets `gamma-test` and `render-area-test` are excluded from
the default build. Build them explicitly from the GPU CMake directory and execute
with the same `VK_DRIVER_FILES` manifest as the game. The first compares production
sRGB IR against a double-precision CPU reference, including threshold/special/HDR
values and unchanged alpha. The second checks conservative draw-elision guards and
executes a vertex buffer-writing shader both with the old maximum render area and
the discarded 1x1 area, requiring exact writes and unchanged guard data.

References: [Apple stage-boundary counter documentation](https://developer.apple.com/documentation/metal/mtlcountersamplingpoint/atstageboundary),
[Apple tile-based rendering](https://developer.apple.com/documentation/metal/tailor-your-apps-for-apple-gpus-and-tile-based-deferred-rendering),
and [MoltenVK 1.4.2 command-buffer implementation](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/Commands/MVKCommandBuffer.mm).

## Command-buffer intervals without encoder counters

`metal_busy_probe.mm` records a bounded window of the game's Metal command-buffer
start/end times. It does not add counter sample buffers or modify render pass
descriptors. It accepts only the `gow3-probe-arm64` executable, so Python/OCR child
processes that inherit the environment remain untouched.

```sh
clang++ -arch arm64 -dynamiclib -O2 -std=c++20 -fobjc-arc \
  -framework Foundation -framework Metal \
  packaging/macos/native/metal_busy_probe.mm -o /absolute/private/metal-busy-probe.dylib
```

For a dedicated diagnostic run, set `DYLD_INSERT_LIBRARIES` to that library and set
`GOW3_METAL_BUSY_TRIGGER` and `GOW3_METAL_BUSY_OUTPUT` to absolute private paths
outside the source repository. `GOW3_METAL_BUSY_SECONDS` selects 1–30 seconds
(default 10); at most 10,000 submitted buffers are sampled. Warm the scene first,
then create the trigger file. The library removes it, records one window, and
writes JSON after the window and its completion handlers finish. The Metal HUD
may remain enabled. Do not mix this run with the encoder-counter probe.

Run `python3 packaging/macos/native/summarize_gpu_busy.py /absolute/private/result-dir`
where `busy.json` is the output file. The summary merges overlapping intervals
across queues instead of adding them. Review the covered duration and invalid
record count before interpreting the result. A separate clean FPS run remains
necessary.

Command-buffer intervals include in-buffer waits and possible preemption. Their
union is not active execution-unit utilization or a fraction of peak throughput.
At 60 Hz, multiply its fraction by 16.667 ms to estimate the occupied interval
budget per frame. IORegistry's GPU percentage includes every process on the Mac.
Neither measurement alone guarantees higher-resolution performance.

## Testing the bounded normalized sRGB conversion

Build `gamma-test` explicitly. Store its optional generated SPIR-V outside the
repository, as with every other shader diagnostic. The ordinary invocation checks
the full-range conversion, including HDR/negative/special values; `--unorm` checks
the bounded production path and its interval boundary with a stricter 5e-7 limit:

```sh
cmake --build /absolute/build/gpu --target gamma-test
VK_DRIVER_FILES=/absolute/MoltenVK_icd.json /absolute/build/gpu/gamma-test /absolute/private/full-range.spv
VK_DRIVER_FILES=/absolute/MoltenVK_icd.json /absolute/build/gpu/gamma-test /absolute/private/unorm.spv --unorm
```

Both invocations also construct shader-specialization keys from resource
descriptors and rebind UNORM, HDR, BC6 and custom-border resources, requiring the
appropriate permutations to remain distinct. Validate the emitted modules with
`spirv-val --target-env vulkan1.3`. This GPU test compares a mathematical reference;
it does not substitute for gameplay, warm-cache or visual validation.

The approximation applies after texture filtering, only to known normalized input
formats. It does not move conversion into hardware sRGB sampling or enable blanket
fast math. Explicit FMAs follow the precision/performance control discussed in
[Apple's Advanced Metal Shader Optimization](https://developer.apple.com/videos/play/wwdc2016/606/).
A 4K benchmark must use both 3840x2160 AOT patches and the matching output/render
configuration. Verify attachment dimensions in the diagnostic profile; the
presentation window and its screenshots can be smaller than the internal targets.

## Static constant-buffer aliases

Build `constant-alias-test` explicitly alongside the existing GPU regression
targets. Run it with VK_DRIVER_FILES pointing at the same native MoltenVK ICD;
an optional argument saves only its synthetic native/shared SPIR-V variants to a
private directory for spirv-val --target-env vulkan1.3. GOW3_VK_VALIDATION=1 and
VK_LAYER_PATH enable optional core validation in this and the null/detile tests.
The production optimization is limited to bounded static read-only constants.

Compare shader hashes, targets, draw counts and complete pass sequences. A faster
material fragment interval does not establish whole-frame FPS improvement.
Do not promote indexed constants or change render-target allocation based on
their theoretical cache/bandwidth advantages; those experiments regressed or had
no demonstrated benefit here. No SPIRV-Tools optimization library is required.

The default Metal compiler policy is retained. For diagnostic comparisons of
MVK_CONFIG_FAST_MATH_ENABLED, disable the project pipeline cache and use separate
processes; do not assume a cached Metal library was compiled with a new policy.
Synthetic precision tests are necessary but do not certify full-game visuals.
See [MoltenVK 1.4.2 math policy](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/Docs/MoltenVK_Configuration_Parameters.md#mvk_config_fast_math_enabled).


## Precise scalar arithmetic on MoltenVK

The native vertex and fragment shader paths can express independently rounded scalar add,
subtract and multiply as explicit `fma(1,a,b)`, `fma(-1,b,a)` and `fma(a,b,0)`.
These match the precise scalar helper bodies emitted by SPIRV-Cross for MSL
`NoContraction`, while avoiding its `[[clang::optnone]]` function boundaries.
This does not contract a guest multiply followed by an add into one operation.
The prior result remains an input to a separate explicit operation. Division,
64-bit arithmetic, resource reads, bounds checks and texture filtering remain
on their original paths. The change is selected only by the Apple ARM64 MoltenVK vertex/fragment
runtime information; compute and other stages retain their existing lowering.

Build `rounding-math-test` explicitly. It executes both production variants and
requires identical finite/infinite result bits and identical NaN classification
for 1,048,576 randomized and edge-case arithmetic results. A cancellation case
requires the separate multiply/add rounding to remain intact. Shader
specializations for the two strategies must differ. An optional path stem saves
the two synthetic SPIR-V files, which can be validated independently:

```sh
cmake --build /absolute/build/gpu --target rounding-math-test
VK_DRIVER_FILES=/absolute/MoltenVK_icd.json /absolute/build/gpu/rounding-math-test /absolute/private/scalar-math
spirv-val --target-env vulkan1.3 /absolute/private/scalar-math.reference.spv
spirv-val --target-env vulkan1.3 /absolute/private/scalar-math.optimized.spv
```

`GOW3_SHADER_AB_TEST=1` plus `GOW3_TOGGLE_FILE` permits a same-process A/B: mask `1152921504606846976`
disables the fragment strategy. Mask `2305843009213693952` disables only the
vertex strategy, and `3458764513820540928` disables both. `0` enables both with
the ordinary optimizations.
Normal sessions fix both strategies on for their lifetime. The diagnostic
strategies use separate shader and pipeline permutations. Switch only in
a frozen/Pause scene, then warm both before measuring. A combat run that changed
the strategy while active stopped on an invalid guest resource descriptor;
normal startup and timing must therefore be checked separately. This diagnostic
is not exposed as an in-game setting, and normal test shortcuts clear the toggle
file environment variable. A shader generation bump invalidates previous cached programs, so the
first launch can recompile shaders. Run clean FPS comparisons without counter
injection, API validation or image capture in their timing windows; collect
individual pass intervals separately. Counters and the Metal HUD are diagnostics,
not proof of a clean frame-time gain.

See the precise helper implementation in
[SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross/blob/main/spirv_msl.cpp).

## Independent stencil projection attachments

The public Metal probe now records the host blend factors, write masks and
bound depth/stencil descriptors. This distinguishes a stencil mask from a
meaningful depth test. The optional split-depth path keeps the combined backing
image and its stencil plane, and omits only an unused depth attachment. The
capability check matches the renderer's existing handling of unsupported host
depth-bounds testing. Depth comparisons/writes, supported bounds, stencil zfail
dependencies and HTILE maintenance remain conservative. A stencil-only pass
must not consume a deferred depth clear. The attachment choice is part of the
pipeline key and render-state memo signature.

Use the included pinned MoltenVK build recipe before opting in with
`GOW3_SPLIT_DEPTH_PLANE=1`. Stock 1.4.2 can infer a Metal pipeline depth format
from a combined stencil format while creating no corresponding render-pass
depth attachment. Metal validation then rejects the incompatible pipeline.
The included patch limits that inference to legacy render passes. See the
[upstream conversion](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/GPUObjects/MVKPipeline.mm#L1840).
The stock driver remains the default when the renderer option is unset.

`stencil-plane-test` checks actual Metal masked blending, preserved depth and
preserved stencil against a combined-attachment reference. It uses synthetic
inputs, no game data. `depth-stencil-state-test` also covers supported/unsupported
bounds and stencil depth-failure dependencies. Run the usual arithmetic, gamma,
constant-alias, detile and depth/stencil-blit checks against the selected ICD.
For diagnostic frozen-pose A/B, bit 59 disables the split-depth choice. Verify
all six actual host depth attachments disappear while all six stencil masks
remain, and compare complete command buffers and game-owned images. Never
infer a speedup merely from the attachment count or memory-traffic estimate.

## Presentation timing — 2026-10-18

For frame pacing, use the direct drawable observer in
[FRAME_PACING.md](FRAME_PACING.md). VideoOut flip intervals precede presentation.
MoltenVK 1.4.2 may replace a zero drawable presentedTime with callback CPU time
in VK_GOOGLE_display_timing, so extension-only timing does not independently
confirm distinct visible frames. Keep zero timestamps separate and use actual
nonzero Metal timestamps for interval distributions. The optional observer records
no game memory or pixels and never overrides a presentation method.
