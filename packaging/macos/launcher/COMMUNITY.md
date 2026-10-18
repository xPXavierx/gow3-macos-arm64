# Community app — first-run ARM64 preparation

The community preview uses the **same native launcher, icon, artwork, fonts and
settings** as the local build. Only the preparation backend changes. It includes
no dump, prepared guest image, generated game C, game AOT library, saves or shader
cache. Those are generated only on the player's Mac from their selected dump.

## For players

1. Download the **macOS ARM64 community ZIP** from
   [Releases](https://github.com/xPXavierx/gow3-macos-arm64/releases).
2. Extract it, move `God of War III.app` to Applications and open it.
3. Select your **extracted, plaintext dump folder**, containing `eboot.bin`,
   `sce_module`, `sce_sys` and `EXEC`. PKG/ISO extraction/decryption is not included.
4. Choose settings and click **Play**. The first preparation runs automatically,
   displays its progress and starts the game when ready. Leave several minutes
   for the first compilation. No Terminal, Xcode, Command Line Tools, Homebrew,
   separate Python installation, Rosetta or preparation-time internet is required.
5. Future launches reuse the locally prepared result. Changing internal render
   resolution or engine cap can prepare another profile; matching compiled objects
   are reused. Language, dump folder and all settings persist.

**Requirements:** Apple Silicon, **macOS 26 or later**, compatible extracted dump,
plus at least **3 GB free on the Mac** for initial preparation. Only M3 Max has
been tested so far; other chips and the complete campaign remain unverified.
Start with 1080p on slower GPUs. 4K output does not imply a 4K physical display.

**Game version tested:** God of War III Remastered, **CUSA01623 v1.02**.
The app checks SHA-256 of `eboot.bin`, `libc.prx` and `libSceFios2.prx` before any
compilation or boot. The accepted executable is:

```text
d85c8135d330c3b601bf5dc3f1dd86bd6119fb8a9fce372bed2c9785f9a79299
```

The tested extracted SFO reports `APP_VER=01.00`; the executable hash is the
compatibility authority. Other regions, executable updates/mods or module
revisions are rejected with an explanation. The app does not convert an
unsupported revision into the tested revision. It does not modify the dump.

**Signing:** this initial preview has local ad-hoc signatures. It is **not
Developer ID-signed or notarized by Apple**. macOS may require approval through
System Settings → Privacy & Security → Open Anyway after the first opening
attempt. Only approve a trusted download; do not disable Gatekeeper.
[Apple's explanation](https://support.apple.com/en-us/102445).
An ordinary Developer ID/notarized release is still pending a maintainer's signing
certificate/account; no such identity is installed on the test machine.

The game may ask to confirm brightness on a fresh save folder. With a keyboard,
**Space** is Cross/Confirm (the on-screen X); Enter is Options. A connected
controller uses its normal south face button. Existing saves are not bundled.

## Settings and data

- English on a fresh install; English, Brazilian Portuguese and Spanish thereafter.
- 720p/1080p/1440p/4K, native or spatial MetalFX Quality/Balanced/Performance.
- 60 FPS or Unlimited (upstream 240 FPS engine profile plus FIFO V-Sync).
- Retina fullscreen, Game Mode metadata and V-Sync stay enabled. macOS controls
  actual Game Mode, including remembered manual opt-out.
- No frame generation. The game HUD always displays render dimensions and MetalFX
  status/preset, even with the optional Apple Metal HUD disabled.

Preferences, saves, caches, logs and generated code are stored outside the app at:

```text
~/Library/Application Support/God of War III ARM64
```

Cancel stops preparation and its compiler subprocesses. Play retries safely,
reusing completed objects. A failed/incomplete profile is not launched. Updating
or moving the app preserves settings and saves; a changed preparation recipe gets
its own cache. The launcher can run from a read-only app bundle.

Use **Open logs** for the session log. Preparation logs are under
`Prepared/<recipe>/<dump>/Base` and `Profiles/<profile>` inside the same data
folder. An incompatible dump, missing files, low disk space or failed preparation
gets a localized message. Do not upload game files, generated C/libraries, saves
or full raw logs in public issue reports; include the error text and sanitized
hardware/OS/profile details instead.

## Packaging from source (maintainers only)

Build the pinned runtime, renderer, patched MoltenVK and native scanner/recompiler
using the existing macOS guides. The player does **not** perform these steps.
Use a clean recompiler checkout plus the retained CPU patch, not experimental local
changes. The tested native tools use Zydis 4.1.1 and Zycore 1.5.2.

1. Compile `Launcher.swift` as usual. Create a relocatable runtime staging bundle
   with `build_app.py --runtime-only` instead of `--profiles`; the standard assets in `launcher/assets` and existing title font reproduce the
   same local-app design without private Desktop paths. This stage contains no generated game profiles.
2. Obtain the official **Zig 0.15.2 aarch64-macos** toolchain:
   `https://ziglang.org/download/0.15.2/zig-aarch64-macos-0.15.2.tar.xz`.
   Verify archive SHA-256
   `3cc2bab367e185cdfb27501c4b30b1b0653c28d9f73df8dc91488e66ece5fa6b`
   before extracting. The compiler, headers and linker are bundled, not downloaded
   at first game launch. Apple SDK files are not redistributed.
3. Package the community app:

```sh
python3 packaging/macos/launcher/build_community.py \
  --runtime-app RUNTIME_STAGING.app --launcher NATIVE_LAUNCHER \
  --tools AOT_TOOLS --recomp-source PATCHED_RECOMP_SOURCE \
  --decoder-include ZYDIS_ZYCORE_INCLUDE --decoder-source ZYDIS_SOURCE \
  --zig EXTRACTED_ZIG_0_15_2 --out 'dist/God of War III.app'
```

The builder excludes private Profiles, checks ARM64 tools/system dependencies,
adds pinned toolchain/recompiler sources/licenses, generates all resolution patch
presets, seals a preparation recipe and rejects known game-derived/private file
names. Keep the exact tagged source and dependency patches in the repository; see
[developer sources and patches](DEVELOPMENT.md).
The stock builder emits ad-hoc signatures, not notarization.

## Preparation integrity and execution model

`session.py` keeps one session lock across preparation and play, validates the
selected dump and invokes `community.ensure_profile`. The scanner constructs the
module link/image/function inventory directly from this player's files. The
selected resolution/FPS patches are applied before C generation. O2 compilation
retains FP contraction off, strict aliasing off and the checkpoint's no-poll-hook
policy. The resulting thin ARM64 dylib is locally signed and checked.

Cached objects and ready profiles are content-verified. A complete marker is
committed only after generation, compilation, linking, signing and another dump
check all succeed. Recipe changes invalidate incompatible generated code. First
launch of each selected profile compiles ahead of game execution; during gameplay
`BB_JIT=0` and the strict native runtime reject CPU fallback. PS4 GPU commands and
shaders still go through the Vulkan/MoltenVK graphics compatibility backend.

The UI calls the **same shared `run.py`** as the private launcher, preserving the
original texture readback, initialization, patch validation, save backup and GPU
optimization policies. No reduced texture quality or renderer rewrite is introduced
by community packaging. Full-campaign correctness and other-Mac performance are
not established by the bounded first-run/boot tests.
