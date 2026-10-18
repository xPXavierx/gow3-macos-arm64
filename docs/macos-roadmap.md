# macOS roadmap

The retained reference is native ARM64, native 3840×2160, 60 FPS, FIFO V-Sync,
frame generation off and the upstream texture-readback policy enabled. Keep it
available when testing any candidate. The campaign and other Macs remain untested.

## Community distribution

The public fork contains sources, tests, dependency patches and local build
recipes. The existing local `.app` contains game-derived AOT libraries and linked
images; do not publish it as a downloadable game or GitHub release asset.

The next packaging milestone is a redistributable runtime/launcher that accepts
the user's own dump and prepares its AOT profiles locally. That still requires
bundling the tools, showing progress, resumable builds, validating game hashes,
disk/RAM requirements and safe recovery from an interrupted first run. The current
launcher selects already prepared profiles; this milestone is not implemented.

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

No high-resolution replacement pack is currently implemented or validated. Keep
derived texture packs private to the local dump preparation workflow. No texture
enhancement is enabled by default just because an offline preview looks sharper.

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
