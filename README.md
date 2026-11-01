# God of War III Remastered — macOS Apple Silicon

[Português brasileiro](README.pt-BR.md) · [Español](README.es.md)

Native ARM64 port maintained by [xPXavierx](https://github.com/xPXavierx), based on
[FelipePrado0/gow3_pc](https://github.com/FelipePrado0/gow3_pc), bbport and shadPS4.
This repository contains the macOS application and the source needed to build it.

## Download and play

1. Download the ARM64 application from [Releases](https://github.com/xPXavierx/gow3-macos-arm64/releases).
2. Open it and select your own compatible **extracted game dump**.
3. Click Play. The app compiles the selected game profile to ARM64 on first use
   and reuses it afterward. Keep the dump available at its selected location.

**Requirements:** Apple Silicon, macOS 26 or later and your own CUSA01623 v1.02
dump. The tested extracted metadata reports APP_VER 01.00; executable and module
hashes determine compatibility. Other regions, updates and modified executables
are not qualified. If only the executable differs, the launcher offers **Continue anyway**
after an explicit compatibility warning; approval survives app updates. Region/module
checks remain strict (see [COMMUNITY.md](packaging/macos/launcher/COMMUNITY.md)). Tested on M3 Max; other Macs and the entire campaign remain
unverified. Initial preparation needs at least 3 GB free space.

The downloadable app includes its offline compiler, Python and native libraries.
Players do not need Xcode, Homebrew, Terminal, Rosetta or a preparation-time
internet connection. The app is ad-hoc signed and is not Apple-notarized; see the
[installation and compatibility guide](packaging/macos/launcher/COMMUNITY.md).

## Community shaders

The updated launcher has a **Community shaders** panel with two separate actions:

- **Download and prepare:** click Play for the daily automatic update check, or
  **Update official catalogue** to check now. Downloads require no GitHub account.
  Updating only downloads/verifies; compatible shaders and pipelines are prepared
  from your own dump when you start the game, then cached on your Mac.
- **Share discoveries:** play, return to the launcher and choose **Contribute on
  GitHub**. Finder selects the generated text file; sign in, drag it into the
  official form and submit. Opening the browser alone does not send it.

Contributions are validated and reviewed before entering the signed official
catalogue. No dumps, saves, logs or another player's compiled GPU cache are shared.
Coverage grows across campaign areas; it does not yet guarantee zero stutter.

**Available since release 0.4.20.** Download the updated application from Releases.
The official catalogue service is online.
See the [player guide](docs/community-shaders.md) for every step and the difference
between updating, compiling and clearing the cache.

## Implemented in this port

- ARM64 CPU functions compiled ahead of execution; no Rosetta or CPU JIT/interpreter
  fallback. Unsupported instructions stop with a specific diagnostic.
- Native launcher in English, Brazilian Portuguese and Spanish. English on first
  use; the selected dump, language and display preferences persist.
- 720p, 1080p, 1440p and 4K output; native rendering or spatial MetalFX Quality,
  Balanced and Performance. When enabled, the HUD shows render dimensions and MetalFX. The launcher HUD
  switch hides both diagnostic overlays when off.
- Known graphics pipelines are prepared before play. First use waits for missing
  shaders instead of omitting draws; unseen variants can still cause a pause.
- Clear shader cache from the launcher while preserving saves, settings and ARM64
  preparation. The action is blocked during game sessions.
- 60 FPS or Unlimited, V-Sync, Retina fullscreen and automatic Game Mode eligibility.
  With the 60 FPS cap, the launcher temporarily selects
  60 Hz without changing resolution or Retina scale, then restores the previous
  display configuration. Unlimited does not request 60 Hz.
- SDL3 input, FFmpeg/ATRAC9 audio and video, save backup and original texture
  readback policies. Graphics use the shadPS4-derived Vulkan backend and MoltenVK.

CPU AOT and graphics preparation are separate. The graphics backend converts
PS4 shaders to SPIR-V and MoltenVK/Metal at runtime. Known pipelines are warmed
before play; this is not a complete offline Metal shader catalog, and first-use
GPU compilation can still cause stutter.

The M3 Max has reached approximately 60 FPS in bounded native 4K combat tests.
This is not a campaign-wide guarantee. Frame generation is excluded from the current build and launcher. Spatial MetalFX
upscaling remains available. See [presentation pacing](packaging/macos/native/FRAME_PACING.md).

The higher-resolution texture experiment was discontinued. No replacement pack
is distributed. Windows/Linux launchers, packaging and unsupported ultrawide
instructions are not part of this port.

## Upstream updates

This fork incorporates selected SpartanPort fixes while retaining the native ARM64 runtime and macOS launcher. Release 0.4.21 includes the memory, mutex, texture-alias, readback, shader-input, registry and fence-publication corrections validated locally. These are adapted backports: GitHub's “commits behind” count tracks ancestry and does not decrease simply because equivalent fixes were ported.

See the [integration status and remaining work](docs/upstream-status.md), including the two newer upstream commits reviewed on 1 November 2026.

## Build and contribute

```sh
git clone --recursive --branch macos-arm64 https://github.com/xPXavierx/gow3-macos-arm64.git
cd gow3-macos-arm64
```

Maintainer guides:

1. [Pinned ARM64 AOT tools](packaging/macos/aot/README.md).
2. [Runtime, renderer and dependencies](packaging/macos/native/README.md#dependencies-and-builds).
3. [Patched MoltenVK](packaging/macos/native/FRAME_PACING.md).
4. [Community application packaging](packaging/macos/launcher/COMMUNITY.md#packaging-from-source-maintainers-only).

`./build.sh --help` lists native build targets. The shared runtime and graphics
sources retain upstream code required by these targets; their internal platform
branches are not a promise of support for another platform. Original upstream
source remains available in Git history and at its credited repositories.

[Report issues here](https://github.com/xPXavierx/gow3-macos-arm64/issues), including
Mac/chip, macOS and app version, selected resolution/MetalFX/frame cap, reproduction
steps and the session log available through the launcher's **Open logs** button.
Do not upload game dumps, saves, extracted textures, generated game C/libraries or
memory captures. Logs can contain local paths; review them before sharing.

## Credits and license

See [source bases, pinned revisions and credits](docs/CREDITS.md). Thanks to
Felipe Prado, deadinside28/bbport, Supermedo, cuesta4, shadPS4, bloodborne-native,
Khronos/MoltenVK and the authors of the retained runtime, codec and shader libraries.
[GPL-2.0-or-later](LICENSE); third-party components retain their own licenses.
This project is not affiliated with Sony Interactive Entertainment or Santa Monica
Studio. No game assets, dump, saves or prepared game code are distributed.
