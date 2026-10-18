#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Local-only AOT profiles for the desktop launcher. Never distribute game-derived output.

All variants are generated from their patched loader image, using the same static
function inventory: only immediate operands/data change, never control flow.
Unchanged generated C reuses an object compiled from identical bytes/headers.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
from session import RESOLUTIONS, SCALES, plan

p = argparse.ArgumentParser()
p.add_argument('--game', required=True, type=Path)
p.add_argument('--prepared', required=True, type=Path, help='Existing prepared 1080p AOT tree')
p.add_argument('--recompiler', required=True, type=Path)
p.add_argument('--recomp-source', required=True, type=Path)
p.add_argument('--decoder-include', required=True, type=Path)
p.add_argument('--out', required=True, type=Path)
p.add_argument('--reuse', action='append', default=[], type=Path, help='Retain a validated AOT library only when its sealed loader image is byte-identical')
a = p.parse_args()
repo = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(repo / 'scripts'))
sys.path.insert(0, str(repo / 'packaging/macos/native'))
from patches import compile_patches, eboot_segments, eboot_image, game_profile_for
from link_modules import module
from isa_rewrite import read_boot_linked
from aot_patches import apply_file_writes

a.out.mkdir(parents=True, exist_ok=True)
original = module(a.game / 'eboot.bin')['elf']
segments = eboot_segments(original)
image = eboot_image(original)
profile = game_profile_for(a.game)
if not profile:
    raise ValueError('Unsupported executable')
xml = ET.parse(profile[1])
base = (a.prepared / 'boot-linked.bin').read_bytes()
_, linked_image = read_boot_linked(base)
offset = len(base) - len(linked_image)
raw = bytearray(base)
oldnames = json.loads((a.prepared / 'aot-patches.json').read_text())['names']
for at, data in compile_patches(profile[1], oldnames, profile[2], segments, image):
    raw[offset+at:offset+at+len(data)] = image[at:at+len(data)]
base = bytes(raw)
selections = {x['profile']: x for r in RESOLUTIONS for u in SCALES for cap in ('60','0')
              for x in (plan(r,u,cap),)}
