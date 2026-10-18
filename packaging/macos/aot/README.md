# Native ARM64 ahead-of-time recompilation tools

This target integrates [bloodborne-native/bloodborne-recomp](https://github.com/bloodborne-native/bloodborne-recomp),
revision `4b23a301944642f1e4e9e921418d4519dc36c315` (GPL-2.0-or-later,
descended from deadinside28's bbport), with Zydis 4.1.1 (MIT).
The bundled patch adds instruction generation and a strict AOT runtime mode.
The God of War loader integration is in [../native](../native/README.md).
The generator, verifier, runtime and generated libraries execute as ARM64.

The native generator includes `FNSTENV`, `FLDENV`, `FNCLEX`, `FNINIT`, `FWAIT` and `FNOP`.
The environment transfers use the runtime's existing x87 double-stack model,
support both 14-byte and 28-byte operands, save control/status before masking
exceptions, and restore TOP on load. `FWAIT` completes synchronously when no
unmasked exception is pending; a pending #MF keeps the strict diagnostic path.
The fixtures test the save/load boundaries and this exception guard separately
from the interpreter oracle. These instructions do not call an interpreter. Unsupported
instructions still stop with a coverage diagnostic; skipping them can corrupt
game state and is not a valid dump compatibility policy. The x87 model still
does not track full 80-bit precision, tags or instruction/data pointers.

## Reproduce

Run from the gow3_pc root. Dependencies are local to this checkout. Requires
Apple Clang, Python 3, CMake >= 3.24, Ninja and Git. All compiled targets explicitly
select ARM64. Use native ARM64 CMake, Ninja and Python; the latest verification
used CMake 3.31.8 and Ninja 1.13.1 through their ARM64 slices.

```sh
mkdir -p out/aot-deps
git clone https://github.com/bloodborne-native/bloodborne-recomp.git out/aot-deps/recomp
git -C out/aot-deps/recomp checkout 4b23a301944642f1e4e9e921418d4519dc36c315
git -C out/aot-deps/recomp apply "$PWD/packaging/macos/aot/bloodborne-recomp-arm64.patch"
git clone --recursive --branch v4.1.1 https://github.com/zyantific/zydis.git out/aot-deps/zydis
git -C out/aot-deps/zydis checkout a2278f1d254e492f6a6b39f6cb5d1f5d515659dc
git -C out/aot-deps/zydis submodule update --init --recursive

cmake -S out/aot-deps/zydis/dependencies/zycore -B out/aot-deps/zycore-build -G Ninja \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/out/aot-deps/install"
cmake --build out/aot-deps/zycore-build --parallel 6
cmake --install out/aot-deps/zycore-build
cmake -S out/aot-deps/zydis -B out/aot-deps/zydis-build -G Ninja \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/out/aot-deps/install" \
  -DCMAKE_PREFIX_PATH="$PWD/out/aot-deps/install" \
  -DCMAKE_C_FLAGS="-I$PWD/out/aot-deps/install/include" \
  -DZYAN_SYSTEM_ZYCORE=ON -DZYDIS_BUILD_TOOLS=OFF -DZYDIS_BUILD_EXAMPLES=OFF -DZYDIS_BUILD_DOXYGEN=OFF
cmake --build out/aot-deps/zydis-build --parallel 6
cmake --install out/aot-deps/zydis-build
cmake -S packaging/macos/aot -B out/macos-aot -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_ASM_COMPILER=/usr/bin/clang \
  -DGOW3_RECOMP_SOURCE_DIRECTORY="$PWD/out/aot-deps/recomp" \
  -DCMAKE_PREFIX_PATH="$PWD/out/aot-deps/install"
cmake --build out/macos-aot --parallel 6
ctest --test-dir out/macos-aot -V
```

## Validation and limits

The verifier requires every selected fixture to be generated and exercised:
zero skipped instructions, failures or crashes. It compares registers, defined
arithmetic/direction flags and memory against the upstream interpreter using
200 input states per encoding. Generated fixtures have no interpreter callbacks.
Most forms use the interpreter as a test reference only. Packed widening, PSIGN
and PMULHRSW have independent byte-oriented reference helpers because the upstream
interpreter does not implement every tested form. No x86 hardware oracle was used.

The fixtures cover integer arithmetic, BSWAP, strings with direction/overlap,
bit operations, division, atomics, scalar conversion and selected SSE/AVX.
This is differential testing, not an independent x86 hardware oracle, and does
not establish full ISA accuracy, exception semantics or all MXCSR modes.

For game execution, `GOW3_AOT_ONLY` prevents both JIT and interpretation.
Untranslated instructions still appear in generated source as calls to the
fallback API, but that API terminates with a diagnostic in the strict runtime.
Function registration/hash matching is not proof of complete game coverage.
The generator's native-instruction percentage includes discovery false positives
and unreachable bytes; it is not a percentage of the game working.

The related [bloodborne-runtime](https://github.com/bloodborne-native/bloodborne-runtime)
revision `9f809002d247e663c92e46cc96b17b1056a38415` supplied reference code
for the portable SRT walker, guest callback bridge and null-descriptor resources. Existing copyright and
license notices are retained. No game-derived sources are distributed.

Some inherited log counters still use the word "translator" for the dispatcher
that also enters AOT functions and native imports. Those labels are not evidence
of interpreted guest execution. In the strict game runtime, any attempted
instruction fallback stops with exit30; the differential verifier separately uses
an interpreter as its reference. Full x86 exception delivery, 80-bit x87 precision
and every floating-point rounding mode remain outside the verified scope.
