#!/usr/bin/env python3
"""Create one opt-in 2x color replacement from the user's DDS and restored PNG.

Developer tool: requires Pillow + NumPy on native ARM64 Python. Never edits a dump.
No game-derived input or output belongs in Git or release downloads.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct

def create(dds: bytes, restored: Path) -> bytes:
    import numpy as np
    from PIL import Image
    if len(dds)<128 or dds[:4]!=b'DDS ' or struct.unpack_from('<I',dds,4)[0]!=124:
        raise ValueError('Expected a legacy DDS')
    height,width=struct.unpack_from('<II',dds,12)
    fourcc=dds[84:88]
    if fourcc not in (b'DXT1',b'DXT5') or struct.unpack_from('<I',dds,112)[0]&0x200:
        raise ValueError('Only BC1/BC3 2D color maps are supported')
    if min(width,height)<32 or max(width,height)>2048:
        raise ValueError('Source dimensions outside bounded experiment')
    block=8 if fourcc==b'DXT1' else 16
    size=((width+3)//4)*((height+3)//4)*block
    if len(dds)<128+size: raise ValueError('Truncated DDS')
    original=Image.open(io.BytesIO(dds)).convert('RGBA')
    image=Image.open(restored).convert('RGB')
    if image.width<width*2 or image.height<height*2 or image.width*height!=image.height*width:
        raise ValueError('Restoration must preserve UV aspect and provide at least 2x dimensions')
    image=image.resize((width*2,height*2),Image.Resampling.LANCZOS).convert('RGBA')
    # Never invent alpha with the color restoration model.
    image.putalpha(original.getchannel('A').resize(image.size,Image.Resampling.LANCZOS))
    levels=[]
    while True:
        levels.append(image.tobytes())
        if image.size==(1,1):break
        # Premultiplied-alpha mip filtering avoids dark fringes around cutout textures.
        pixels=np.asarray(image,dtype=np.float32)/255
        pixels[:,:,:3]*=pixels[:,:,3:4]
        size_next=(max(1,image.width//2),max(1,image.height//2))
        channels=[np.asarray(Image.fromarray(pixels[:,:,i]).resize(size_next,Image.Resampling.BOX)) for i in range(4)]
        reduced=np.stack(channels,axis=2)
        reduced[:,:,:3]/=np.maximum(reduced[:,:,3:4],1e-8)
        image=Image.fromarray(np.uint8(np.clip(reduced*255+.5,0,255)))
    header=b'G3TXv001'+struct.pack('<8I',width,height,block,len(dds[128:128+size]),width*2,height*2,len(levels),1)
    return header+dds[128:128+size]+b''.join(levels)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('original_dds',type=Path);p.add_argument('restored_png',type=Path)
    p.add_argument('output',type=Path);a=p.parse_args()
    data=create(a.original_dds.read_bytes(),a.restored_png)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_bytes(data)
    manifest_path = a.output.parent / 'manifest.json'
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {'version':1,'files':{}}
    manifest['files'][a.output.name] = hashlib.sha256(data).hexdigest()
    manifest_path.write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
    print(f'{a.output.name}: {len(data)/1048576:.2f} MiB, exact original BC base + 2x RGBA mip chain')
if __name__=='__main__':main()
