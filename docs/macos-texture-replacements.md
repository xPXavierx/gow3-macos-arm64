# Experimental local color texture replacement

This opt-in developer feature is separate from the 0.4.0 application download.
It is a bounded test of four character/weapon color maps, not a whole-game HD pack.
No extracted, restored, or generated game pixels belong in this repository or its releases.

## Runtime

Set `GOW3_TEXTURE_PACK` to a local directory containing `.g3tx` files. With no pack,
the original texture path is used. An optional `GOW3_TEXTURE_PACK_CONTROL` file
containing `0` or `1` switches sampling during a frozen Photo Mode comparison.
This switch does not unload resident replacement images.

Identification uses a hash followed by an exact comparison of the complete original
base-level BC blocks copied from the actual Vulkan image. It does not identify a
texture by its guest address, nor trust a potentially stale CPU copy. Readback is
asynchronous, limited to 64 pending requests / 32 MiB, with upload revisions and
image lifetime checks. The descriptor memo is invalidated when a match becomes ready
or the comparison switch changes. The original image is used until then.

Only normalized sampling of single-layer, single-sample 2D BC1/BC3 color textures
with a complete base view is redirected. Integer-coordinate access, explicit minimum
LOD, storage, render/depth targets and GPU-written images use the original image.
CPU/GPU dirtiness, a changed backing or alias generation prevents stale replacement
sampling. Guest memory, pitches, original GPU images and CPU readbacks retain their
original dimensions and contents. The original GPU-to-CPU coherence policy stays on.

Replacement images use RGBA8 with a complete mip chain and compatible UNORM/sRGB
views. Component swizzles are preserved. The pack generator retains alpha from the
original DDS rather than the restoration model, and filters mips with premultiplied
alpha. It is intended for explicitly identified color maps; normal, roughness,
emissive and packed-channel maps require separate treatment.

The experiment accepts only 2x dimensions, sources from 32 to 2048 pixels per axis,
up to 64 files and 256 MiB of pack data. Resident images are shared by identical
content matches. RGBA8 deliberately avoids another lossy compression stage for
this first comparison, but costs much more memory than the original BC maps.
Texture binding helper work is serialized for this opt-in path; descriptor sets
using replacements are revalidated instead of using the native-image set memo.

## Preparing local files

Use an ARM64 restoration tool on copies extracted from the user's own dump.
The local experiment used the official ARM64 slice of
[Real-ESRGAN ncnn Vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan),
`realesrgan-x4plus`, then reduced its 4x output to 2x. Restoration is performed
offline. No neural model runs during gameplay and no Rosetta is involved.
The output is inferred detail, not recovered original high-resolution artwork.

With native Python, Pillow and NumPy installed:

```sh
python3 packaging/macos/native/create_texture_pack.py original.dds restored.png local-pack/material.g3tx
```

The local test app may contain `Contents/Resources/TexturePack`. Its launcher then
shows Original / 2x Character (test), remembers the choice and passes the pack only
when selected. Without that local folder, the option is hidden. No pack is included
in the community download. The same launcher still offers English, Brazilian
Portuguese and Spanish, Retina, Game Mode, native/MetalFX resolution and a 60 FPS cap.

## Verification

Build `gow3gpu`, `texture-replacement-data-test` and `texture-replacements-test`.
The first test rejects wrong dimensions, incomplete mips and malformed/truncated
headers. The Vulkan test uses a synthetic BC1/RGBA fixture, verifies the actual
upload/readback/matching path, compatible views and fallback after CPU/GPU writes,
alias changes, native-coordinate access and source content changes.

For visual A/B, freeze the same pose and camera in Photo Mode, warm both modes,
then switch the control file. Take screenshots outside timing windows. Compare
actual presentation intervals as well as renderer FPS; zero Metal presented-time
callbacks are invalid display samples. Alternating active combat is a regression
test, but different enemies/draw counts cannot establish a small texture-only cost.
Do not infer the cost of replacing every texture from four color maps.
