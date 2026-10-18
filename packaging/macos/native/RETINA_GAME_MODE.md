# Retina presentation and automatic Game Mode

## Native application identity

The native CMake build produces `gow3-probe-arm64.app` as well as the command-line
loader. Both executables are ARM64-only and link the same strict AOT runtime.
The actual game process lives inside the application bundle; this is not a shell
application that launches an unidentified child. The bundle's stable identifier
is `org.gow3pc.native-arm64`. Its Info.plist declares the games category,
`LSSupportsGameMode`, legacy `GCSupportsGameMode`, and high-resolution capability.

The shared `run.py` chooses the sibling bundled executable for native graphical
launches, validates its ARM64 architecture, and preserves the existing startup,
patch, texture-readback, save-backup and supervisor rules. CPU-only launches and
loader tests retain the command-line executable. Build the normal native CMake
targets, or explicitly build `gow3-native-game` for the graphical host. Continue
launching through `run.py` with the prepared AOT inputs; the bare bundle does not
contain the player's dump or generated game library.

## Fullscreen startup

On macOS the renderer requests a native Cocoa fullscreen Space at every startup,
with SDL's fullscreen-Spaces hint enabled. It uses desktop fullscreen without an
exclusive display-mode switch. This satisfies the OS fullscreen condition for
automatic Game Mode. `GOW3_FULLSCREEN=0` explicitly starts windowed for reference
testing. The port's display menu can also leave fullscreen during a session.

Game Mode requires Apple silicon and macOS 14 or later. The OS controls its
actual state: it can pause when the game loses focus and turns off on exit. If
the player has manually disabled Game Mode for this game, macOS remembers that
choice. Re-enable it in Game Overlay (Command-Escape, Settings on macOS 26) once
to restore automatic activation. The port does not override that system choice
through private APIs or change the machine's power mode.

## Retina correction

The old SDL window did not request a high-density drawable. A window occupying
roughly 3456 display pixels therefore presented only about 1728 image pixels
across. Even a 3840x2160 MetalFX texture was reduced to that small presentation
image before macOS enlarged it. This discarded output detail and made the Metal
HUD's drawable resolution much smaller than the configured rendering resolution.

The macOS window now requests high pixel density by default. Logical Cocoa sizes
remain points; window-size and resize handling use `SDL_GetWindowSizeInPixels`.
Existing overlay input already applies pixel density. `GOW3_RETINA=0` restores
the old low-density drawable only for controlled comparisons.

The startup log reports logical size, drawable size and pixel density. On the
tested Mac, a 1728-point-wide window now has a 3456-pixel drawable (density 2.00).
The game preserves its aspect ratio inside that drawable. The panel's width is
not 3840 pixels: a fixed 4K MetalFX texture still fits the available physical
display pixels. Internal rendering resolution, MetalFX output texture size and
the Metal HUD's drawable size are separate quantities. Resizing presentation
does not rebuild or silently change the profile's fixed game rendering size.

This correction removes the avoidable Retina resolution loss. It does not by
itself eliminate geometric aliasing or temporal shimmer. SMAA/FXAA remain optional
spatial filters, described in [SPATIAL_AA.md](SPATIAL_AA.md). More presentation
pixels can cost GPU time; old low-density FPS results must not be represented as
measurements of the corrected high-density presentation.

## Verification

Both native hosts and the renderer compiled on Apple M3 Max. All five native
runtime/loader/static-AOT CTest checks and nine shared-launcher tests passed.
The bundled game booted and reached the saved combat scene using FXAA Strong,
2560x1440 input and 3840x2160 MetalFX spatial output, without frame generation.

At 2026-10-17 12:32:57 local time, `gamepolicyd` identified the actual game PID
55362 with `labelReason=infoPlist`, granted fullscreen and frontmost status, and
reported `Game mode enabled` followed by `Game mode status is now on`. Thus Game
Mode activation was observed in the system, not inferred from the plist alone.
No desktop screenshots were taken. Game screenshots came from its own renderer.

## Primary references

- [Apple: Game Mode requirements and remembered manual choice](https://support.apple.com/en-lamr/105118)
- [Apple WWDC25: Level up your games, Game Mode opt-in](https://developer.apple.com/videos/play/wwdc2025/209/)
- [SDL: high-DPI coordinates and pixel sizes](https://wiki.libsdl.org/SDL3/README-highdpi)
- [SDL: native macOS fullscreen Spaces](https://wiki.libsdl.org/SDL3/SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES)

## Corrected-presentation performance — bounded run 163

FXAA Strong plus 2560x1440-to-3840x2160 MetalFX spatial output averaged
58.925 FPS during approximately 40 seconds of active gameplay (five-second samples
57.0–60.0), with native fullscreen, Retina 2x and Game Mode active.
Pause averaged 58.150 FPS. There were zero shader compiles, no game captures
or native GPU profiling counters inside either timing window. The average combat
draw count was 1765.5 per frame. The game-only image is
3456x1944 pixels inside the fullscreen drawable, preserving 16:9.

These results do not isolate the cost or benefit of Retina, Game Mode or FXAA,
and do not establish sustained 60 FPS over the campaign. Earlier runs 158–161
used low-density presentation and are historical references, even though their
MetalFX output texture was 4K. Run 162 reached gameplay and displayed Pause but
its English-only recognition check could not accept the Portuguese menu; no FPS
measurement is claimed for that aborted attempt.

The measured gameplay used movement, defense and attack inputs. The final frame
has an occluded player and no visible enemies; this workload is not proof of
sustained performance in heavy combat. The final rebuild additionally makes the
explicit `GOW3_FULLSCREEN=0` reference override choose Windowed even when a saved
configuration requested fullscreen; the measured fullscreen branch is unchanged.
