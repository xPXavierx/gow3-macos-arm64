# Upstream integration status

Reviewed on 1 November 2026 against SpartanPort [`c459438`](https://github.com/FelipePrado0/SpartanPort/commit/c4594385ba8cd4f02285b0ad1f434ba64bf78771).
The original audit used `a2299cd`; the common ancestor is `7180a7b`.

## What “commits behind” means

GitHub compares commit ancestry. This Mac port adapts selected changes into new
commits because the CPU runtime, launcher, presentation and shader preparation
differ from the Windows/Linux project. Backported code does not make its original
upstream commit an ancestor. The counter therefore remains even when the
corresponding correction is present. It is not a count of missing bug fixes.

The audit found 557 upstream-only commits initially; two more appeared before
this release. Of the initial history, 404 commits touched the upstream launcher
(often alongside other files). The audit grouped changes into 26 findings; it did
not test every commit or treat every new platform/feature as a Mac requirement.
No merge that marks the entire upstream history integrated is included in 0.4.21.

## Corrections included in 0.4.21

| Audit | Contract ported | Upstream reference | Mac checkpoint |
|---|---|---|---|
| A01 | Preserve physical offsets when flexible mappings split | `ed3af5b7dd66` | `9d38b66` |
| A02 | Keep guest mutex flags outside the Darwin pthread object | `9e8a080bb757` | `9d38b66` |
| A03 | Retire old GPU mappings before fixed-map replacement | `e650a33d7529` | `9d38b66` |
| A04 | Copy the useful intersection of compatible image aliases | `9ca82cb552c8` | `9d38b66` |
| A05 | Exact image readback waits/cancellation; retain CPU page rules | `b776be748eac` (readback portion) | `f9f8b0f` |
| A06 | Protected backing uploads plus required snapshot dependencies | `63d437e373cc` | `f9f8b0f` |
| A07 | Release publication of completion labels and safe backing failure | `6f5aa50c7640` + Mac adaptation | `2e7778d` |
| A08 | Reject empty input at the shader compiler boundary | `368d89910ddb` | `78eaf14` |
| A09 | Keep active lock/semaphore/thread records near the list head | `201a00f0cf5f` | `7503594` |

The Mac adaptations retain 16 KiB tracking, native TLS/handles, complete required
draws, the original deferred/stale readback policy and the signed community
catalogue. Shader keys/ABI and the CPU AOT preparation recipe are unchanged.
Frame generation remains disabled. Detailed contracts and provenance:
[memory/alias integrity](upstream-integrity-2026-11-01.txt),
[readback/uploads](readback-2026-11-01.txt),
[shader boundary](shader-entry-2026-11-01.txt),
[registry lookup](registry-2026-11-01.txt),
[fence publication](fence-publication-2026-11-01.txt).

## Reviewed changes still outside this release

| Finding/change | Disposition |
|---|---|
| A10 shader/pipeline caches | Large overlap with the Mac preparation/catalogue implementation; review missing pieces individually. Do not replace the signed portable catalogue with device binaries. |
| A11 low-address handles | Existing Darwin equivalent retained. |
| A12 same-timestamp legacy save backups | Reproduced in the legacy script; separate correction pending. The launcher also has its own backup flow. |
| A13 minimize/surface-loss recovery | Requires adaptation and Mac window/presentation tests. |
| A14 FPU state | Upstream x86 implementation does not configure ARM64; native policy retained. |
| A15 worker priorities | Windows/Linux motivation; no corresponding M3 Max gain established. |
| A16 pool-allocation diagnostics | Darwin-specific review pending. |
| A17 high-FPS QTE/Cronos hooks | Relevant logic; x86 hooks require native adaptation. |
| A18 half-resolution post-processing patch | Conflicts with the Mac's physical render-size scaling; not imported. |
| A19 controller-speaker downmix gain | Separate audio correction pending. |
| A20–A21 FSR/DLSS/framegen/temporal composition | Outside the selected feature set. |
| A22 Rust launcher/branding/trophies | Existing Swift launcher and current controls retained. |
| A23 mutex destruction | Darwin normal/recursive/error-check lifetime cases tested with A02; no winpthreads counters imported without need. |
| A24 new menus/cheats/bindings | Outside the selected feature set; Mac shortcuts remain 0–4. |
| A25 first-draw capture resources/diagnostics | Tooling correction remains separate from game rendering. |
| A26 Windows/Linux packaging/backends | Not directly applicable to the native macOS runtime. |
| `6f43d655` fullscreen/taskbar | Windows foreground/taskbar fix reviewed; no Win32 calls imported into the Mac launcher. |
| `c4594385` oversized tiling/detiling dispatches | Relevant shared Vulkan issue. The Mac currently uses this path; adapting and testing batching against MoltenVK device limits is pending, separate from the qualified memory/synchronization batch. |

## Validation and limits

Eleven native runtime/loader/AOT tests and five graphics checks pass, including
60,000 concurrent label publication/recycling cycles, protected guest views,
aliases, readback dependencies and real MoltenVK roundtrips. All executed packaged
code is ARM64; no Rosetta, CPU JIT/interpreter fallback or omitted draws.

On M3 Max, the final native 4K uncapped comparison was 82.81 vs 82.80 FPS: **no
FPS gain demonstrated**. A three-minute autosave observation started with an empty
port shader cache and reached 59.99 FPS at cap 60, with p95 16.87 ms and maximum
33.17 ms. Balanced MetalFX to 4K with movement/attacks measured 59.98 FPS over one
minute. These are bounded VideoOut observations, not a long boss-fight or
physical-display benchmark. The Metal driver cache was not erased; preparation
and loading occur before the observation window. The first new-binary warm-up
took 149.5 seconds; a repeated warm launch took 7.14 seconds.

Full-campaign coverage, extended boss fights/phase transitions and other Macs
remain unqualified. This release makes no claim that all stutter is eliminated
or that every upstream correction has been integrated.
