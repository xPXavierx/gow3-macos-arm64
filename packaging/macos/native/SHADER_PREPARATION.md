# Graphics shader preparation

The desktop launcher selects `GOW3_SHADER_POLICY=complete`. Known cached pipelines
are loaded before the game starts, using up to six workers. A new shader or
pipeline is captured/compiled on workers and the requesting draw waits for it;
a full queue falls back to the synchronous compilation path. Failed required
compilation stops with a diagnostic rather than silently omitting the draw.

This does not discover every game shader from the dump in advance. Variants not
yet observed must still be compiled on first use and can cause a pause. Existing
CPU ARM64 AOT preparation, shaders, texture readback, visual settings and saves
are preserved. This policy changes readiness and scheduling, not shader math.

## Key identity and compiler cache reuse

GraphicsPipelineKey uses its entire byte representation for hashing, equality
and serialization. Explicit byte-preserving copy/assignment is required because
member-wise relocation inside a robin-hood table may leave bit-field padding
behind, changing the key after insertion. pipeline-key-identity-test covers copy,
move, displacement and rehash of 4096 keys including nonzero serialized padding;
the delivered 0.4.3 header fails that regression test. If a completed pipeline
job still misses publication, the renderer logs that cache miss and uses the
synchronous build path. It no longer reports an empty compiler error and aborts
for a successful job; actual compilation failures still stop with a diagnostic.

Complete rendering waits for one requested pipeline at a time. These sequential
builds now reuse the warmed main VkPipelineCache, so related new pipelines can
reuse Metal shader libraries. The diagnostic skip mode retains separate worker
caches because its pipeline builds can overlap. This does not remove first-time
compilation of previously unknown shader variants.

CPU code is compiled to ARM64 ahead of execution. The graphics backend still
lowers PS4 shaders to SPIR-V and then uses MoltenVK/Metal at runtime; it is not a
complete offline Metal shader catalog. CPU AOT does not by itself eliminate GPU
pipeline-creation stutter.

## Diagnostic comparison

`GOW3_SHADER_POLICY=skip` retains the previous asynchronous/omission behavior for
controlled developer comparisons. The desktop adapter ignores inherited GOW3
experiments and explicitly selects complete rendering.

`GOW3_FRAME_STATS=1 GOW3_FRAME_TRACE=/absolute/path/flips.txt` optionally records
each real guest flip. Metal can present an old image while the game stalls, so
drawable presentation FPS alone is insufficient. On macOS, libc++ steady_clock
uses CLOCK_MONOTONIC_RAW; align its epoch before comparing with Python
monotonic/mach_absolute_time. Keep captures and builds outside timing windows.

## Bounded M3 Max experiment — 2026-10-22

Native 3840x2160, cap 60, V-Sync, display 60 Hz, no MetalFX/frame generation or
Rosetta. Same copied Gaia 3:21 Titan save and scripted block/attack/dodge inputs.
Four 60-second combat windows; no screenshots/builds inside them.

| Policy/cache | Game FPS | p99 (ms) | Largest interval (ms) | Omitted draws in reported combat windows |
|---|---:|---:|---:|---:|
| skip-cold-2 | 57.61 | 25.21 | 44.17 | 485 |
| skip-warm-2 | 60.00 | 19.51 | 25.30 | 484 |
| complete-cold-2 | 59.88 | 19.90 | 134.88 | 0 |
| complete-warm-2 | 60.01 | 16.68 | 20.43 | 0 |


Complete/warm preloaded **233/233 pipelines in 1.18 s on six workers** and recorded
zero omitted draws in the entire session. Earlier exploratory complete/cold
playback had a **1,229.3 ms** worst combat-frame interval in the guest statistics;
this remains evidence of first-use variability, despite the later traced run
showing a 134.9 ms maximum.

Limits: the application caches were isolated/empty, but the macOS driver cache
was shared and not reset. All four measured combat windows had the game
unfocused; these are controlled relative policy checks, not maximum foreground
performance certification. Enemies/camera differ between runs, so small mean FPS
differences are not proof of a GPU speedup. Actual Gaia/Poseidon trail causation
still needs the reproducible save. Captures were inspected, not pixel-compared
as deterministic identical frames. Only one Mac and bounded gameplay were tested.

## Command-stream preparation experiment (2026-10-23)

The candidate implements `GOW3_SHADER_LOOKAHEAD` with a bounded 12-job queue per
kind (shader permutations and graphics pipelines). Draw-preparation workers can
now prepare missing pipeline combinations and vertex/fragment permutations of
programs whose resource layouts are already known. They still do not discover
an entire game's shader catalog from its assets.

When a new shader module is published, a generation change wakes the preparation
workers. They revisit still-pending draws from command buffers that the GPU has
not passed; they do not overwrite Ready draws or release their snapshot arenas.
The wake uses a condition variable rather than periodic polling. The GPU thread
publishes completed results and alone owns the live pipeline/program maps.

Shader translation uses owned code, user data, flattened resources and fetch
snapshots. The generated resource-walk table must match the captured table byte
for byte, not merely have the same size. Specialization is checked again before
publication/use. Future vertex descriptors can be incomplete: unsupported
captured formats are rejected and assertions within speculative jobs become job
failures. A required draw retains its fatal error policy and full synchronous
fallback; speculative rejection never authorizes omitting it.

