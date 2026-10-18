#!/usr/bin/env python3
"""Map linked modules and discover AOT candidates from unwind metadata, exports and relocations.
This does not guarantee complete code coverage; the strict loader rejects coverage gaps.
"""
from pathlib import Path
import sys,subprocess,struct,json,re,os
import hashlib
import argparse
parser=argparse.ArgumentParser(description="Prepare an ARM64 AOT experiment from a local plaintext game dump. Generated game code stays local.")
parser.add_argument('game',type=Path)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--tools',type=Path,required=True,help='Directory containing gow3-aot-scan and gow3-aot-recomp')
parser.add_argument('--patch','--code-patch',dest='patches',action='append',default=[],help='Exact upstream file-backed patch name to bake before recompilation; repeatable')
parser.add_argument('--scan-only',action='store_true',help='Prepare local loader/function inventory; defer C generation to the selected desktop profile')
a=parser.parse_args()
scan_only=a.scan_only
repo=Path(__file__).resolve().parents[3]
game=a.game.resolve();out=a.out.resolve();tool=a.tools.resolve()
subprocess.run([sys.executable,repo/'scripts/prepare.py',game,'--out',out],check=True)
subprocess.run([sys.executable,repo/'scripts/link_modules.py',game,'--out',out],check=True)
subprocess.run([sys.executable,repo/'scripts/content_profile.py',game,'--out',out,'--sku','full'],check=True)
sys.path.insert(0,str(repo/'scripts'))
from link_modules import module
from isa_rewrite import read_boot_linked
from prepare import nid
from aot_patches import bake
patched_main = bake(game,out,a.patches,module,read_boot_linked) if a.patches else None
if not a.patches:
 (out/'aot-patches.json').unlink(missing_ok=True)
link=json.loads((out/'link.json').read_text())
(out/'native-inputs.json').write_text(json.dumps({
 'boot_sha256':hashlib.sha256((out/'boot-linked.bin').read_bytes()).hexdigest(),
 'note':'Prepared before AOT compilation; run.py verifies the dump/module hashes too',
},indent=2)+'\n')
allfunc={};calls=[];imports=[]
known={nid(n):n for n in ['setjmp','_setjmp','sigsetjmp','longjmp','_longjmp','siglongjmp']}
for name,path,base in [('eboot',game/'eboot.bin',0)]+[(m['file'],game/'sce_module'/m['file'],int(m['base'],16)) for m in link['modules']]:
 m=module(path);d=out/('scan-'+name);d.mkdir(exist_ok=True)
 if name=='eboot' and patched_main is not None:m['elf']=patched_main
 elf=d/'module.elf';elf.write_bytes(m['elf'])
 roots={m['header'][4],m['tags'].get(12,0)}
 roots.update(x['value'] for x in m['symbols'] if x['section'] and x['type']==2)
 for target,kind,sym,addend in m['relocs']:
  if kind==8:roots.add(addend)
  elif kind in (1,6,7) and m['symbols'][sym]['section']:roots.add(m['symbols'][sym]['value']+addend)
 (d/'pointers.txt').write_text(''.join(f'{a:#x}\n' for a in sorted(roots)))
 subprocess.run([tool/'gow3-aot-scan',elf,d,d/'pointers.txt'],check=True)
 sizes={}
 for l in (d/'functions.tsv').read_text().splitlines()[1:]:
  a,z,n,bad=l.split('\t');a=int(a,16);sizes[a]=(int(z),int(n));allfunc[base+a]=int(z)
 for l in (d/'calls.tsv').read_text().splitlines()[1:]:
  a,b=l.split('\t');calls.append((base+int(a,16),base+int(b,16)))
 slots={t:m['symbols'][sym]['name'] for t,kind,sym,add in m['relocs'] if kind in (6,7)}
 for l in (d/'slots.tsv').read_text().splitlines()[1:]:
  a,b=(int(x,16) for x in l.split('\t'))
  if b in slots and sizes.get(a,(0,0))[1]<=2:
   n=slots[b].split('#')[0];imports.append((base+a,known.get(n,n)))
 # Unwind metadata need not cover every exported leaf function.
 for sym in m['symbols']:
  if sym['section'] and sym['type']==2 and sym['size'] and any(p['flags']&1 and p['vaddr']<=sym['value'] and sym['value']+sym['size']<=p['vaddr']+p['memsz'] for p in m['ph'] if p['type']==1):
   allfunc.setdefault(base+sym['value'],sym['size'])
combined=out/'aot';combined.mkdir(exist_ok=True)
(combined/'functions.tsv').write_text('offset\tsize\n'+''.join(f'{a:#x}\t{z}\n' for a,z in sorted(allfunc.items())))
(combined/'calls.tsv').write_text('caller\tcallee\n'+''.join(f'{a:#x}\t{b:#x}\n' for a,b in sorted(calls)))
(combined/'imports.tsv').write_text('stub\tname\n'+''.join(f'{a:#x}\t{n}\n' for a,n in sorted(set(imports))))
(combined/'offsets.txt').write_text(''.join(f'{a:#x}\n' for a in sorted(allfunc)))
raw=(out/'boot-linked.bin').read_bytes();segments,image=read_boot_linked(raw);entry=struct.unpack_from('<Q',raw,16)[0]
eh=struct.pack('<16sHHIQQQIHHHHHH',b'\x7fELF\x02\x01\x01'.ljust(16,b'\0'),3,62,1,entry,64,0,0,64,56,len(segments),0,0,0)
ph=b''.join(struct.pack('<II6Q',1,flags,16384+a,a,0,z,z,16384) for a,z,flags in segments)
(combined/'linked.elf').write_bytes((eh+ph).ljust(16384,b'\0')+image)
print('Combined function candidates:',len(allfunc),flush=True)
if scan_only:
 sys.exit(0)
# Remove only this tool's generated sources; obsolete windows must not be linked.
for stale in combined.glob('recomp*.c'):stale.unlink()
env=dict(os.environ,BB_JIT='0',GOW3_AOT_INCLUDE_STUBS='1',GOW3_AOT_GUARDED_TABLES='1')
subprocess.run([tool/'gow3-aot-recomp',combined/'linked.elf',combined/'functions.tsv',combined/'recomp.c','@'+str(combined/'offsets.txt')],env=env,check=True)

# Surface all static gaps together, before an expensive native build/boot cycle.
with (combined/'coverage-audit.log').open('w') as report:
 subprocess.run([sys.executable,repo/'packaging/macos/aot/audit_coverage.py',combined],stdout=report,check=True)
print('Static gap inventory:',combined/'coverage-summary.json',flush=True)
