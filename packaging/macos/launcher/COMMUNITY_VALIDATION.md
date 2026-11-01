# Community release 0.4.22 validation

Tested on 1 November 2026, Apple M3 Max / macOS 26.5.2. This is a launcher and
executable-approval update. The renderer, native game host and graphics catalogue
are unchanged from 0.4.21; no new FPS gain or stutter reduction is claimed.

## Consent and source checks

- Original [danielgpinheiro PR #7](https://github.com/xPXavierx/gow3-macos-arm64/pull/7)
  commit retained, with the tested launcher confirmation built on top.
- 31 Python tests pass: 10 preparation/consent, 3 launcher shader-catalogue,
  10 portable catalogue and 8 catalogue-service tests.
- The launcher compiles for ARM64. Its AppKit self-test covers 114 translated keys
  in English, Brazilian Portuguese and Spanish, explicit Cancel / Continue anyway
  buttons, persisted settings and shader-cache clearing that preserves approvals.
- The production Swift consent bridge invoked the bundled isolated Python and
  saved/reused approval from a disposable support directory. The reference dump
  still passes without an override; wrong title/module hashes remain blocked.
- Two app-resource directories with different preparation recipes reused the same
  support-folder approval without asking again. Changed hashes require a fresh
  decision, and the save operation rejects a stale digest or busy session.

## Fresh automatic preparation

A disposable copy of the owner dump was used. Bytes appended outside the SELF's
mapped payload changed its raw SHA-256 while its extracted ELF remained exactly
identical. The original dump was not modified. This isolates the approval path;
it does not certify the contributor's actual alternate retail executable.

The unapproved executable was initially refused. After explicit test approval,
a fresh preparation/object cache generated **37 C shards** with
**16,042 functions** and linked a thin ARM64 game library in
**153.76 seconds**. Preparation ran offline with Xcode,
/Library/Developer and /opt/homebrew reads denied. Sealed-profile reuse took
**0.03 seconds**; the approval file remained unchanged.

The recipe changes in this release, so the first Play may rebuild a profile.
Approval is kept outside the app and independent of that recipe: future updates
must not require authorizing the same executable again. Other executable hashes
still need their own approval. Existing saves/settings remain outside the app.

## Distribution and scope

- 109 thin ARM64 Mach-O files, all non-system dependencies bundled; Retina and
  fullscreen Game Mode metadata retained, frame generation disabled.
- No dump, saves, acceptance file, generated guest C or prepared game library is
  bundled. Only the compiler/tool sources and portable graphics descriptions ship.
- Existing signed catalogue: 1,300 shader and 962 pipeline descriptions, partial
  campaign coverage. Its signature/public key and the renderer ABI are unchanged.
- Ad-hoc signature integrity checked; Apple notarization and a separate clean
  account download/quarantine flow were not performed.

The contributor's actual executable was unavailable for an independent gameplay
test. There is no new gameplay/FPS benchmark for 0.4.22. Prior engine tests and
bounded native 4K gameplay measurements, including remaining cold-use pauses,
are recorded in the [0.4.21 validation](https://github.com/xPXavierx/gow3-macos-arm64/blob/v0.4.21/packaging/macos/launcher/COMMUNITY_VALIDATION.md).
Other Macs, long boss fights and the full campaign remain unqualified.

Machine-readable results: [community-validation.json](community-validation.json).
