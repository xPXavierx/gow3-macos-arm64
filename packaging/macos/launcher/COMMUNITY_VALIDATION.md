# Community preview validation — 2026-10-18

Release: 0.4.0 preview 1. Tested on Apple M3 Max, macOS 26.5.2, with the
hash-accepted CUSA01623 v1.02 executable/modules. These results do not certify
other Macs, every dump or the complete campaign.

- **Cold automatic preparation:** 126.078 s, empty application data directory,
  bundled ARM64 Python, scanner, recompiler and Zig 0.15.2/Clang 20.1.2. The entire
  preparation process ran under an OS sandbox denying reads from Xcode,
  `/Library/Developer` and `/opt/homebrew`; no Apple SDK was bundled.
- **Second preparation request:** 0.052 s to check and reuse the native-4K ready
  profile. This excludes game boot, asset loading and graphics shader preparation.
- **Another selected profile:** 4K output with MetalFX Quality (2560×1440 input)
  prepared in 28.965 s using the same incremental object cache.
- **Relocation/read-only bundle:** extracted the actual release ZIP into a path
  with spaces and accented characters, removed write permissions from the app,
  checked its signature, reused the Quality profile in 0.034 s and generated a
  new 720p native profile in 26.005 s, with the same developer-directory read bans.
- **Original checkpoint parity:** cold discovery reproduced all 16,042 function
  candidates exactly. The native-4K patched loader image SHA-256 remained
  `1e7fc75649e7d4a1c14916d0e63a266e22859553f3ac0337e7aea24ea6f588ca`,
  identical to the previous working local profile.
- **CPU compiler semantics:** 953 synthetic instruction cases passed the existing
  native differential check, with zero failures, crashes or skipped cases. Its
  reference interpreter is a test oracle; actual game CPU fallbacks remain off.
- **Game regression:** the first cold-generated library booted, reached the main
  menu, loaded an isolated copy of the initial AUTOSAVE, reached Pause and ran a
  roughly 40-second sequence beginning in combat and ending in climbing. No
  native fault was recorded. Texture appearance and native-4K/MetalFX-off HUD
  were inspected in the game's own render captures. This was a packaging/boot
  regression check, not a new optimization or sustained heavy-combat benchmark.
  Shutdown logged zero blocks translated and zero instructions interpreted from
  translated code.
- **Actual GUI flow:** empty preferences opened in English with the dump picker.
  Choosing the dump and Play showed live stage/progress; Cancel stopped compiler
  processes; Play resumed completed work and automatically opened the native game.
  With a fresh save folder the game's brightness calibration appeared normally;
  Space confirmed Cross/X. The Metal HUD showed Game Mode on while frontmost.
- **Failure UX:** a synthetic incompatible executable was rejected before source
  generation. The actual GUI displayed the localized compatibility explanation
  naming CUSA01623 v1.02. Four preparation/cache/cancellation contract tests and
  nine existing shared startup tests passed. The Swift self-test checked all
  three translation key sets, accented fonts, English first-use defaults, saved
  settings/bookmarks, missing folders, dimensions and Retina/Game Mode metadata.
- **Package audit:** 19,651 files checked, 107 Mach-O files, all thin ARM64 and all
  non-system runtime bindings inside the app. No dump, prepared image, generated
  game library, user preferences/saves or Python bytecode was present. Deep/strict
  code-signature integrity passed before and after relocation.

The ZIP is approximately 131 MiB (about 506 MiB extracted). It contains the same
launcher art/icon/font/menu style as the local app. Initial saves, game-derived
captures and all prepared game code used above remain private and are excluded.

## Remaining limits

The package is ad-hoc signed and **not Apple-notarized**. A Developer ID identity
was unavailable on the test machine. Other-Mac testing, download/quarantine
approval on a separate clean macOS account, complete campaign coverage, other
game revisions and long-run behavior remain unverified. See COMMUNITY.md for
installation, compatibility and the signing limitation.
