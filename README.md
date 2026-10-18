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
are not qualified. Tested on M3 Max; other Macs and the entire campaign remain
unverified. Initial preparation needs at least 3 GB free space.

The downloadable app includes its offline compiler, Python and native libraries.
Players do not need Xcode, Homebrew, Terminal, Rosetta or a preparation-time
internet connection. The app is ad-hoc signed and is not Apple-notarized; see the
[installation and compatibility guide](packaging/macos/launcher/COMMUNITY.md).

## Implemented in this port

- ARM64 CPU functions compiled ahead of execution; no Rosetta or CPU JIT/interpreter
  fallback. Unsupported instructions stop with a specific diagnostic.
- Native launcher in English, Brazilian Portuguese and Spanish. English on first
  use; the selected dump, language and display preferences persist.
- 720p, 1080p, 1440p and 4K output; native rendering or spatial MetalFX Quality,
  Balanced and Performance. The HUD always shows render dimensions and MetalFX.
- 60 FPS or Unlimited, V-Sync, Retina fullscreen and automatic Game Mode eligibility.
  With the 60 FPS cap and no frame generation, the launcher temporarily selects
  60 Hz without changing resolution or Retina scale, then restores the previous
  display configuration. Unlimited and frame generation do not request 60 Hz.
- SDL3 input, FFmpeg/ATRAC9 audio and video, save backup and original texture
  readback policies. Graphics use the shadPS4-derived Vulkan backend and MoltenVK.

The M3 Max has reached approximately 60 FPS in bounded native 4K combat tests.
This is not a campaign-wide guarantee. MetalFX frame generation is a developer
experiment, disabled in the public launcher; rendered and presented FPS must be
measured separately. See [frame interpolation](packaging/macos/native/FRAME_INTERPOLATION.md)
and [presentation pacing](packaging/macos/native/FRAME_PACING.md).

The higher-resolution texture experiment was discontinued. No replacement pack
is distributed. Windows/Linux launchers, packaging and unsupported ultrawide
instructions are not part of this port.

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
