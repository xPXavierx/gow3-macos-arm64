#!/usr/bin/env python3
"""Game-derived source and libraries must remain local; do not redistribute them."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import subprocess,os,hashlib,json,sys
import argparse
parser=argparse.ArgumentParser(description="Compile locally generated game C into an ARM64-only library")
parser.add_argument('directory',type=Path,help='The aot subdirectory written by prepare_game.py')
parser.add_argument('--recomp-source',type=Path,required=True)
parser.add_argument('--decoder-include',type=Path,required=True)
parser.add_argument('--jobs',type=int,default=4)
parser.add_argument('--opt-level',choices=('1','2','3'),default='2',help='Clang optimization level (default: 2); keep FP contraction disabled')
parser.add_argument('--output-directory',type=Path,help='Separate object/library directory for controlled build comparisons')
parser.add_argument('--no-vector-cache',action='store_true',help='Diagnostic: retain the emitter vector state accesses for A/B comparisons')
a=parser.parse_args()
generated=a.directory.resolve();out=(a.output_directory or generated).resolve()
out.mkdir(parents=True,exist_ok=True)
src=a.recomp_source.resolve();inc=a.decoder_include.resolve()
compiler='/usr/bin/clang'
headers=b''.join(p.read_bytes() for p in sorted((src/'src').rglob('*.h')))
toolchain=subprocess.check_output([compiler,'--version'])
flags=['-arch','arm64','-O'+a.opt_level,'-std=gnu11','-fPIC','-ffp-contract=off','-fno-strict-aliasing','-I'+str(src/'src'),'-I'+str(inc)]
files=sorted(generated.glob('recomp*.c'))
if not files:raise RuntimeError('No generated recomp*.c files: run prepare_game.py first')
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
from aot_vector_cache import transform
def compile(p):
 obj=(out/p.name).with_suffix('.o');log=(out/p.name).with_suffix('.log')
 code=p.read_text()
 if not a.no_vector_cache:code,_=transform(code)
 digest=hashlib.sha256(toolchain+headers+json.dumps(flags).encode()+code.encode()).hexdigest(); stamp=(out/p.name).with_suffix('.sha256')
 if obj.exists() and stamp.exists() and stamp.read_text()==digest:return obj
 source=p
 if code!=p.read_text():
  directory=out/'vector-sources';directory.mkdir(exist_ok=True)
  source=directory/p.name;source.write_text(code)
 with log.open('w') as f:
  subprocess.run([compiler,*flags,'-c',str(source),'-o',str(obj)],stdout=f,stderr=subprocess.STDOUT,check=True)
 stamp.write_text(digest)
 print('Compiled',p.name,flush=True);return obj
with ThreadPoolExecutor(max_workers=max(1,a.jobs)) as ex:objects=list(ex.map(compile,files))
temporary=out/'libgow3-recompiled.tmp.dylib'
subprocess.run([compiler,'-arch','arm64','-dynamiclib',*map(str,objects),'-o',str(temporary)],check=True)
os.replace(temporary,out/'libgow3-recompiled.dylib')
arch=subprocess.check_output(['/usr/bin/lipo',out/'libgow3-recompiled.dylib','-archs'],text=True).strip()
if arch!='arm64':raise RuntimeError('Expected an ARM64-only library, got '+arch)
(out/'build-settings.json').write_text(json.dumps({'architecture':arch,'compiler':toolchain.decode(),'flags':flags,'source_files':len(files)},indent=2)+'\n')
print('Built ARM64-only library:',out/'libgow3-recompiled.dylib')
