# Community release 0.4.21 validation

Tested on 1 November 2026, Apple M3 Max / macOS 26.5.2, with the hash-verified
CUSA01623 v1.02 dump. Other Macs and the whole campaign remain unqualified.

## Source and package checks

- Five selected backport checkpoints, plus release documentation/versioning;
  [upstream integration status](../../../docs/upstream-status.md).
- 11 native runtime/loader/AOT tests and 5 graphics checks, including actual
  MoltenVK roundtrips and 60,000 concurrent label publications/recycles.
- 34 preparation/display/catalogue tests pass. The remote catalogue validator
  [passed](https://github.com/xPXavierx/gow3-macos-arm64/actions/runs/41332584068).
- 109 thin ARM64 Mach-O files; non-system dependencies bundled. No dump, saves,
  prepared game library or generated game code is included.
- Launcher self-test covers 106 translated keys, English first use, persisted
  dump/settings/language, Retina/Game Mode and safe shader-cache clearing.
- Official catalogue download/signature verification passed with the existing
  public key. The portable seed has 1,300 shader and 962 pipeline descriptions.

## Actual first run

A fresh support directory and compiler/object cache were used. CPU preparation
completed offline in **153.63 seconds**, with reads of Xcode, /Library/Developer
and /opt/homebrew denied. The sealed profile was then revalidated using its
Mach-O header under the same restrictions; reuse took about **0.03 seconds**.
The CPU preparation recipe is unchanged from 0.4.20, so compatible existing
profiles can be reused. No player Xcode/Homebrew installation is required.

The packaged game loaded all **16,042 locally generated functions**, with zero
CPU JIT translations/interpreted fallback. The test used the same newly generated
library, copied saves and an initially empty port shader cache. Native 4K with
cap 60, movement and attacks completed without fatal errors or omitted draws.
The prior display mode was restored after the session.

The one-minute observation measured **59.81 FPS**, p95 **16.92 ms**, p99 **17.07 ms**
and maximum **202.59 ms**, with two intervals above 33.5 ms. Loading, resource
indexing and pipeline warming precede this measurement. **This is not a claim of
zero stutter**; the longest cold-run interval was not isolated to a cause. The
initial 959-pipeline warm-up took 76.63 seconds on this run. The system Metal
driver cache was not cleared.

The separately controlled native 4K uncapped backport comparison was
82.81 -> 82.80 FPS; no FPS gain is claimed. Three-minute private autosave and
MetalFX checks are described in the linked upstream status. This is a correctness
release, with further shader coverage, longer boss fights and transitions still
pending. Frame generation stays disabled; all execution is ARM64, without Rosetta.

Source/audit data: [community-validation.json](community-validation.json). The
distribution retains ad-hoc signatures; Apple notarization and a separate clean
account download/quarantine test were not performed.