template = next(x for x in xml.getroot().findall('Metadata') if x.get('Name') == 'Resolution Patch - 4K')
known = {(1920,1080): 'native', (1280,720): '720p', (2560,1440): '1440p', (3840,2160): '4K'}
for size in sorted({tuple(x['input']) for x in selections.values()}):
    if size in known:
        continue
    w,h = size
    entry = ET.fromstring(ET.tostring(template))
    entry.set('Name', f'Resolution Patch - {w}x{h}')
    entry.set('Author', 'kvicken + cuesta4; launcher preset adaptation')
    # Retain the validated 4K arena for all presets; do not risk texture eviction.
    entry.set('Note', 'Exact desktop input size. Requires setting DMEM to 6144 MB or more. One resolution patch only.')
    for line in entry.find('PatchList'):
        data = bytes.fromhex(line.get('Value'))
        value = int.from_bytes(data, 'little')
        replacement = {3840:w, 2160:h, 1920:w//2, 1080:h//2}.get(value)
        if replacement is not None:
            line.set('Value', replacement.to_bytes(len(data), 'little').hex())
    xml.getroot().append(entry)
    known[size] = f'{w}x{h}'
xmlfile = a.out / 'God_of_War_III_Remastered.xml'
xml.write(xmlfile, encoding='utf-8', xml_declaration=True)
cache = a.out / 'objects'
cache.mkdir(exist_ok=True)
flags = ['-arch','arm64','-O2','-std=gnu11','-fPIC','-ffp-contract=off','-fno-strict-aliasing',
         '-I'+str(a.recomp_source.resolve()/'src'), '-I'+str(a.decoder_include.resolve())]
headers = b''.join(x.read_bytes() for x in sorted((a.recomp_source/'src').rglob('*.h')))
compiler = subprocess.check_output(['/usr/bin/clang','--version'])
generator_sha256 = hashlib.sha256(a.recompiler.read_bytes() + headers + Path(__file__).read_bytes()).hexdigest()

def compile_one(path):
    digest = hashlib.sha256(compiler+headers+json.dumps(flags).encode()+path.read_bytes()).hexdigest()
    obj = cache/(digest+'.o')
    if not obj.exists():
        subprocess.run(['/usr/bin/clang',*flags,'-c',str(path),'-o',str(obj)], check=True)
    return obj

def reuse_checkpoint(out):
    stamp = out/'profile.json'
    metadata = json.loads(stamp.read_text())
    expected = hashlib.sha256((out/'boot-linked.bin').read_bytes()).digest()
    for candidate in a.reuse:
        candidate_manifest = candidate/'profile.json'
        if not candidate_manifest.exists() or json.loads(candidate_manifest.read_text()).get('generator_sha256') != generator_sha256:
            continue
        if hashlib.sha256((candidate/'boot-linked.bin').read_bytes()).digest() != expected:
            continue
        from native_launch import require_arm64
        library = candidate/'aot/libgow3-recompiled.dylib'
        require_arm64(library)
        shutil.copy2(library,out/'aot/libgow3-recompiled.dylib')
        metadata.update(reused_checkpoint_library=True,library_sha256=hashlib.sha256(library.read_bytes()).hexdigest())
        stamp.write_text(json.dumps(metadata,indent=2))
        print('Retained byte-identical prepared checkpoint',out.name,flush=True)
        break

for key, selection in sorted(selections.items()):
    out = a.out / key
    stamp = out / 'profile.json'
    if stamp.exists() and json.loads(stamp.read_text()).get('generator_sha256') == generator_sha256:
        reuse_checkpoint(out)
        print('Existing',key,flush=True)
        continue
    out.mkdir(exist_ok=True)
    for name in ('analysis.json','link.json'):
        shutil.copy2(a.prepared/name,out/name)
    resolution = known[tuple(selection['input'])]
    names = ['Skip Any Video With X Button']
    names += ['Bug Fix - Texture Corruption Fix'] if resolution == 'native' else ['Resolution Patch - '+resolution]
    if selection['engine'] != 60:
        names += [f"Frame Rate Patch - {selection['engine']} FPS"]
    writes = compile_patches(xmlfile,names,profile[2],segments,image)
    _, boot = apply_file_writes(original,base,writes,read_boot_linked)
    (out/'boot-linked.bin').write_bytes(boot)
    digest = hashlib.sha256(boot).hexdigest()
    (out/'native-inputs.json').write_text(json.dumps({'boot_sha256':digest}))
    (out/'aot-patches.json').write_text(json.dumps({'names':names,'boot_sha256':digest,'writes':len(writes)}))
    generated = out/'aot'
    generated.mkdir(exist_ok=True)
    for name in ('functions.tsv','offsets.txt'):
        shutil.copy2(a.prepared/'aot'/name,generated/name)
    segs, linked = read_boot_linked(boot)
    entry = struct.unpack_from('<Q',boot,16)[0]
    eh = struct.pack('<16sHHIQQQIHHHHHH',b'\x7fELF\x02\x01\x01'.ljust(16,b'\0'),3,62,1,entry,64,0,0,64,56,len(segs),0,0,0)
    ph = b''.join(struct.pack('<II6Q',1,f,16384+at,at,0,n,n,16384) for at,n,f in segs)
    elf = generated/'linked.elf'
    elf.write_bytes((eh+ph).ljust(16384,b'\0')+linked)
    print('Generating',key,flush=True)
    with (out/'generation.log').open('w') as log:
        subprocess.run([str(a.recompiler.resolve()),str(elf),str(generated/'functions.tsv'),str(generated/'recomp.c'),
                       '@'+str((generated/'offsets.txt').resolve())],
            env={**os.environ,'BB_JIT':'0','GOW3_AOT_INCLUDE_STUBS':'1','GOW3_AOT_GUARDED_TABLES':'1'},
            stdout=log,stderr=subprocess.STDOUT,check=True)
    # Preserve the checkpoint's generated-code policy. Newer tools can emit
    # optional polling instrumentation; the desktop app does not enable it.
    # Remove only that exact generated spelling before compiling any object.
    for source in generated.glob('recomp*.c'):
        text = source.read_text().replace('    unsigned poll_spins = 0;\n', '')
        text = re.sub(r' \{ rc_poll_wait\(&poll_spins\); goto (L_[0-9a-f]+); \} else \{ poll_spins = 0; \}', r' goto \1;', text)
        if 'poll_spins' in text or 'rc_poll_wait(' in text:
            raise ValueError('Unknown poll-hook generator format')
        source.write_text(text)
    with ThreadPoolExecutor(max_workers=6) as pool:
        objects = list(pool.map(compile_one, sorted(generated.glob('recomp*.c'))))
    library = generated/'libgow3-recompiled.dylib'
    subprocess.run(['/usr/bin/clang','-arch','arm64','-dynamiclib',*map(str,objects),'-o',str(library)],check=True)
    stamp.write_text(json.dumps({**selection,'resolution_patch':resolution,'patches':names,
        'generator_policy':'checkpoint-no-poll-hooks-v1', 'generator_sha256':generator_sha256,
        'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest()},indent=2))
    reuse_checkpoint(out)
    print('Built',key,flush=True)
