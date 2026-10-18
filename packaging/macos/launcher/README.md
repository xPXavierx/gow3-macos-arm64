# Native macOS desktop launcher

For the community first-run app, see [COMMUNITY.md](COMMUNITY.md). It uses this same
launcher and prepares game code locally instead of bundling private AOT profiles.
The private prebuilt-profile recipe below remains available for development.

The AppKit/SwiftUI launcher uses the existing `run.py` startup policy. It does not
reimplement texture readback, patch selection, save backup or strict ARM64 checks.
The new `session.py` adapter supplies paths and settings, then starts `run.py` with
a bundled ARM64 Python. No Homebrew, terminal command or build tool is needed to
run the resulting local app. The supplied dependencies target **macOS 26**; the
Swift UI's lower deployment target does not establish support for older systems.

The UI offers English, Brazilian Portuguese and Spanish; fixed 720p, 1080p,
1440p or 2160p output; native rendering or spatial MetalFX Quality, Balanced or
Performance; 60 FPS or Unlimited; and the Metal HUD. Frame generation stays off.
The in-game HUD always includes actual game-image input dimensions and applied
MetalFX status/preset, published atomically by the renderer before window fitting.
It remains visible when the optional Apple Metal HUD is off. English, Brazilian
Portuguese and Spanish labels follow the launcher language.
V-Sync, Retina, fullscreen and the two-image swapchain remain enabled. The default
60 FPS mode retains the checkpoint's minimum presentation duration.

The menu uses straight edges and a black, blood-red, aged-gold and ivory palette.
The application icon uses an inset rounded square with transparent corners and a
subtle shadow, rendered by the build-time CoreGraphics helper `Icon.swift`.
This does not round the launcher's option rows or buttons. English is the default
when no language has been saved; subsequent launches retain the selected language
and all other settings. An update preserves existing Portuguese/Spanish choices.
The title font is registered with CoreText for this process only. Menu labels and
native popup items use macOS Copperplate Bold, with accented characters intact.
The game's localized `R_LANG_EN.WAD` names its bitmap atlas `TXR_Copperplate_en`
and its font metadata `SMF2Copperplate_en`: this establishes the family, not a
byte-identical outline font/weight match. No game atlas is copied into the app.
The optional local artwork is read from the sealed bundle, never the Desktop.

Preferences, the dump URL/bookmark, logs, caches and saves live in
`~/Library/Application Support/God of War III ARM64`. The dump remains external.
A normal launch never writes into the app bundle. The first folder picker accepts
the dump itself or a containing folder up to two levels above it; cancellation
preserves the previous choice. Missing folders are checked again before Play.
Executable/module hashes are checked before boot. This local package supports the
same executable used for its AOT preparation, not arbitrary game updates/mods.

## Build

1. Build the native host, renderer and the patched MoltenVK driver as documented in
   `../native/README.md` and `../native/FRAME_PACING.md`.
2. Prepare the baseline game locally using `../native/prepare_game.py`. Keep the
   static function inventory and original dump available. Apply the pinned,
   bundled CPU patch to a clean recompiler source tree, and build the AOT tool.
3. Run `build_profiles.py --game DUMP --prepared BASELINE --recompiler AOT_TOOL
   --recomp-source CPU_SOURCE --decoder-include ZYDIS_INCLUDE --out PRIVATE_PROFILES`.
   This builds 24 thin ARM64 libraries covering all 32 UI combinations. Only
   resolution immediates/data and the upstream frame-rate patches vary; the
   function inventory/control flow stays fixed. Unknown generated polling-hook
   spellings are rejected; the checkpoint's original generated-code policy is
   preserved. Unchanged generated source reuses an object keyed by source,
   headers, compiler and flags. The original dump is read-only. Add repeatable
   `--reuse VALIDATED_PREPARED_TREE` to retain existing checkpoint libraries,
   but only after the complete prepared loader image matches byte for byte.
4. Compile `Launcher.swift` using `swiftc -O -target arm64-apple-macos14.0` and the
   macOS SDK, then call `build_app.py --launcher EXE --host-app HOST.app --renderer
   GPU_DYLIB --moltenvk DRIVER_DYLIB --python-prefix PYTHON_FRAMEWORK_VERSION
   --profiles PRIVATE_PROFILES --out DESTINATION.app`. Add `--branding-directory
   LOCAL_BRANDING` to incorporate user-supplied `GOW-3-Icon.jpg` (square),
   `GOW-3-Art.jpg` (wide), and `GODOFWAR.TTF`. Keep those private inputs outside
   the source checkout. Without that option the builder uses the repository's
   existing icon/title font and a black background. Both the launcher and nested
   game bundle receive the icon at sizes up to 1024 pixels. The current recipe expects
   the Python 3.14 framework layout. It bundles the real Python executable, not
   the framework's path-dependent launcher stub, and excludes Homebrew's
   `sitecustomize.py` and installed packages.

The builder copies and rewrites the complete transitive Mach-O dependency graph
using loader-relative paths, strips non-ARM64 slices, writes a relative Vulkan
ICD, and seals nested code and the top-level bundle with local ad-hoc signatures.
The actual game process retains its own app identity, Retina/Game Mode metadata
and native fullscreen Space. The OS controls Game Mode, including remembered
manual opt-out: <https://support.apple.com/en-us/105118>.

## Frame limit semantics

60 selects the upstream 60 Hz game profile, 60 FPS present limit and the validated
minimum-duration pacing path. Unlimited selects the upstream 240 FPS engine/swing
energy patch, removes the 60 FPS present limit/minimum duration, and keeps FIFO
V-Sync. It is consequently bounded by the display and the engine's 240 FPS safety
ceiling. It is not frame generation. Do not remove the presentation cap from an
unpatched 60 Hz image: doing so can change the game's speed. The higher-rate mode
requires its own simulation-time/gameplay validation; neither the UI label nor a
high FPS counter proves correct physics throughout the campaign.

## Verification and local-only output

`GOW3Launcher --self-test REPORT.json` checks translation coverage, settings and
bookmark persistence, nested folder discovery, missing folders and UI resolution
labels, plus font registration and accented menu glyphs. `GOW3Launcher --preview
PREVIEW.png --support ISOLATED_STATE` renders only its own launcher content for
visual review; it does not capture the desktop or start the game. Shared
native-launch tests also check that custom baked resolutions do
not omit the original texture/readback/save policies. Validate every prepared
profile's ARM64 slice, image seal and code-patch writes before packaging, and test
real game startup from a relocated bundle with a minimal environment.

**Do not put the built app, Profiles or generated C/libraries in a public source
archive.** The local app contains game-derived executable images and AOT code,
although the full dump/assets stay external. It is a private build for this
user's supplied dump. This launcher source, build recipes and port patches can
be included in the source-only archive separately.
