# Retired texture replacement experiment

**Inactive since 2026-10-18.** Local generated packs and the texture test app were
deleted at the user’s request. No pack ships with either application. This document
records the implemented source format and validation for future developers; it is
not an installation guide for an available HD texture feature.


This opt-in developer feature is separate from the 0.4.0 application download.
The local expanded experiment covers Kratos and the POS050 opening area, including
Gaia, ground, foliage, the Hippocampus and the weapon/NPC dependencies loaded there.
It is not a whole-campaign HD pack. No extracted or restored game pixels belong in
this repository or its release downloads.

## Runtime

Set `GOW3_TEXTURE_PACK` to a local directory of `.g3tx` files. Without a pack,
the original texture path is used. An optional `GOW3_TEXTURE_PACK_CONTROL` file
containing `0` or `1` switches sampling during a frozen Photo Mode comparison.
Switching does not unload resident replacement images.

Identification hashes and then compares the complete original base-level bytes
copied from the actual Vulkan image. Neither a guest address nor a possibly stale
CPU snapshot identifies the replacement. Readback is asynchronous, bounded to
64 pending requests / 32 MiB, and checked against upload revisions and lifetime.
Queue overflow defers identification; sampling retries after previous requests
retire. Checked misses stay cached. Descriptor memo entries are invalidated when
requests retire or the comparison switch changes.

Only normalized sampling of single-sample 2D BC1, BC3 and RGBA8 images is redirected.
One layer or six layers (cube faces represented by the renderer as a 2D array) are
supported, including individual face views. Integer-coordinate access, explicit
minimum LOD, non-base mip views, storage, render/depth targets and GPU-written
images use the original. CPU/GPU dirtiness, changed backing or alias generation
prevents stale sampling. Guest memory, pitches, original images and CPU readbacks
retain their original dimensions and contents. GPU-to-CPU coherence stays enabled.

The loader indexes dimensions and content hashes, keeps only original identification
bytes in CPU memory, and reads replacement payloads lazily into upload staging.
Compatible UNORM/sRGB views and component swizzles are preserved. ASTC 4x4 LDR
support is checked before accepting compressed packs. There is no decompression,
neural inference, CPU architecture translation or Rosetta in gameplay.

Limits: 2048 files, 4 GiB total file data, 128 MiB per file, and 4 GiB of resident
replacement payloads. Source dimensions are 1–2048 per axis and output dimensions
must be exactly 2x. Original images remain resident as required by guest tracking.
Texture helper work is serialized for this opt-in path; descriptors using a
replacement are revalidated instead of using the native-image set memo.

## Format

All integers are little-endian. Version 1 (`G3TXv001`) retains its 40-byte header
and RGBA8 mip chain for the initial four-color-map test.

Version 2 (`G3TXv002`) has a 64-byte header:

| Offset | Type | Value |
| --- | --- | --- |
| 8, 12 | u32 | original width, height |
| 16 | u32 | source block: 8=BC1, 16=BC3, 4=RGBA8 texel |
| 20 | u32 | layers: 1 or 6 |
| 24 | u64 | exact original base byte count, all layers |
| 32, 36 | u32 | output width, height |
| 40 | u32 | complete output mip count through 1x1 |
| 44 | u32 | output encoding: 1=RGBA8, 2=ASTC 4x4 |
| 48 | u64 | output payload bytes |
| 56 | u64 | reserved, zero |

The header is followed by tightly packed original base bytes, layer by layer,
then a complete replacement pyramid in **mip-major, layer-minor** order. Each ASTC
mip/layer occupies `ceil(width/4)*ceil(height/4)*16` bytes, including small tails.
No padding, trailing bytes, or partial pyramids are accepted.

## Preparing local files

Use native ARM64 tools on copies extracted from the user's dump. Color restoration
is offline inferred detail, not recovered original high-resolution artwork.
The first test used official
[Real-ESRGAN ncnn Vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan),
`realesrgan-x4plus`, reduced to 2x. The expanded local test uses
[RealESRGAN_x2plus](https://github.com/xinntao/Real-ESRGAN/blob/master/docs/model_zoo.md)
on PyTorch MPS and native
[astcenc](https://github.com/ARM-software/astc-encoder) at 4x4 / medium.

Restore only identified color images. Packed normal channels, masks, lookup tables,
and particle/blood masks must not pass through a generic RGB restoration model.
The expanded experiment resamples these as data, keeps their authored lower mips,
and retains original alpha at corresponding mips. Restored color mips are filtered
from restored RGB using premultiplied alpha; authored alpha is retained. Increasing
data-map dimensions alone does not add surface detail or change geometry/lighting.
Alpha is retained before lossy ASTC encoding; compressed texels are not bit-exact.
Review cutout coverage, tile seams and material appearance in-game.

A developer helper for a single 2D BC color map is available with native Python,
Pillow and NumPy:

```sh
python3 packaging/macos/native/create_texture_pack.py original.dds restored.png local-pack/material.g3tx --astc-encoder /path/to/arm64/astcenc
```

Omit `--astc-encoder` to retain the uncompressed v1 test format. The helper does not
classify a whole game's materials or generate a full pack automatically.

A local test app may contain `Contents/Resources/TexturePack`. Its launcher shows
Original / 2x Kratos & opening, saves the choice, checks every listed file against
`manifest.json` before launch and passes the directory only when selected. Original
is the default. Without a local pack the option is hidden. No pack is included in
the community download. English, Brazilian Portuguese, Spanish, Retina, Game Mode,
native/MetalFX resolution and the 60 FPS cap remain available.

## Verification

Build `gow3gpu`, `texture-replacement-data-test` and `texture-replacements-test`.
Parser tests reject malformed/truncated files, incorrect dimensions, layers,
payload lengths and mip counts. GPU fixtures cover legacy RGBA output and ASTC,
BC1 and RGBA8 sources, six-layer/face views, sRGB/swizzles, CPU/GPU/alias invalidation,
source mismatch, stale in-flight requests, and recovery after a 65-image queue burst.
Generate reproducible fixtures, then run the selected directory (pass `cube` or
`rgba` as the second test argument for those source types):

```sh
python3 tests/make_texture_replacement_fixtures.py /tmp/gow3-texture-fixtures --astc-encoder /path/to/arm64/astcenc
texture-replacements-test /tmp/gow3-texture-fixtures/astc
texture-replacements-test /tmp/gow3-texture-fixtures/cube cube
texture-replacements-test /tmp/gow3-texture-fixtures/rgba rgba
```

Run with Vulkan and Metal validation. The synthetic fixture generator includes no
game pixels and can also generate a legacy fixture without external dependencies.

For visual A/B, freeze the same pose and camera in Photo Mode, warm both modes,
and switch the control file. Capture outside timing windows. Compare actual
presentation intervals and renderer FPS. Zero Metal presented-time callbacks are
invalid display samples. Active combat with changing draw counts is a regression
test; it cannot establish a small texture-only cost. Do not infer a whole-campaign
quality or performance result from the opening area.
