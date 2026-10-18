"""Exercise the actual loader with original synthetic x86 instructions, no game data.
The strict runtime must call real HLE and reject changed/uncompiled code without interpreting.
"""
from pathlib import Path
import os,struct,subprocess
import sys
exe,gen,src,inc,compiler,directory=sys.argv[1:]
exe=Path(exe);gen=Path(gen);src=Path(src);out=Path(directory)
out.mkdir(parents=True,exist_ok=True)
def require_arm64(path):
    assert subprocess.check_output(['/usr/bin/lipo',str(path),'-archs'],text=True).strip()=='arm64'
require_arm64(exe);require_arm64(gen)
# Hand-authored entry: call _init_env and then an unsupported import.
code=bytes.fromhex('4883ec08ff15160000004883c408ff2514000000')
image=code.ljust(16384,b'\0')
boot=struct.pack('<8s6Q',b'G3PROBE2',len(image),0,1,2,2,1)+struct.pack('<3Q',0,len(image),5)
boot+=b'bzQExy189ZI#libc'.ljust(128,b'\0')+b'after-arm64-init'.ljust(128,b'\0')
boot+=struct.pack('<8Q',32,1,0,0,40,1,1,0)+image
(out/'boot.bin').write_bytes(boot)
eh=struct.pack('<16sHHIQQQIHHHHHH',b'\x7fELF\x02\x01\x01'.ljust(16,b'\0'),3,62,1,0,64,0,0,64,56,1,0,0,0)
ph=struct.pack('<II6Q',1,5,16384,0,0,len(image),len(image),16384)
(out/'fixture.elf').write_bytes((eh+ph).ljust(16384,b'\0')+image)
(out/'functions.tsv').write_text('offset\tsize\n0x0\t'+str(len(code))+'\n')
env=dict(os.environ,BB_JIT='0')
subprocess.run([gen,out/'fixture.elf',out/'functions.tsv',out/'recomp.c','0'],env=env,check=True)
subprocess.run([compiler,'-arch','arm64','-O2','-dynamiclib','-ffp-contract=off','-fno-strict-aliasing','-I'+str(src/'src'),'-I'+str(inc),*map(str,out.glob('recomp*.c')),'-o',str(out/'libfixture.dylib')],check=True)
require_arm64(out/'libfixture.dylib')
env['GOW3_RECOMP_LIB']=str(out/'libfixture.dylib')
r=subprocess.run([exe,out/'boot.bin','--cpu-only'],env=env,capture_output=True,text=True,timeout=10)
print(r.stdout+r.stderr,flush=True)
assert r.returncode==20,r.returncode
assert '_init_env=1' in r.stdout
assert 'first unsupported PS4 import: after-arm64-init' in r.stdout
assert '0 blocks translated' in r.stdout
assert '0 instructions interpreted from translated code' in r.stdout
# Decoding at load time validates the compiled functions' hashes; it is not execution.
# Changing a compiled instruction must invalidate the code hash and fail closed.
bad=bytearray(boot);bad[-len(image)]=0x90;(out/'changed.bin').write_bytes(bad)
r=subprocess.run([exe,out/'changed.bin','--cpu-only'],env=env,capture_output=True,text=True,timeout=10)
print(r.stdout+r.stderr)
assert r.returncode==30,r.returncode
assert 'ARM64 AOT coverage gap' in r.stderr
print('PASS: ARM64 loader -> AOT entry -> real HLE call -> named import trap; modified code rejected, no CPU fallback')
# A library requesting the old per-instruction fallback must fail closed, even
# though its instruction hash matches the loaded image.
chunk=out/'recomp_00000.c'
original=chunk.read_text()
needle='void f_0(BbCpu *c) {'
assert needle in original
chunk.write_text(original.replace(needle,needle+'\n c->rip=rc->image_base; rc->step(c); return;\n',1))
try:
    subprocess.run([compiler,'-arch','arm64','-O2','-dynamiclib','-I'+str(src/'src'),'-I'+str(inc),
                    *map(str,out.glob('recomp*.c')),'-o',str(out/'libfallback.dylib')],check=True)
finally:
    chunk.write_text(original)
env['GOW3_RECOMP_LIB']=str(out/'libfallback.dylib')
r=subprocess.run([exe,out/'boot.bin','--cpu-only'],env=env,capture_output=True,text=True,timeout=10)
print(r.stdout+r.stderr)
assert r.returncode==30
assert 'interpreted instruction disabled' in r.stderr
print('PASS: matching library cannot execute an instruction fallback')
