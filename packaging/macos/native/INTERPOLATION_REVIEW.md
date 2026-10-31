# Native interpolation review (0.4.16 local)

## Follow-up: actual autosave, uncapped only

The user's session selected uncapped generation but generated zero frames. A
reproduction with a copy of that autosave confirmed a display-buffer identity
failure, independent of interpolation mode. The earlier combat save had not
exposed this ordering difference.

Depth/motion and final scene color were captured correctly, but Snapshot used
the previously observed VideoOut attachment as the expected flip address. In
this area the current scene is copied to VideoOut after HUD composition; the
saved address consistently lagged by one buffer in the three-buffer rotation.
Ready rejected every frame and the Metal 4 interpolator was never configured.

Display now associates a completed color capture with the current display
attachment when that draw is observed. Ready still requires its address to
match the actual flip. Missing inputs still reset history and retain real
draws. The fix does not disable that check or accept an arbitrary next flip.

For this investigation the launcher offers only Off and Uncapped. Key 2 toggles
those same states (0/3); legacy 30+30/half-refresh preferences migrate to
Uncapped. Half refresh is deferred. The older experiments below are historical,
not the current user-facing choices.

In the affected autosave, 25 seconds of attack animations produced 45.25 real
and 45.29 generated presentations per second (90.49 total). Actual presentation
p95 was 16.67 ms, maximum 29.17 ms. Intermediate captures differed from the
current composed image in 95.7% of moving world pixels and from the previous
scene in 98.0%; this rules out simply presenting a duplicate. It does not
qualify artifact-free interpolation across the campaign.

Repeated paused comparisons at 4K output measured about 83–86 total FPS with
native rendering and generation, versus 64–68 real FPS with generation off.
MetalFX Quality plus generation reached 99.5 total; Performance reached 102.8.
These are short local windows with measurable drift, not a claim of sustained
120 FPS. Native 4K still pays substantial real-frame cost for interpolation.
All these generation measurements used Uncapped, not half refresh.

## Earlier 0.4.15 investigation

The FSR 4.1.1 experiment was removed. This build retains the 0.4.13 MetalFX spatial
upscaler, physical scene targets, native CPU register-cache optimization, texture
coherence rules and fused SDR RCAS presentation. CPU AOT preparation is unchanged.

## Why the previous frame-generation counter did not imply useful motion

The important failure was in the game's temporal inputs. The depth-clear hook
captured scene color from an earlier VideoOut image. The final composed image
contained the later scene effects and HUD. Consequently, the two inputs differed
throughout the world, and MetalFX UI decomposition preserved almost the entire
current image as if it were interface. An actual game capture showed only
0.045/255 mean difference between the generated and current composed world.
Counting presentations, or testing a synthetic moving object, did not detect
this integration failure.

Depth and vertex motion are still captured before the tracked scene-depth clear.
Color is now captured from the current full-size color attachment before the
first subsequent indexed HUD geometry. This includes opaque 3D interface
geometry with depth testing, which a blend-only/depth-disabled UI classifier
misses. The attachment size and byte-color format are checked; missing inputs
invalidate history and retain real frames. No guest draw is suppressed. This
uses the common rendering transition, without a level name or material hash.

In the corrected combat capture, the mean world difference between scene color
and final composition, excluding HUD regions, was 0.119/255 instead of 9.294/255.
The generated image differed from both real endpoints in approximately 93% of
the moving pixels. The HUD remained composed separately. These are bounded
capture checks, not a full-campaign artifact-free qualification.

## What was learned from the installed TOTK implementation

Both projects use Apple's native Metal 4 frame interpolator. The local TOTK
implementation supplies scene color and premultiplied UI separately, reconstructs
camera motion from depth/camera matrices, and rejects camera flow on moving
surfaces. GoW retains actual vertex motion and the supported composited-UI
contract, now with matching scene input. Copying TOTK's camera-only motion would
lose GoW's existing object motion.

The other substantial difference is presentation. TOTK uses CAMetalDisplayLink
and its supplied drawables, a separate presentation path, bounded resource slots
and completion tracking. GoW's previous path asked MoltenVK for nextDrawable
while holding its Vulkan submission lock. Evenly spaced requested timestamps
could still become alternating short/long intervals on the ProMotion panel.

