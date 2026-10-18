"""Real loader tests for hoisted relative jump tables and checked-index aliases.
All machine code is hand-authored; no game data. Native execution must fail closed
for a clobbered index or a partially invalid table.
"""
from pathlib import Path
import os,struct,subprocess,sys
exe,gen,src,inc,compiler,directory=sys.argv[1:]
root=Path(directory);root.mkdir(parents=True,exist_ok=True)
for selector,bad in [(0,''),(1,''),(2,''),(0,'clobber'),(0,'invalid-table'),(0,'jbe'),(1,'jbe'),(2,'jbe'),(0,'jbe-clobber'),(0,'flags-clobber'),(0,'narrow'),(1,'narrow'),(2,'narrow'),(0,'narrow-unsafe'),(0,'mask'),(3,'mask'),(0,'mask-clobber'),(0,'mask-narrow'),(0,'cfg-loop'),(1,'cfg-loop'),(0,'cfg-clobber'),(0,'cfg-volatile'),(0,'narrow-guarded'),(1,'narrow-guarded'),(2,'narrow-guarded'),(0,'byte-alias'),(1,'byte-alias'),(2,'byte-alias'),(0,'byte-alias-clobber'),(0,'guarded-spill'),(1,'guarded-spill'),(2,'guarded-spill'),(0,'guarded-high'),(0,'guarded-invalid-table'),(0,'cfg-indirect'),(1,'cfg-indirect'),(0,'overlap'),(0,'overlap-taken')]:
 out=root/(str(selector)+'-'+(bad or 'valid'));out.mkdir(exist_ok=True)
 code=bytearray();labels={};fixups=[]
 def label(name):labels[name]=len(code)
 def emit(h):code.extend(bytes.fromhex(h))
 def relative(h,target):
  emit(h);fixups.append((len(code),target));code.extend(b'\0'*4)
 if bad.startswith('overlap'):
  emit('b80100000085c0' if bad=='overlap-taken' else '31c0')
  relative('0f85','interior')
  emit('b8');label('interior');emit('0f0b9090')
 if not bad.startswith('jbe'):
  relative('488d05','table');emit('4989c5')  # hoisted table base copied into r13
  emit('90'*20)
 if bad=='cfg-indirect':
  relative('4c8d25','first-table');emit('31c94883e1004963048c4c01e0ffe0')
  label('first-target')
 if bad.startswith('cfg'):
  # r13 constant survives a loop and a returning native import. A caller-saved
  # alias must not survive that call, and a second definition must be rejected.
  relative('e9','loop-check')
  label('loop-back')
  if bad=='cfg-clobber':emit('4d31ed')
  relative('e9','loop-check')
  label('loop-check')
  emit('31c0');relative('0f85','loop-back')
  if bad=='cfg-volatile':emit('4d89eb') # mov r11,r13
  emit('4883ec08');relative('ff15','slot0');emit('4883c408')
  if bad=='cfg-volatile':emit('4d89dd') # mov r13,r11 after volatile call
 emit('b8');code.extend(struct.pack('<I',0x10000 if bad=='narrow-unsafe' else selector))
 emit('0fb6c8' if bad.startswith('byte-alias') else '89c1')  # copy index before the check, with unrelated instructions in between
 if bad.startswith('guarded'):emit('48894c24f8')
 emit('41ba0400000041c1e201909090')
 if bad.startswith('jbe'):
  emit('83f901');relative('0f86','dispatch');relative('e9','default')
  label('dispatch');relative('488d05','table');emit('4989c5')
 elif not bad.startswith('mask'):
  if bad.startswith('narrow'):
   if bad=='narrow':emit('0fb7c9')  # zero-extend cx before comparing its low word
   emit('6683f901')
  elif bad.startswith('byte-alias'):emit('3c01')
  else:emit('83f801')
  emit('bb78563412')  # mov ebx,imm preserves CMP flags
  if bad=='flags-clobber':emit('85c9')
  relative('0f87','default')
 emit('b802000000')  # changing the compared register does not change the copied index
 if bad.startswith('mask'):
  emit('6683e101' if bad=='mask-narrow' else '4883e101')
  if bad=='mask-clobber':emit('31c9')
 if bad in ('clobber','jbe-clobber','byte-alias-clobber'):emit('31c9')
 if bad=='guarded-high':emit('48c74424f800000100')
 if bad.startswith('guarded'):emit('488b4c24f8')
 emit('49634c8d004c01e941ba01000000ffe1') # movsxd rcx,[r13+rcx*4]; add rcx,r13; jmp rcx
 for i,name in enumerate(['case0','case1','default']):
  label(name);emit('4883ec08');relative('ff15','slot0');emit('4883c408');relative('ff25','slot'+str(i+1))
 label('table')
 for name in ['case0','case1']:
  code.extend(struct.pack('<i',(len(code)+0x10000 if bad.endswith('invalid-table') and name=='case1' else labels[name])-labels['table']))
 if bad=='cfg-indirect':
  label('first-table');code.extend(struct.pack('<i',labels['first-target']-labels['first-table']))
 for i in range(4):labels['slot'+str(i)]=512+8*i
 for at,name in fixups:struct.pack_into('<i',code,at,labels[name]-(at+4))
 image=bytes(code).ljust(16384,b'\0')
 boot=struct.pack('<8s6Q',b'G3PROBE2',len(image),0,1,4,4,1)+struct.pack('<3Q',0,len(image),5)
 imports=['bzQExy189ZI#libc','table-case0','table-case1','table-default']
 boot+=b''.join(x.encode().ljust(128,b'\0') for x in imports)
 boot+=b''.join(struct.pack('<4Q',512+8*i,1,i,0) for i in range(4))+image
 (out/'boot.bin').write_bytes(boot)
 eh=struct.pack('<16sHHIQQQIHHHHHH',b'\x7fELF\x02\x01\x01'.ljust(16,b'\0'),3,62,1,0,64,0,0,64,56,1,0,0,0)
 ph=struct.pack('<II6Q',1,5,16384,0,0,len(image),len(image),16384)
 (out/'fixture.elf').write_bytes((eh+ph).ljust(16384,b'\0')+image)
 (out/'functions.tsv').write_text('offset\tsize\n0x0\t'+str(len(code))+'\n')
 env=dict(os.environ,BB_JIT='0')
 env.pop('GOW3_AOT_GUARDED_TABLES',None)
 if bad.startswith('guarded'):env['GOW3_AOT_GUARDED_TABLES']='1'
 subprocess.run([gen,str(out/'fixture.elf'),str(out/'functions.tsv'),str(out/'recomp.c'),'0'],env=env,check=True)
 subprocess.run([compiler,'-arch','arm64','-O2','-dynamiclib','-I'+str(Path(src)/'src'),'-I'+inc,*map(str,out.glob('recomp*.c')),'-o',str(out/'libtable.dylib')],check=True)
 env['GOW3_RECOMP_LIB']=str((out/'libtable.dylib').resolve())
 r=subprocess.run([exe,str(out/'boot.bin'),'--cpu-only'],env=env,capture_output=True,text=True,timeout=10)
 print(r.stdout+r.stderr)
 if bad in ('clobber','jbe-clobber','invalid-table','flags-clobber','narrow-unsafe','mask-clobber','mask-narrow','cfg-clobber','cfg-volatile','byte-alias-clobber','guarded-high','guarded-invalid-table','overlap-taken'):
  assert r.returncode==30,(bad,r.returncode)
  assert ('interpreted instruction disabled' if bad=='overlap-taken' else 'function not recompiled') in r.stderr
 else:
  assert r.returncode==20,r.returncode
  assert 'first unsupported PS4 import: '+imports[(selector&1 if bad=='mask' else selector)+1] in r.stdout
  assert ('_init_env=2' if bad in ('cfg-loop','cfg-indirect') else '_init_env=1') in r.stdout and '0 instructions interpreted from translated code' in r.stdout
 print('PASS:',selector,bad or 'bounded native table dispatch',flush=True)