Completely unseen programs, compute/tessellation paths and pipelines reached
before their preparation finishes can still block. Looking ahead only in command
buffers provides a short lead time. Eliminating those first-use stalls requires
scene/asset-time pipeline enumeration or an offline catalog reconstructed from
the user's dump, including render state and resource specializations. Moving a
compiler onto a worker is insufficient. Apple documents precompiling GPU binary
archives in [Metal binary archives](https://developer.apple.com/documentation/metal/metal-binary-archives);
Vulkan's [pipeline-cache guidance](https://docs.vulkan.org/guide/latest/pipeline_cache.html)
also distinguishes reusable compiler data from live prepared pipeline objects.

`GOW3_SHADER_TRACE=1` optionally identifies required waits >= 2 ms by phase and
shader/pipeline hash. `Pipeline look-ahead stats` and `Shader look-ahead stats`
report queueing, publication, direct consumption/waits, rejections and outstanding
jobs. Publication counts are not proof that every prepared object was drawn.
Only actual guest-flip traces are used for FPS/stutter comparisons.

Validation includes the actual concurrent MoltenVK shared-cache test, immutable
fetch/resource snapshots, byte-preserving pipeline keys and a negative control:
a speculative assertion rejects one job and restores the worker policy, while
the same assertion in required rendering still exits with code 23. The first
long candidate exposed an invalid speculative vertex format and exited; that
failed run must remain in the experiment history, outside successful FPS tables.


## First-launch asset catalogue (0.4.7 local)

The standard launcher keeps complete rendering, async compiler workers and
command anticipation enabled. Before the first game launch it indexes bounded
GCN programs directly from the selected dump. An unaligned `OrbShdr` match alone
is insufficient: its code header, footer distance, code length, version and ID
must agree. The index stores only paths, offsets, lengths and SHA-256 digests.
It is rebuilt when source size/mtime or its own checksum changes.

The supplied **partial render-description corpus** covers the opening and menus:
831 shader specialization recipes and 445 graphics pipeline descriptions. It
contains no GCN program, SPIR-V, Metal binary, CPU AOT or original asset payload.
Resource addresses and unused user data are removed; PSO records serialize only
numeric state and vector elements, never C++ container or Vulkan pointer fields.
These are known combinations collected by the bounded validation sessions, not
all combinations inferred from every material in the campaign.

At renderer startup, source code comes from the player's indexed dump. A shader
is translated with owned neutral resource/fetch descriptions; generated resource
walk layout and full specialization must match. Compilation assertions reject
that preparation locally. Recipes are tied to shader/pipeline format versions
and structure sizes. Export additionally requires **byte-identical SPIR-V** to
the measured reference. The resulting binaries and metadata are saved on the
player's Mac, followed by normal parallel pipeline warm-up before playable frames.
New/uncovered states retain complete required rendering, async compilation and
persistent caching; a late indispensable job can still delay its requesting draw.

The index also groups programs by exact source code. Existing prepared programs
can serve additional asset identifiers when code bytes match on use, hardware
stage agrees and normal current runtime/resource/fetch specialization matches.
This neither builds a combinatorial cross product nor substitutes a similar
material. Dynamic constants/buffers still bind from the actual current draw.
The index covers all WADs, while PSO state coverage remains partial. Completely
new code families, compute/tessellation and unrecorded render states can still
compile during play. No campaign-wide or zero-stutter guarantee is made.

The first scan of the tested 816 WADs took about 47 seconds on the external SSD;
subsequent launches reuse the validated index. This is separate from first-run
CPU AOT preparation and startup GPU compilation. Clear shader cache also removes
this index and local GPU binaries; it preserves saves, settings and CPU AOT.
Catalogue failure falls back to complete on-demand rendering. It must not prevent
the game opening or silently omit draws.

Diagnostics (launcher cleans inherited experiments):

- `GOW3_SHADER_CATALOG`: read-only description directory.
- `GOW3_SHADER_ASSET_INDEX`: locally generated `programs.tsv`.
- `GOW3_SHADER_EQUIVALENCE=0`: disable equivalence for comparisons.
- `GOW3_SHADER_CATALOG_EXPORT`: maintainer-only export, byte-compared against
  an existing reference cache; never enabled by the normal launcher.
- `Shader catalog`, `Pipeline warm-up`, `Shader asset equivalence` and
  `Shader equivalence stats` report actual work. Additional identifiers are not
  a count of additional compiled pipelines, nor proof they were drawn.

Cache writes have a startup-only barrier after importing descriptions. Live
draws never drain the global compiler/cache queue; they wait for their own
required shader/pipeline when necessary. Atomic request accounting prevents
losing an IO request during concurrent saves. Description readers bound container
lengths and seal written bytes rather than unused preallocated space.


Multiple validation recordings are merged with canonical catalogue permutation
IDs. At import, the full specialization is matched against the player's current
cache; an occupied local index is never assumed to mean the same variant.
Matching stages retain their indices; new ones are appended. Every catalogue PSO
stage reference is rewritten to that local mapping before warm-up. Existing
binaries are preserved. Dynamic vertex-binding strides may differ between
recordings of an otherwise identical pipeline; duplicate checking ignores only
that dynamic value, retaining the first valid static description. Live strides
continue to come from the current draw via `bindVertexBuffers2`.

Exact-code asset reuse is limited to **already prepared, fully matching** runtime,
fetch and resource specializations on the same hardware stage. A novel state
falls back to its original program ID and normal async path; it does not learn
an unverified new variant under another asset's program template.


Each recipe also carries a source-code SHA-256 and a reference SPIR-V XXH3
fingerprint. Import reuses an existing local variant only when its binary, full
specialization and generated resource-walk layout agree. A different GPU profile
reconstructs locally before comparing. Known shader-side fetch number conversions
keep their original program ID; they cannot reuse neutral no-conversion recipes
through the additional-identifier path. These restrictions favor correct visuals
over broader unverified coverage.
