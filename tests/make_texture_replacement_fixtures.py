#!/usr/bin/env python3
"""Generate synthetic fixtures only; no game data. ASTC fixtures require Pillow + astcenc."""
from pathlib import Path
import argparse, struct, subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path);p.add_argument('--astc-encoder',type=Path)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
legacy=a.output/'legacy';legacy.mkdir(exist_ok=True)
rgba=b''.join(bytes([85])*(w*w*4) for w in (64,32,16,8,4,2,1))
(legacy/'synthetic.g3tx').write_bytes(b'G3TXv001'+struct.pack('<8I',32,32,8,512,64,64,7,1)+bytes([85])*512+rgba)
if a.astc_encoder:
 from PIL import Image
 for name,layers,block in [('astc',1,8),('cube',6,8),('rgba',1,4)]:
  target=a.output/name;target.mkdir(exist_ok=True);parts=[]
  for w in (64,32,16,8,4,2,1):
   image=target/'input.tga';encoded=target/'output.astc'
   Image.new('RGBA',(w,w),(85,85,85,85)).save(image)
   subprocess.run([str(a.astc_encoder.resolve()),'-cl',str(image),str(encoded),'4x4','-medium','-silent'],check=True)
   parts.append(encoded.read_bytes()[16:]*layers)
  original=bytes([85])*((4096 if block==4 else 512)*layers);payload=b''.join(parts)
  header=b'G3TXv002'+struct.pack('<IIIIQIIIIQQ',32,32,block,layers,len(original),64,64,7,2,len(payload),0)
  (target/'synthetic.g3tx').write_bytes(header+original+payload)
  image.unlink();encoded.unlink()
print(a.output)
