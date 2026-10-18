"""Check x86 -> C -> ARM64 on hand-authored instructions, with no Rosetta.

The external differential harness compares generated native functions against
its interpreter. Generated functions get no interpreter/JIT fallback callbacks.
This is a toolchain check, not evidence of game coverage or gameplay.
"""
import os
from pathlib import Path
import subprocess
import sys

# Integer arithmetic/flags, bit operations, memory, atomics and SSE/AVX.
ENCODINGS = (
    "f6f3", "f6fb", "f6f7", "f6ff", "f633", "f63b", "41f67336", "41f67b36",
    "d9fd", "d9fa",
    "660f3801c1", "c4e27901c0", "c4e27d01c0", "c4e2690103", "c4e26d0103", "660f3802c1", "c4e27902c0", "c4e27d02c0", "c4e2690203", "c4e26d0203",
    "f30f16c1", "f30f1603", "c5fa16c1", "c5fe16c1", "c5fe1603", "f30f12c1", "f30f1203", "c5fa12c1", "c5fe12c1", "c5fe1203", "f20f12c1", "f20f1203", "c5fb12c1", "c5ff12c1", "c5ff1203",
    "dac1", "dac9", "dad1", "dad9", "dbc1", "dbc9", "dbd1", "dbd9",
    "df03", "db03", "df2b", "dd8d78feffff",
    "dbe9", "dbf1", "dfe9", "dff1", "d93b", "d92b", "dd3b", "dfe0",
    "dee1", "dee9", "dec1", "dec9", "def1", "def9", "d8c1", "d8c9", "d8e1", "d8e9", "d8f1", "d8f9", "dcc1", "dcc9", "dce1", "dce9", "dcf1", "dcf9", "d9c9", "d9e8", "d9ee", "d9e0", "d9e1", "d803", "d80b", "d823", "d82b", "d833", "d83b", "dc03", "dc0b", "dc23", "dc2b", "dc33", "dc3b", "da03", "da0b", "da23", "da2b", "da33", "da3b", "de03", "de0b", "de23", "de2b", "de33", "de3b",
    "dd03", "d903", "db2b", "d9c0", "d9c7", "dd13", "d913", "dd1b", "d91b", "db3b", "ddd3", "dddb",
    "c5f8ae5de4", "0fae1f", "c5f8ae17", "0fae17",
    "f0480fc74dc0", "f0480fc70f", "480fc70f",
    "660f7cc1", "c5f17cc9", "c5f57cc9", "660f7c03", "c5e97c03", "c5ed7c03",
    "660f60c1", "c5e960d2", "c5ed60d2", "c5e96003", "660f68c1", "c5e968d2", "c5ed68d2", "c5e96803", "660f61c1", "c5e961d2", "c5ed61d2", "c5e96103", "660f69c1", "c5e969d2", "c5ed69d2", "c5e96903", "660f62c1", "c5e962d2", "c5ed62d2", "c5e96203", "660f6ac1", "c5e96ad2", "c5ed6ad2", "c5e96a03", "660f6cc1", "c5e96cd2", "c5ed6cd2", "c5e96c03", "660f6dc1", "c5e96dd2", "c5ed6dd2", "c5e96d03", "660f3800c1", "c4e26900d2", "c4e26d00d2", "c4e26d0003",
    "c4e3790ac901", "660f3a0ac101", "660f3a0bc102", "c4e3710bc303",
    "660f3a08c100", "660f3a09c101", "c4e37d08c102", "c4e37d09c103",
    "c4e3790ac904", "660f3a0ac104", "660f3a0bc104", "c4e3710bc304",
    "660f3a08c104", "660f3a09c104", "c4e37d08c104", "c4e37d09c104",
    "c4e3790a0301", "660f3a0b0302", "c4e37d080303",
    "0faef0", "0faee8", "0faef8",
    "48f7e9", "48f7e3", "f7eb", "f7e3", "66f7eb", "66f7e3", "f6eb", "f6e3", "48f72b", "48f723",
    "c1c207", "c1c80d", "48c1c007", "48c1c80d", "66c1c007", "66c1c80d",
    "c0c007", "c0c80d", "48d3c0", "48d3c8", "d3c0", "d3c8", "66d3c0", "d2c8",
    "48c10307", "c00b01", "c1c000", "66c1c010", "c0c008",
    "660f3820c1", "c4e27920c1", "c4e27d20c1", "660f3821c1", "c4e27921c1", "c4e27d21c1", "660f3823c1", "c4e27923c1", "c4e27d23c1", "660f3825c1", "c4e27925c1", "c4e27d25c1", "660f3830c1", "c4e27930c1", "c4e27d30c1", "660f3831c1", "c4e27931c1", "c4e27d31c1", "660f3833c1", "c4e27933c1", "c4e27d33c1", "660f3835c1", "c4e27935c1", "c4e27d35c1",
    "c4e37d18c001", "c4e37518c300", "c4e37518c301", "c4e375180301",
    "c4e37d19c300", "c4e37d19c301", "c4e37d190301",
    "c4e37538c301", "c4e375380300", "c4e37d39c301", "c4e37d390300",
    "660f64c1", "c5e964ed", "c5ed64ed", "660f65c1", "c5e965ed", "c5ed65ed",
    "660f66c1", "c5e966ed", "c5ed66ed",
    "660ffcc1", "c5d9fce8", "c5ddfce8", "660ff8c1", "c5d9f8e8", "c5ddf8e8",
    "660ffdc1", "c5d9fde8", "c5ddfde8", "660ff9c1", "c5d9f9e8", "c5ddf9e8",
    "f20f5ac1", "c5fb5ac0", "c5f35ac3", "f20f5a03", "c5f35a03",
    "f30f5ac1", "c5fa5ac0", "c5f25ac3", "f30f5a03", "c5f25a03",
    "488703", "8703", "668703", "8603", "4887d8", "87d8", "6687d8", "86d8",
    "c4e2f8f3ce", "c4e2f8f3de", "c4e2f8f3d6", "c4e270f3c9", "c4e270f7c3", "c4e270f2c3",
    "0fbcc3", "0fbdc3", "f30fbcc3", "f30fbdc3", "f30fb8c3", "0fbc03", "0f38f003", "0f38f103",
    "48f7f3", "48f7fb", "f7f3", "f7fb", "66f7f3", "66f7fb",
    "fc", "fd", "f3aa", "f348ab", "f3a4", "f348a5", "f3a6", "f348a7", "f2ae", "f248af", "f3ac", "f348ad",
    "4801d8", "4811d8", "4829d8", "4819d8", "4831d8", "4821d8",
    "4809d8", "48d3e0", "48d3e8", "48d3f8", "480fafc3", "480fc8",
    "488b03", "488903", "f0480fc103", "f0480fb10b",
    "0f58c1", "660f58c1", "660fefc1", "c5f858c1", "0fc8", "490fcf", "410fcf",
)


