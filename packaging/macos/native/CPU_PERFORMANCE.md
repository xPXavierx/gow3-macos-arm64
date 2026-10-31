# CPU costs and resolution scaling

Lowering the scene resolution reduces pixel work. Guest logic, visibility work,
command preparation, draw count, and fixed-size shadow maps also set a frame-time
floor. Native ARM64 execution alone does not remove those costs.

## Changes in 0.4.12-local

- Register changes are enumerated from an owned bitmap with `countr_zero`, on every
  C++ standard library. The previous libc++ path tested all 1,664 register blocks
  per draw; libstdc++ had a sparse iterator. `RegDelta` uses the same traversal.
  The hot full scan disappeared from CPU samples. No isolated FPS gain is assigned
  to this change alone.
- `launcher/aot_vector_cache.py` promotes the pinned recompiler's private vector
  state into C locals. The compiler can keep intermediates in native registers.
  Touched slots are synchronized at the emitter's existing function-call, return,
  tail-call and fallback boundaries; instruction tracing also sees current state.
  Untouched slots retain callee changes. Guest memory operations, acquire/release
  ordering, CPU flags, instructions and branches are preserved.
- Community preparation applies the pass before compiling with the bundled ARM64
  Zig toolchain. The pass participates in the preparation recipe: old prepared
  libraries are retained under their old recipe and cannot be mistaken for new
  code. Saves, preferences and graphics caches are independent of this change.

This is ahead-of-time native compilation. The game remains in strict AOT mode;
no runtime CPU translation or interpreter fallback is enabled. Generated game C
and libraries stay local and must not be committed or distributed.

## Controlled measurement, 2026-10-25

M3 Max, external power, CUSA01623 v1.02, initial Gaia battle, native 1080p,
warm graphics cache, frame generation off. A diagnostic library switches the
same 3,097 native functions between their original and cached forms at function
boundaries. Both sides share the same dispatch overhead. Six three-second
combat windows use the sequence original/cached/cached/original/cached/original;
the first 250 ms of each window is excluded. FPS uses real game-flip timestamps.

| Path | FPS in the three combat windows |
| --- | --- |
| Original vector state accesses | 83.4, 78.4, 83.7 |
| Cached vector state | 110.2, 109.1, 103.7 |

That is approximately **82 to 108 FPS (+32%)** for this combat test. This is a
bounded test, not a full-campaign performance guarantee. In-game controls were
left untouched during it. Earlier separate runs with user input and changed
camera/combat states were rejected as comparative evidence.

The same process then remained paused for original/cached/original measurements:

| Native resolution | Original A | Cached B | Original C |
| --- | ---: | ---: | ---: |
| 1080p | 115.6 | 112.6 | 114.5 |
| 4K | 66.2 | 66.2 | 65.6 |

The pause menu does not benefit from the combat CPU optimization (about 2% lower
uncapped FPS in that particular 1080p test). At 4K, GPU use was about 96%, the
busiest CPU thread about 56%, and GPU waits consumed about 44% of the command
thread interval. Reducing CPU work cannot remove that remaining GPU cost.
The diagnostic real-frame limit was 120 FPS; the engine/vblank profile was 240 Hz.

World-only screenshot comparisons across the paused switch had mean RGB absolute
errors below 0.025/255. They are not bit-identical screenshots. No missing draws,
CPU fallback, visible material corruption or geometry loss was observed in the
accepted test. Fixed-size shadow work and the remaining main-thread CPU costs
still need optimization; four times fewer pixels does not imply four times less
work for the entire frame.

## Checks

- 2,000 randomized register-file submission replays, including ClearState,
  overlapping writes, bitmap boundaries and overflowing mark lengths.
- 962 native instruction fixtures, 200 cases each: zero failures/crashes/skips.
- 10,000 native callback/trace/return state comparisons, including upper vector
  lanes and callee changes to slots the caller does not use.
- x87 14/28-byte environment boundaries and exception guards.
- Production community generation and compilation with its bundled compiler.

Run the source tests with:

```sh
python3 -B -m unittest discover -s tests -p test_aot_vector_cache.py
python3 -B -m unittest discover -s tests -p test_community_preparation.py
cmake --build <gpu-build> --target register-delta-test
```

The existing `packaging/macos/aot/verify_aot.py` verification now tests the vector
cache pass as part of instruction generation. `compile_game.py` applies the same
pass by default; `--no-vector-cache` is a maintainer A/B diagnostic.
