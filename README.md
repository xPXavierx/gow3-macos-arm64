# God of War III Remastered — macOS Apple Silicon

Experimental native ARM64 fork of [FelipePrado0/gow3_pc](https://github.com/FelipePrado0/gow3_pc), maintained by [xPXavierx](https://github.com/xPXavierx).

The game CPU functions are compiled ahead of time to ARM64. The runtime uses SDL3,
FFmpeg and CoreAudio; the graphics backend translates PS4 GPU commands/shaders
through shadPS4, Vulkan and a locally patched MoltenVK driver. Game execution uses
neither Rosetta nor a CPU JIT/interpreter fallback. Unsupported AOT paths stop with
a diagnostic.

**This is a source release for developers. You must supply and prepare your own
extracted game dump.** The local playable app contains game-derived AOT libraries
and linked images and is not included here. There is no community-ready downloadable
Mac app in this repository yet. No game dump, extracted textures, saves, shader
captures, generated game C or compiled game libraries are committed.

## Current macOS checkpoint

- Tested on an M3 Max with macOS 26. The current private dependency bundle requires
  macOS 26; older systems and other Apple Silicon models are not certified.
- Local AOT preparation and hash validation used CUSA01623 v1.02. Other game
  revisions/regions require their own preparation and gameplay validation.
- Intro, main menu, Load Game and initial combat run in native ARM64.
- Native AppKit/SwiftUI launcher: persistent dump selection, English first launch,
  English/Brazilian Portuguese/Spanish, 720p/1080p/1440p/4K, native or spatial
  MetalFX, 60 FPS or Unlimited. Rounded macOS app icon; straight-edged menu controls.
- Retina fullscreen and Game Mode metadata; FIFO V-Sync and bounded presentation
  queue. The game HUD always reports real render dimensions and MetalFX status.
- Frame generation is experimental, disabled and absent from the launcher options.
- Textures in the tested initial save match the dump after restoring the upstream
  linear-image readback policy. This is not full-campaign visual validation.

A five-minute native 3840×2160 combat test confirmed approximately 60 presentations/s
with direct Metal timestamps. Remaining intervals alternate around 12.5/20.8 ms;
60 FPS average does not imply perfectly even pacing. The notebook panel presents
at 3456×1944 physical pixels. See the [current pacing report](packaging/macos/native/FRAME_PACING.md)
for scope and limits. No campaign-wide 4K/60 guarantee is made.

## Build your own local app

```sh
git clone --recursive --branch macos-arm64 https://github.com/xPXavierx/gow3-macos-arm64.git
cd gow3-macos-arm64
```

Follow these guides in order:

1. [Build the pinned ARM64 AOT tools](packaging/macos/aot/README.md).
2. [Build the native runtime/renderer and prepare your dump](packaging/macos/native/README.md#dependencies-and-builds).
3. [Build the patched MoltenVK driver and configure pacing](packaging/macos/native/FRAME_PACING.md).
4. [Prepare static profiles and bundle the desktop launcher](packaging/macos/launcher/README.md).

The first game preparation/build is a developer workflow and can take substantial
time and disk space. The current launcher chooses among 24 prepared static
profiles; it does not yet generate these on a new player's Mac. Optional custom
launcher artwork is supplied locally via `--branding-directory` and is not in this
fork. Game files stay outside the source tree and bundle; generated AOT artifacts
must stay in ignored/private build directories.

## Contributing and next steps

See [the roadmap](docs/macos-roadmap.md) for community packaging, higher-resolution
texture experiments and GPU/frame-pacing work. Use the existing hardware checks
and compare the same living combat scene. Report actual render/output dimensions,
chip, OS, mode, frame times, GPU timings and visual changes; a menu FPS average
is not a gameplay benchmark. Do not upload saves or game-derived captures/code in
source contributions. Reproduction steps and sanitized numeric reports are useful.

This branch preserves the tested upstream base
`7180a7bafa51afbe991523f6392f69ea1cf03e1e`. Newer upstream changes require a deliberate
merge and regression testing. The upstream Windows/Linux documentation and credits
are retained in [README.upstream.md](README.upstream.md); its platform claims do not
certify the macOS fork. Detailed older macOS investigations remain in
[the development history](docs/macos-arm64.md).

## Credits and license

Thanks to Felipe Prado, deadinside28/bbport, Supermedo, cuesta4, shadPS4,
bloodborne-native/bloodborne-recomp, bloodborne-native/bloodborne-runtime,
Khronos/MoltenVK and the authors of the retained runtime, codec, UI and shader
libraries. [Source bases and adapted-code credits](docs/CREDITS.md) identify the
pinned revisions and retained notices. [GPL-2.0-or-later](LICENSE); third-party
components retain their own licenses. This project is not affiliated with Sony
Interactive Entertainment or Santa Monica Studio.