# Declarative cross product: six widening ratios, signed/unsigned, SSE/AVX128/
# AVX256, separate registers/aliased registers/memory. No game bytes required.
def widening_encodings():
    for signed in (True, False):
        for ratio in range(6):  # BW, BD, BQ, WD, WQ, DQ
            opcode = (0x20 if signed else 0x30) + ratio
            for prefix in ("660f38", "c4e279", "c4e27d"):
                for modrm in ("c1", "c0", "03"):
                    yield f"{prefix}{opcode:02x}{modrm}"

# Packed binary family encodings use independent opcode metadata, not C semantics.
PACKED_BINARY_OPCODES = [(2, 8), (2, 9), (2, 10), (2, 11), (1, 213), (1, 229), (1, 228), (2, 64), (1, 244), (2, 40), (1, 245), (2, 56), (2, 60), (1, 218), (1, 222), (1, 234), (1, 238), (2, 58), (2, 62), (2, 57), (2, 61), (2, 59), (2, 63), (1, 117), (2, 55), (1, 236), (1, 237), (1, 220), (1, 221), (1, 232), (1, 233), (1, 216), (1, 217), (1, 107), (2, 43), (1, 99), (1, 103)]

def packed_binary_encodings():
    for opcode_map, opcode in PACKED_BINARY_OPCODES:
        # SSE: dst=xmm0/src=xmm1, alias xmm0, memory [rbx].
        prefix = "660f" + ("38" if opcode_map == 2 else "")
        for modrm in ("c1", "c0", "03"):
            yield f"{prefix}{opcode:02x}{modrm}"
        # VEX: distinct dst=0/src1=2/src2=1; dst aliases src2; memory.
        for vl in (0x69, 0x6d):
            for modrm in ("c1", "c0", "03"):
                yield f"c4{0xe0 + opcode_map:02x}{vl:02x}{opcode:02x}{modrm}"

