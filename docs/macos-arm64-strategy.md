# ARM64 strategy review — 2026-10-16

The existing architecture remains useful, but fixing only the instruction reported by
one boot is an inefficient coverage strategy. Keep the working loader and renderer;
make compiler changes by semantic family and validate control flow independently of
instruction semantics. This is an engineering recommendation, not proof that this
route has the shortest possible completion time.

## What comparable projects actually provide

- [N64Recomp](https://github.com/N64Recomp/N64Recomp) translates MIPS functions to C
  using symbols/metadata. Its runtime resolves indirect function calls separately.
  Its architecture is reusable as a design; its MIPS semantics and console services
  cannot directly execute this PS4 x86-64 binary.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) translates Xbox 360 PowerPC
  code to C++. It supports explicit function boundaries and jump-table information.
  This reinforces the need for control-flow metadata and a separate console runtime.
- [bloodborne-recomp](https://github.com/bloodborne-native/bloodborne-recomp/blob/main/docs/RECOMPILATION.md)
  is our closest reusable base. Its published progress permits interpreter/translator
  fallback, including inside generated functions. Its successful hybrid game runs
  are not evidence that all code paths work under our strict AOT-only loader.
- [Remill](https://github.com/lifting-bits/remill) provides x86/AMD64 instruction
  semantics lowered to LLVM bitcode. It is a plausible alternative semantic backend,
  not a finished PS4 runtime: integration would require register/memory model bridges,
  control-flow recovery, ABI adapters, build changes and differential validation.
  We have not built it here or measured a benefit over the existing compiler.

An ARM64 executable can still be an interpreter or dynamic translator. “No Rosetta”
and “no runtime CPU translation” are different constraints. This implementation keeps
the stronger current property: generated game code is compiled before execution;
missing semantics/targets stop explicitly. A hybrid path might bring up more game
code earlier, but would change that property. No hybrid fallback was enabled.

## Generalization implemented in this revision

1. Static gap inventory is emitted whenever the game is prepared. It groups missing
   sites by mnemonic and affected function candidates, with addresses retained only
   in the local build directory. Counts include scanner false positives and unreachable
   bytes; they are not a percentage of a playable port.
2. Complete packed widening matrix: six source/destination width ratios, signed and
   unsigned, SSE/AVX128/AVX256, separate registers, aliasing and memory. All 108 forms
   use one lowering. A separate byte-copy/sign-fill reference checks the result,
   avoiding dependency on the upstream interpreter's missing BQ/WQ implementations.
3. Shared vector operand capture, lane loops and writeback now cover multiplication,
   multiply-add, min/max, saturating add/subtract, narrowing packs and comparisons.
   The fixture cross product checks register aliasing, both vector widths, memory,
   and legacy preservation versus VEX clearing of upper lanes.
4. Backward reaching-definition analysis identifies a table address across known
   control-flow edges, loops and SysV calls that preserve its register. Unknown or
   conflicting definitions are rejected. Direct paths are decoded before this pass;
   newly recovered cases trigger another analysis pass. Recovered indirect edges
   are retained so a later table can resolve a base through an earlier table. Narrow comparisons without proven zero extension now emit an explicit full-register
   runtime guard before the table load. The index is never truncated. Every table
   target must remain inside the function. The game preparation also enables bounded
   partial recovery of local tables when an index was spilled to memory: local object
   references limit candidate entries, targets inside table bytes or known instruction
   interiors are rejected, and a runtime guard checks the complete index. This is
   explicitly a supported subset, not a proof of all possible indirect destinations.
   Original index values and computed branch targets remain unchanged; an index
   outside the recovered subset stops in strict AOT.
5. Generated fallthrough follows the exact decoded instruction length. Sorted
   instruction starts can overlap; an interior candidate must never execute merely
   because its generated C appears next. Synthetic tests cover both an untaken edge
   into an instruction immediate and a deliberately taken unsupported interior path.
6. Packed floating conversions, double shuffles/permutations, sign-mask extraction,
   high/low 64-bit moves and non-temporal vector transfers share existing generic
   operand and writeback helpers. These preserve the runtime's existing floating
   point model; full x86 exception flags and non-default rounding are not certified.
   Sign-controlled vector negation and rounded multiply use separate byte-oriented
   references, including the signed-minimum multiply corner. FSCALE accepts finite int-sized exponents and rejects undefined conversions;
   x87 still uses the upstream double-valued stack rather than 80-bit arithmetic.

## Cost and priorities

For k related gaps, the old process pays k times for generation, compilation and
boot, in addition to semantic implementation. A family pass pays for a shared
implementation and a fixture matrix, then one integration cycle. It does not remove
unrelated graphics, timing or service failures. Test counts measure checked forms,
not game progress or speed.

Prioritize real boot blockers and high-confidence static gaps. Do not implement
privileged instructions simply because a scanner found many occurrences: those can
be data misidentified as code. Keep GPU compatibility tests separate, use native
ARM64 tools, and require a captured menu plus input response before claiming a
working menu. Gameplay, audio quality and performance require additional evidence.

## Observed effect of the strategy

The exact-successor change eliminated a false CMC/invalid-instruction stop: the
candidate started inside a conditional branch's displacement, and sorted C output
had accidentally executed it on fallthrough. This is why adding every reported
mnemonic would be unsound. Check decoding and control flow before expanding semantics.
The next real runs reached interactive brightness setup. Scripted Right moved its
slider; Cross continued into the opening credits. A subsequent boot loaded the saved
setup preference. These observations validate specific UI paths, not gameplay or
complete save compatibility. Some captures showed incomplete text, others complete
text; graphics stability remains a separate issue.

The final bounded native run rendered the main menu and changed its selection.
A subsequent capture showed difficulty selection. The user selected New Game and
reported the opening cutscene starting. The run ended at its configured 420-second
watchdog, with exit 142, rather than an unsupported-instruction stop. This is a
verified menu milestone, not proof of playable gameplay or a complete port.
