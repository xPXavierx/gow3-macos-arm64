# macOS status and remaining work

The supported product is the Apple Silicon application and its offline first-run
ARM64 compiler. See [installation](../packaging/macos/launcher/COMMUNITY.md) and
[maintainer build instructions](../packaging/macos/native/README.md).

The tested dump is CUSA01623, v1.02, identified by the hashes checked by the app.
Other game versions, the full campaign and other Mac models remain unverified.
The public download contains no game dump, save or game-derived AOT library.

## Stable configuration

Native ARM64, Retina, native or MetalFX spatial rendering, V-Sync, correct original
texture readbacks and frame generation off. At a 60 FPS cap the launcher temporarily
requests a matching 60 Hz display mode and restores the prior mode after exit.
Unlimited rendering and experimental frame generation do not request that change.

## Current research

Measure GPU material/screen passes and actual presentation intervals independently.
Keep captures and profiling outside clean combat timing windows. Record render and
output sizes, shader compilations, real frames and generated frames separately.
A high average FPS alone does not demonstrate regular pacing or low input latency.
MetalFX frame interpolation is experimental and disabled in the public launcher.
It must preserve real-frame throughput and pass motion/HUD/scene-transition checks
before becoming a normal launcher option.

The optional texture replacement experiment was discontinued on 2026-10-18.
Its local generated packs and test application were deleted. No HD texture pack
ships with the app. The source mechanism remains available for development; its
[format reference](macos-texture-replacements.md) describes an inactive experiment,
not an available player feature.

## Distribution and compatibility

Developer ID signing/notarization, broader Mac testing and additional verified dump
versions remain pending. Preserve upstream attribution and license notices.
Windows/Linux guides, UltraWide patches and unintegrated DLSS/FSR dependencies
were removed from this Mac distribution; shared renderer/runtime code remains.
