# macOS roadmap

The retained reference is native ARM64, native 3840×2160, 60 FPS, FIFO V-Sync,
frame generation off and the upstream texture-readback policy enabled. Keep it
available when testing any candidate. The campaign and other Macs remain untested.

## Community distribution

The public fork contains sources, tests, dependency patches and local build
recipes. The existing local `.app` contains game-derived AOT libraries and linked
images; do not publish it as a downloadable game or GitHub release asset.

The community first-run preview now bundles the ARM64 tools and an offline
Zig/Clang compiler with this same launcher. It validates the tested dump, shows
progress, handles cancellation and verifies/reuses locally generated profiles.
See [installation and packaging](../packaging/macos/launcher/COMMUNITY.md).
Developer ID signing/notarization, other-Mac testing and broader dump-version
coverage remain pending. Keep the older private prebuilt-profile app separate.

Retain upstream history, license notices and attribution. Keep the tested base
pinned while reviewing newer upstream changes separately. A source build and
existing tests do not establish Windows/Linux or full-campaign compatibility.

## Texture quality

Render resolution and texture resolution are different. The audited blade
materials reference 512×512 color/detail maps. Increasing the output to 4K cannot
restore information absent from those maps. The earlier subset audit is not an
inventory or integrity certificate for the entire game.

The 2026-10-18 read-only local inventory scanned 816 WAD archives (23.08 GB)
and recognized 24,983 BC1/BC3 DDS occurrences with 8,540 distinct resource names.
The new tool agreed with all 1,464 previously independently audited resources.
It excluded 147 resources in another format and flagged one 64×40 UI resource
whose declared mip chain exceeds its embedded payload. That isolated header/data
mismatch needs classification; it is not a certificate that the user's dump is
corrupted. No whole-game texture replacement or new gameplay-quality benchmark
was performed in this inventory step.

Start by measuring the resources rather than upscaling every file:

1. Inventory DDS resources across the local WADs, recording dimensions, mips,
   format and payload bounds. `audit_dds_archives.py` reads only and exports
   metadata. It does not decode pixels or certify other asset/container formats.
2. Compare normal filtering with the existing `GOW3_ANISOTROPY` option in the same
   paused scene and repeat the same combat route for cost. Anisotropy helps angled
   surfaces; it does not create new texture detail. Preserve point-filtered
   lookups, depth, mip selection and the original texture-readback rules.
3. Implement an optional external replacement cache for a small set of immutable
   color textures, keyed by content, before considering a whole-game pack. Keep
   guest addresses, pitches and readback images intact; a larger host sampling
   image must not silently replace a render target or guest storage image.
4. Compare original and 2× replacements in-game with generated mip chains,
   correct sRGB/linear handling and independent treatment of color, normals,
   packed material channels and alpha-tested edges. A generic color upscaler is
   not an acceptable normal/roughness-map transformation.
5. Measure memory, upload time, GPU time, frame-time percentiles, seams and
   temporal stability. A 2× increase on both axes uses 4× the texels at the same
   format, before considering transient generation/upload buffers. Scale beyond
   the proof only after a visible, measured benefit.

The 2026-10-18 experiment implemented a content-keyed sampling cache, first with
four character/weapon color maps and then with Kratos plus the POS050 opening
area (ground, foliage, Gaia and loaded weapon/NPC dependencies). The expanded
local set contains 437 unique GPU identities covering 459 distinct DDS payloads.
It uses offline 2x color restoration and separately resampled packed data, ASTC
4x4 compression, complete mip chains, preserved view colorspace/swizzles, lazy
uploads and bounded asynchronous identification. Original guest images and the
readback/coherence rules remain intact. See
[the mechanism and developer workflow](macos-texture-replacements.md).

The expanded native-4K run applied 194 different replacement images, adding about
1501 MiB of GPU allocations. In four frozen 30-second comparison windows, original
presentation rates were 59.99/60.00 FPS and replacement rates 51.69/59.99 FPS.
The first replacement window included an isolated 545.1 ms renderer stall and
some zero Metal presentation callbacks; retain this anomaly rather than claiming
an uninterrupted 60 FPS result. There were no shader compiles or replacement
uploads in that window. Its cause remains unassigned.

Across alternating 20-second active combat windows, originals presented
58.75/59.99 FPS and replacements 60.01/60.01 FPS. Enemy/draw counts changed, so
these are regression observations, not evidence of a texture-induced speedup.
Presentation p95 remained near 20.83 ms, retaining the existing cadence issue.
Separate validation with the final app reached gameplay and exercised sampling
with no reported Vulkan validation errors. All CPU translation/interpreter
counters remained zero. This does not cover the full campaign or other Macs.

Visible improvements are modest; geometry and lighting are unchanged, and data
resampling alone creates no new normal-map detail. The local test launcher
remembers Original / 2x Kratos & opening. Without a pack it hides that option;
enhancement remains off by default. Keep derived packs outside community downloads.
Whole-campaign restoration, better authored/estimated surface detail, precise GPU
texture-only cost, the isolated stall and broader validation remain open work.

## GPU work and smoothness

Use direct Metal presentation timestamps and separate profiling captures from
clean timing. The five-minute 4K checkpoint is close to 60 confirmed
presentations/s, but still has roughly 12.5/20.8 ms interval alternation. Improving
that distribution is a separate objective from lowering average GPU time.

Prioritize current material/screen-effect hot spots only after a new profile
confirms their cost. Compare one general mechanism at a time and retain changes
only with visual checks and measured gains. Log render/output sizes, upscaler,
shader compiles and scene state; exclude menu/death-screen time from combat FPS.
Keep frame generation off. See [measurement instructions](../packaging/macos/native/FRAME_PACING.md).
