# Community graphics preparation and live controls (macOS ARM64)

This work follows the architecture of [MacNativeGames](https://github.com/ly05010419/MacNativeGames)
(revision `5281186af56bdcce6ae9b3bb9daa9791bc02994e`): collect descriptions of the
states actually used by the game, merge discoveries, and build device-specific
GPU programs locally before playing. God of War uses its own shaders, resource
layouts, projection, depth and animated-object motion. No Zelda shader data or
Switch keys are used by this application.

## Community catalogue

The launcher offers **Community catalogue → Import catalogue / Export discoveries**.
Play normally, exit the game, then export a `.jsonl` contribution. Another player
can import that file while their game is closed. The next launch merges it with
the bundled catalogue and that player's local discoveries, rebuilds shader
programs from their own verified dump, and prepares complete known pipelines.
Imports are deduplicated and validated before installation. There is no automatic
upload, submission, account or network service.

Supported game: **CUSA01623, update 1.02**, with executable SHA-256
`d85c8135d330c3b601bf5dc3f1dd86bd6119fb8a9fce372bed2c9785f9a79299`.
A different executable accepted through `accepted-executables.json`
(see [COMMUNITY.md](../packaging/macos/launcher/COMMUNITY.md)) still uses this
same catalogue, rebuilt locally from that dump; descriptions that do not match
its code are rejected and complete required rendering applies.
The renderer ABI must also match; only explicitly supported description migrations
are accepted across versions, and other catalogues are rejected. This initial format records vertex/fragment specializations and graphics
pipelines. It does not cover every compute stage or every possible campaign state.
The initial bundled corpus contains **831 shader specializations and 445 graphics
pipeline descriptions**, recorded in the opening/menu; coverage grows through
actual discoveries elsewhere. A whole-game asset index identifies code in the dump;
it does not by itself prove all material and pipeline combinations are known.

Files contain canonical numeric resource/specialization descriptions, source
hashes and pipeline state. Shader identity excludes the contributor GPU's reference
SPIR-V hash. Files contain no SPIR-V, Metal library, game shader instructions,
resource-walker instructions, executable, save data or absolute source paths.
The numeric specialization payload is tied to the renderer ABI, separately from
the JSON envelope version. Local GSR2/GPR2 reconstruction files use SHA-256 checksums.
The native loader regenerates resource traversal from the player's own source and
checks the expected layout fingerprint and complete specialization.

Recording uses a bounded low-priority worker queue. A contribution is published
only after reconstructing it locally and comparing its emitted program with the
actual program used by the renderer. A full queue defers recording. It never
drops a draw. Missing or incompatible catalogue entries use complete required
rendering: compilation can run in background, but a draw whose indispensable
program is still unavailable waits rather than disappearing.

User data stays under `~/Library/Application Support/God of War III ARM64`:

- `Community/imported`: explicitly imported JSONL catalogues.
- `Community/recording/<session>`: temporary recorded descriptions, scoped to one process.
- `Community/contributions/<renderer-ABI>.jsonl`: merged local discoveries after a session.
- `User/cache/CUSA01623/CommunityCatalog`: derived launch descriptions.

**Clear shader cache** removes the derived graphics cache, retaining imports,
local discoveries, saves, settings and ARM64 recompilation. This allows cold GPU
cache comparisons without throwing away the shared preparation knowledge.

### Guest early depth (0.4.17)

`DEPTH_BEFORE_SHADER` is preserved from the PS4 register through fragment-stage
specialization, variant equality and SPIR-V `EarlyFragmentTests`. The mode is
emitted only when requested by the game, never inferred from the `Z_ORDER` hint.
A fragment shader exporting depth cannot use this mode.

Shader binary version 33 and ARM64 metadata version `0x41520011` invalidate the
previous GPU cache generation. The current catalogue ABI is `f9b9a649560a0b85`.
The bundled seed contains 1,300 shader descriptions and 962 pipeline descriptions.
Known legacy ARM64 descriptions (`f9bf6c9321b85a41`, `f9baa7c07fa3310e`) can be
rebuilt locally: their old zero padding did not capture the guest bit, so both
states are prepared and the native compiler rejects illegal candidates. This
does not skip any live draw or reuse old GPU binaries. New discoveries record the
actual bit. Saves, settings and CPU AOT are unaffected.

Controlled 4K tests did not show a measurable FPS improvement from this change;
the tested live scenes did not request the bit. This is a correctness fix, not a
demonstrated solution to the frame-generation overhead. Older ABI numbers below
describe earlier implementation steps.

## Live controls

The launcher stays open as the startup fallback. The game uses a native ARM64
profile with a 240 FPS engine ceiling and live presentation limits. Changes are
latched at a frame boundary; resolution changes drain dependent GPU work before
retiring old resources. The screen remains Retina/high-density and uses the
macOS fullscreen Space for Game Mode eligibility.

| Key | Action |
| --- | --- |
| **0** | Show/hide shortcut hints at the upper left, independently of performance HUD |
| **1** | Performance HUD on/off: FPS, CPU%, GPU%, render/output sizes, MetalFX |
| **2** | Native → spatial MetalFX quality → balanced → performance |
| **3** | 720p → 1080p → 1440p → 2160p |
| **4** | 60 FPS cap on/off |

Frame generation is excluded from build 0.4.18: no launcher option, no interpolation shortcut
binding, and no MetalFX interpolator is compiled. Old frame-generation preferences
are ignored; an old enabled choice migrates to the safe 60 FPS mode. The shortcuts are consecutive: 0 hints, 1 performance HUD, 2 MetalFX,
3 resolution and 4 the 60 FPS cap.

Enabling the 60 FPS cap leases a matching 60 Hz display mode without changing
logical size, physical pixel size or Retina scale. Uncapping releases that lease;
exit/crash releases it as well. Spatial MetalFX remains available.

The CPU percentage is process CPU time normalized across all logical cores.
The macOS GPU activity counter is system-wide and is labelled `(system)`; it
includes the compositor and other GPU applications. Values unavailable on a GPU
are shown as unavailable, never fabricated as zero.

## Archived frame interpolation experiment

The following describes an excluded development experiment, not an available
feature. Its source is retained for future research; it is not linked into the
current application. Shared HUD and timestamp functions live in the display adapter.

The Metal 4 implementation uses the public `MTL4FXFrameInterpolator`, explicit
Vulkan/Metal shared-event ordering, three reusable command-allocator/residency
slots and a bounded cache of four exact effect configurations. Each slot is reused
only after its own submission completes; frame resources stay retained until
completion. The cache retains effects, not old frame textures. History resets on
configuration changes. Actual GoW object/camera/depth inputs remain in use.
Unsupported or incomplete input frames retain the real frame.

This adapts the configuration caching, explicit lifetimes, mode selection and
half-refresh pacing ideas from MacNativeGames. The GoW Vulkan/MoltenVK presentation
path differs from TOTK's direct Metal display-link path, so cadence and GPU cost
must be measured in the actual GoW scene rather than inferred from TOTK.

Apple references:
[Metal 4 frame interpolator](https://developer.apple.com/documentation/metalfx/mtl4fxframeinterpolator),
[Metal 4 core API](https://developer.apple.com/documentation/metal/understanding-the-metal-4-core-api),
[Metal HUD control](https://developer.apple.com/documentation/xcode/customizing-metal-performance-hud).

Validation evidence for the local candidate is stored separately under
`work/native/community-runtime`. Builds are not considered validated by compilation
alone; see that directory's game logs, captures and measurements for limitations.


## Live resolution correctness (0.4.8 local)

The fixed AOT profile uses a 3840x2160 logical display while raster proxies
change resolution. Pixel-coordinate reads must retain logical image dimensions,
including reads requested by a sampler's `force_unnormalized` flag. Sample
lowering marks these resources as requiring native dimensions before binding.
Shader metadata version `0x4152000f` invalidates older cached classifications;
the corresponding catalogue ABI is `f9bf6c9321b85a41`.

The intermediate used to resize input for spatial MetalFX retains the display's
sampling view independently of the backing allocation format. The game can use
BGRA views on RGBA allocations; replacing that view swapped red and blue.

Regression coverage includes eight sampler/instruction/MSAA combinations through
the production resource-patching pass, speculative rejection without noexcept
termination, GPU color/depth/stencil proxy round trips and actual game captures
at 720p, 1080p, 1440p and 2160p plus three MetalFX presets. The picture fills its
16:9 presentation area and keeps the display's aspect ratio on other screens.

The bundled seed contains 880 shader specializations and 481 graphics pipelines,
including verified discoveries from Monte Olimpo. Recording also worked in
Tumba de Ares. This is not a full-campaign catalogue. Imported descriptions reduce
known compilation work, but do not guarantee that all frame-time spikes vanish.

## Physical render chain and live presets (0.4.11 local)

The logical coordinate grid and the physical render dimensions are now independent.
Live sessions retain the 3840x2160 AOT grid so changing from a smaller preset back
up to native 4K does not lose texel positions through integer coordinate rounding.
This is allocation/addressing compatibility, not 4K rasterization. The active
scene color, G-buffer, depth and eligible post-processing attachments are physical
images at the requested render size (or the corresponding half/mip size).

| Output | Mode | Physical scene input |
| --- | --- | --- |
| 3840x2160 | Native | 3840x2160 |
| 3840x2160 | MetalFX Quality | 2560x1440 |
| 3840x2160 | MetalFX Balanced | 2272x1278 |
| 3840x2160 | MetalFX Performance | 1920x1080 |

Integer texture fetches map logical pixel centers to physical texels. Size queries
and pixel-coordinate samplers see their descriptor's logical dimensions without
inserting extra guest-buffer/SRT reads. Exact-view guards remain independent of
proxy eligibility. Fragment coordinates use the exact logical and physical scene
sizes, avoiding truncated ratios after live changes. Sampled depth views also
consume their current physical image.

Feedback effects read a separate snapshot at physical resolution while writing
their physical attachment. Early eligible passes also scale, including those
before the first main geometry draw. These changes remove per-frame resolves to
the logical 4K image from the tested combat render chain. Fixed shadow maps and
small exposure resources retain their own independent dimensions.

MetalFX receives the physical final image once and produces the selected output
size. RGBA/BGRA mappings, color encoding, Retina presentation, HUD controls and
Game Mode metadata remain intact. Required draws are never omitted. Unsupported
storage/MSAA/offset/alias cases retain the correctness fallback; this is not a
claim that every campaign effect has been captured and timed.

Shader binary generation is 32, ARM metadata generation is `0x41520010`, and the
catalogue ABI is `f9baa7c07fa3310e`. Logical sizes are additional host push data;
the resource layouts and portable catalogue description schema stay unchanged.
The driver push-constant limit is checked before pipeline creation. The catalogue
is rebuilt locally for this renderer; saved games, launcher settings and ARM64 AOT
libraries are separate and are preserved.

Comparisons must use Unlimited to expose frame-rate differences. The 60 FPS cap
continues to limit output to 60, even when a lower input saves GPU time.

## Historical investigation: direct startup profiles (0.4.10 local)

The launcher now selects the AOT input profile at the chosen render resolution,
including when live shortcuts are enabled. Previously every live session selected
3840x2160 logical game buffers, even with a lower MetalFX input. Integer texture
reads, alias copies and final presentation repeatedly materialized those large
compatibility images. This was an implementation limitation of the live controls,
not a requirement of MetalFX.

| 4K output mode | Game render / AOT input | Presentation |
| --- | --- | --- |
| Native | 3840x2160 | 3840x2160 |
| MetalFX Quality | 2560x1440 | 3840x2160 |
| MetalFX Balanced | 2272x1278 | 3840x2160 |
| MetalFX Performance | 1920x1080 | 3840x2160 |

At startup the logical and physical scene dimensions match, so the normal scene
chain needs no parallel resolution proxies. MetalFX receives the rendered image
directly. Presentation also consumes the current physical target after a live
change, retaining its size and RGBA/BGRA view mapping instead of resolving to the
startup size and resizing again. The view is exported to Metal without a CPU copy.

Live changes still retain compatibility copies for game shaders that require
logical integer coordinates, compute, aliasing and readbacks. These guards preserve
texture contents and are not disabled for speed. Starting with the desired launcher
preset avoids that live-change overhead; performance after a live switch may differ
from performance after launching with that preset.

The launcher retains full-target discard nearest copies where compatibility is
needed (`GOW3_SCENE_DISCARD_RESAMPLE=1`), including combined depth/stencil where
supported. Integer and uncommon formats keep their exact existing copy path. The
shader-catalogue ABI remains `f9bf6c9321b85a41`; required draws are never omitted.

GPU regression checks cover color/depth/stencil round trips, nearest split-pattern
boundaries, half-resolution mip chains and direct presentation of targets both
smaller and larger than their logical image. Launcher tests cover every resolution
and MetalFX preset, ensuring live controls do not force a 4K input profile.

Alias copies that fully overwrite their destination now sample only the requested
logical rectangle from a current proxy. A source-sized viewport and smaller render
area reproduce the old full-resolve pixel centers without reconstructing the whole
source. Formats must match exactly; multisampled, layered, mipmapped, partial-write
and incompatible-format copies keep the existing path. Diagnostic toggle bit 63
restores full resolves for same-frame comparisons.

On this M3 Max, two 90-second runs from the identical isolated Gaia checkpoint,
with warm caches, uncapped real frames, V-Sync enabled and frame generation off, measured:

| 4K output | Real FPS mean | Frame time p50 | Frame time p99 | GPU utilization, system mean |
| --- | ---: | ---: | ---: | ---: |
| Native | 75.13 | 13.69 ms | 16.00 ms | 96.8% |
| MetalFX Quality | 88.05 | 11.10 ms | 15.79 ms | 91.2% |

These combat runs measured the direct-input and presentation changes before the
additional clipped-alias optimization (inactive when input matches the startup
profile). Each run retained an isolated approximately 0.51–0.52 second spike with
no shader compilation recorded at that point; its cause remains unresolved.
The table includes those spikes. It is not a claim of stutter-free campaign play.

The clipped-alias change was then isolated in a paused scene after a live switch
from Native to Quality: full/direct/full/direct measured 67.71 / 71.67 / 67.68 /
71.65 FPS over 12-second windows. The four full RG16F source resolves were replaced
with destination-sized alias passes. Color comparison and RG16F packing checks
retained the reference result. The final renderer also passed a 30-second capped combat check (59.63 FPS mean,
17.21 ms p99, one 214.56 ms spike) and eight live setting changes while the
world remained visible. A separate run in which Kratos died was rejected. Frame
generation remains off throughout.

CPU sampling confirmed five game workers plus parallel graphics preparation and
recording. The game main thread remains a limit, and some active samples are busy
polling rather than computation. A repeated process-start diagnostic `getenv` in
`RefreshGraphicsKey` also contended on libc's environment lock across graphics
workers. Caching that flag removed this call site from the follow-up sample;
no isolated FPS gain is attributed to this small CPU change.