def packed_float_encodings():
    for prefix, pp, opcode in (("",0,0x5a),("f3",2,0xe6),("66",1,0x5a),("f2",3,0xe6),("66",1,0xe6)):
        for modrm in ("c1","c0","03"):
            yield f"{prefix}0f{opcode:02x}{modrm}"
            for vl in (0x78,0x7c):
                yield f"c5{vl|pp:02x}{opcode:02x}{modrm}"
    for prefix, pp, lengths in (("66",1,(0,4)),("f2",3,(0,))):
        for opcode in (0x5d,0x5f):
            for modrm in ("c1","c0","03"):
                yield f"{prefix}0f{opcode:02x}{modrm}"
                for vl in lengths:
                    yield f"c5{0xe8|pp|vl:02x}{opcode:02x}{modrm}"
    for imm in (0,5,10,15):
        for prefix, opcode in (("660f",0xc6),("c5e9",0xc6),("c5ed",0xc6),("c4e379",5),("c4e37d",5)):
            for modrm in ("c1","c0","03"):
                yield f"{prefix}{opcode:02x}{modrm}{imm:02x}"
    for vl in (0x69,0x6d):
        for opcode in (0x0c,0x0d):
            for modrm in ("c1","c0","03"):
                yield f"c4e2{vl:02x}{opcode:02x}{modrm}"

def vector_move_encodings():
    yield from ("660fe703","c5f9e703","c5fde703","660f382a03","c4e2792a03","c4e27d2a03",
                "0f2b03","c5f82b03","c5fc2b03","660f2b03","c5f92b03","c5fd2b03")
    for pp,prefix in ((0,""),(1,"66")):
        for load in (0x12,0x16):
            yield f"{prefix}0f{load:02x}03"
            yield f"{prefix}0f{load+1:02x}03"
            yield f"c5{0xe8|pp:02x}{load:02x}03"
            yield f"c5{0xf8|pp:02x}{load+1:02x}03"
    for prefix,pp,opcode in (("",0,0x50),("66",1,0x50),("66",1,0xd7)):
        for modrm in ("c1","c0"):
            yield f"{prefix}0f{opcode:02x}{modrm}"
            for vl in (0x78,0x7c):
                yield f"c5{vl|pp:02x}{opcode:02x}{modrm}"

ENCODINGS = tuple(dict.fromkeys((*ENCODINGS, *widening_encodings(), *packed_binary_encodings(), *packed_float_encodings(), *vector_move_encodings())))

def require_arm64(path):
    architectures = subprocess.check_output(["/usr/bin/lipo", str(path), "-archs"], text=True).strip()
    if architectures != "arm64":
        raise RuntimeError(f"Expected ARM64-only artifact: {path}: {architectures}")

def main():
    generator, tester, source, compiler, directory, decoder_include = sys.argv[1:]
    out = Path(directory)
    out.mkdir(parents=True, exist_ok=True)
    encodings = out / "synthetic-instructions.txt"
    generated = out / "synthetic.c"
    library = out / "libsynthetic.dylib"
    encodings.write_text("\n".join(ENCODINGS) + "\n")
    env = dict(os.environ, BB_JIT="0")
    env.pop("ROSETTA_ADVERTISE_AVX", None)
    for executable in (generator, tester):
        require_arm64(executable)
    result = subprocess.run([generator, "--snippets", str(encodings), str(generated)],
                            env=env, text=True, capture_output=True, check=True)
    print(result.stdout, end="", flush=True)
    expected = f"{len(ENCODINGS)} instructions translated natively (of {len(ENCODINGS)} lines)"
    if expected not in result.stdout:
        raise RuntimeError("An instruction was skipped; this verification requires full fixture coverage")
    # Each instruction must have native semantics. The generator also emits an
    # unreachable generic branch-dispatch epilogue after the snippet's return;
    # the harness supplies NULL fallback callbacks, so any executed fallback
    # crashes and fails the differential test rather than silently interpreting.
    code = generated.read_text()
    if "rc->step(" in code:
        raise RuntimeError("Unexpected interpreted instruction in native fixture")
    subprocess.run([compiler, "-arch", "arm64", "-O2", "-std=gnu11", "-dynamiclib",
                    "-ffp-contract=off", "-fno-strict-aliasing", "-I" + str(Path(source) / "src"),
                    "-I" + decoder_include,
                    str(generated), "-o", str(library)], check=True)
    require_arm64(library)
    result = subprocess.run([tester, str(encodings), str(library), "200", "20"], env=env, check=True, text=True, capture_output=True)
    print(result.stdout + result.stderr, end="", flush=True)
    expected = f"{len(ENCODINGS)} instructions tested, 0 failed, 0 crashed, 0 skipped"
    if expected not in result.stdout:
        raise RuntimeError("The differential oracle did not test every fixture: " + expected)

if __name__ == "__main__":
    main()