## Presentation integration

The patched MoltenVK can obtain a drawable from the native host instead of
calling nextDrawable. In the qualified 30+30 mode, CAMetalDisplayLink owns the layer.
Its drawables use present(), as required by that API; presentAtTime is reserved
for the ordinary path. The callback's target presentation timestamp bounds the
age of prefetched drawables. The experimental higher-rate path holds at most two unused drawables ahead;
these have not been assigned any game image. No rendered game image
is discarded by this mechanism.

Ownership changes wait for an ordered presentation-scheduler marker without
holding the global submission mutex. This is necessary because native MetalFX
queues can await ready events produced by another Vulkan submission. The display
link is invalidated before ordinary acquisition resumes; pausing it is not
sufficient. A callback timeout detaches it and restores the ordinary path, with a
three-second retry delay. Background/occlusion behavior still needs wider testing.

MoltenVK's existing serial asynchronous submission path is used during frame
generation. Returning to synchronous mode first drains dispatched jobs and
preserves queue order. With generation off, the previous synchronous policy is
retained. This does not create another Vulkan graphics queue: the tested driver
exposes one queue in that family. The native MetalFX queue remains separate.

The adapter explicitly registers protocol version 1 with the renderer. An older,
unpatched MoltenVK cannot enable layer ownership, so rebuilding the renderer
alone does not make nextDrawable race an unsupported display link. The launcher
sets GOW3_METAL_PACING_HOST to its bundled renderer, and symbol lookup only opens
an already-loaded image (RTLD_NOLOAD).

Physical scene/UI targets are sampled directly where current, avoiding a
logical-resolution resolve followed by another reduction. Generated SDR output
also uses the same fused RCAS presentation as real frames. Real and generated
colors retain their appropriate RGBA/BGRA and sRGB views. Generated color remains
at the selected render resolution; it is not silently reduced further.

## Build and validation

Build the renderer normally, including vk_metal_display_link.mm. Rebuild the
pinned MoltenVK with build_moltenvk.py --frame-pacing. The complete dependency
patch is moltenvk-present-duration.patch; its original cap-60 extension is
retained alongside the native adapter. Bundle both updated libraries together.
No change to the CPU AOT recipe or shader catalog ABI is required.

Validation includes production GPU input packing, actual Metal 4 interpolation
with a moving synthetic object, RGBA/BGRA and compact/full motion variants,
stationary HUD, direct RCAS and MetalFX interop. Real-game captures additionally
check the scene/UI contract and distinguish generated contents from both real
endpoints. Timing uses distinct nonzero VK_GOOGLE_display_timing actualPresentTime
values; generated presentations never advance simulation or game flips.

The higher-rate display-link experiment was not retained as the launcher
default: GPU-saturated native 4K missed deadlines, and adding a CPU ready wait did
not improve it. Half-refresh and uncapped generation retain ordinary drawable
acquisition, with corrected temporal inputs and asynchronous submissions. This
preserves the real-frame path while further presentation work is evaluated.

The default remains frame generation off. Half refresh targets half the reported
display rate in real frames; on a 120 Hz display that is 60 real + 60 generated.
It cannot guarantee that throughput when the GPU lacks sufficient time. 4K output
with MetalFX Balanced renders internally at 1920x1080; it is not native 4K. Native
4K generation remains significantly more expensive. Input latency and complete
campaign fidelity are not yet qualified.

Private local measurement files are under work/native/interpolation-review and
work/native/community-runtime/interp-validated in the development workspace.
Failed/death-menu runs are excluded from accepted performance comparisons.

References: [Apple CAMetalDisplayLink](https://developer.apple.com/documentation/quartzcore/cametaldisplaylink),
[Apple targetPresentationTimestamp](https://developer.apple.com/documentation/quartzcore/cametaldisplaylink/update/targetpresentationtimestamp),
and the locally installed TOTK source under TOTK_build/totk/third_party/d2r_gfx/gfx/nvn.
