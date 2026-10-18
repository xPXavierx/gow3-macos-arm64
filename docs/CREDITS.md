# ARM64 port credits

This work extends FelipePrado0/gow3_pc and retains its existing credits to
[deadinside28/bloodborne_pc (bbport)](https://github.com/deadinside28/bloodborne_pc)
and [shadPS4](https://github.com/shadps4-emu/shadPS4). Existing notices and
third-party licenses remain applicable.

The optional spatial antialiasing pass uses SMAA 1x by Jorge Jimenez, Jose I.
Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez (MIT), from
[iryoku/smaa](https://github.com/iryoku/smaa) revision
`71c806a838bdd7d517df19192a20f0c61b3ca29d`, including its area/search tables.
Its compute wrapper replaces discarded edge pixels with explicit zero writes.
FXAA 3.11 is by Timothy Lottes / NVIDIA (BSD-3-Clause), obtained from Intel's
[CMAA2 sample](https://github.com/GameTechDev/CMAA2) revision
`071c6b0857559f4e36f614362e6d2aab1b61938a`. The port adds full RGB luminance
evaluation without a separate luminance prepass. Both upstream copyright and
license notices are retained in `gpu/shadps4/video_core/host_shaders/spatial_aa/`.

The native CPU integration uses
[bloodborne-native/bloodborne-recomp](https://github.com/bloodborne-native/bloodborne-recomp),
revision `4b23a301944642f1e4e9e921418d4519dc36c315` (GPL-2.0-or-later),
and Zydis/Zycore for decoding. The bundled recompiler patch extends that
project's code generator and strict execution checks.

[bloodborne-native/bloodborne-runtime](https://github.com/bloodborne-native/bloodborne-runtime),
revision `9f809002d247e663c92e46cc96b17b1056a38415`, supplied reference code
for the portable SRT walker and guest callback bridge. The new
`vk_null_resources.cpp` and `.h` are adapted from that revision, with
additional typed/MSAA resources and explicit limits in the rasterizer.
Their GPL notices and source attribution are retained.

The Vulkan-to-Metal implementation is MoltenVK. The descriptor-set workaround
for runtime array lengths is described in
[MoltenVK PR 2827](https://github.com/KhronosGroup/MoltenVK/pull/2827).
The optional stencil-only dynamic-rendering fix is supplied as a small patch
against KhronosGroup/MoltenVK v1.4.2, revision
`db66022459ffb663aa2b50f6b018bc2e124f5edf`. Its build helper keeps the dependency
local; it does not install or replace a system driver. MoltenVK retains its
upstream Apache-2.0 license and copyright notices.

The archive contains engine/runtime sources and patches, not the game's assets
or any C, binaries or memory records generated from the user's dump.

The opt-in ARM64 video-skip preparation reuses the existing upstream XML patch by
cuesta4, crediting illusion0001/illusionyy's PS3 patch. Its notices remain in
`patches/God_of_War_III_Remastered.xml`; the new preparation code only validates and
applies these definitions to local build intermediates before recompilation.

The separate 120 FPS ARM64 profile reuses the upstream "Frame Rate Patch - 120
FPS" by kvicken, including its swing-energy correction. The XML definition and
attribution remain intact. Native preparation bakes both changes into the boot
image and scanner input before generating C; it does not invent a second timing
patch or change the original 60 FPS profile.
