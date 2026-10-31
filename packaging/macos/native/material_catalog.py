#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Plan bounded shader/pipeline preparation from WAD material dependencies.

No code is copied. Only exact shader ABI headers may borrow a resource/render
recipe. The native importer rechecks header bytes and rebuilds with the real code;
resource-walk or specialization differences reject the candidate. Live draws
still validate their current state and never use an incomplete pipeline.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import mmap
from pathlib import Path
import re
import struct
import time

VERSION = 2
MASK = (1 << 64)-1

def combine(value, index):
    return (value ^ ((index+0x9e3779b9+(value << 12)+(value >> 4)) & MASK)) & MASK


def records(data):
    offset = 0
    while offset+64 <= len(data):
        tag, flags, size = struct.unpack_from('<HHI', data, offset)
        if offset+64+size > len(data):
            raise ValueError('Truncated WAD record')
        name = data[offset+8:offset+64].split(b'\0')[0].decode('latin1')
        # Scene WADs store a count here, not a payload byte length.
        if tag == 0 and name == 'EntityCount':
            size = 0
        yield offset+64, size, tag, name
        offset += 64+((size+15) & ~15)
    if offset != len(data):
        raise ValueError('Trailing WAD bytes')


def package_stages(data, start, size, family):
    if size < 160 or struct.unpack_from('<I', data, start)[0] != 0x10008:
        return None
    if struct.unpack_from('<I', data, start+60)[0] != family:
        return None
    vs_size, fs_size = struct.unpack_from('<II', data, start+48)
    prefix = size-vs_size-fs_size
    if prefix < 96 or min(vs_size, fs_size) < 64:
        return None
    result = []
    for stage, base, length in [(1,start+prefix,vs_size),(0,start+prefix+vs_size,fs_size)]:
        code_offset = struct.unpack_from('<I', data, base+8)[0]
        if not 64 <= code_offset <= 4096 or code_offset+8 > length:
            return None
        code = base+code_offset
        if struct.unpack_from('<I', data, code)[0] != 0xbeeb03ff:
            return None
        footer = code+(struct.unpack_from('<I', data, code+4)[0]+1)*8
        if footer+28 > base+length or data[footer:footer+8] != b'OrbShdr\x07':
            return None
        flags = struct.unpack_from('<I', data, footer+8)[0]
        if ((flags >> 2) & 15) != stage or flags >> 8 > footer-code:
            return None
        raw_hash = struct.unpack_from('<Q', data, footer+16)[0]
        # Skip relocatable code addresses/length. Preserve ALL register, usage,
        # vertex-input and stage-interface bytes, including allocation settings.
        header = bytes(data[base+16:code])
        result.append((raw_hash, code, base+16, len(header), hashlib.sha256(header).hexdigest()))
    return result


def build(game, catalog, output, max_pipelines=4096, progress=None):
    started = time.monotonic()
    game, catalog, output = map(Path, (game,catalog,output))
    index_path = output/'programs.tsv'
    index_digest = hashlib.sha256(index_path.read_bytes()).hexdigest()
    manifest_digest = hashlib.sha256((catalog/'manifest.json').read_bytes()).hexdigest()
    fingerprint = hashlib.sha256(f'{VERSION}:{index_digest}:{manifest_digest}:{max_pipelines}'.encode()).hexdigest()
    report_path = output/'materials.json'
    if report_path.exists():
        old = json.loads(report_path.read_text())
        if old.get('fingerprint') == fingerprint and all((output/name).is_file() and hashlib.sha256((output/name).read_bytes()).hexdigest()==digest for name,digest in old['files'].items()):
            return {**old, 'reused': True, 'seconds': time.monotonic()-started}
    programs = {}
    for line in index_path.read_text().splitlines():
        raw, offset, length, stage, digest, path = line.split('\t')
        programs[int(raw,16)] = (path,int(offset),int(length),int(stage),digest)
    headers = {}; packages = []; materials = defaultdict(set); counts = Counter()
    files = sorted(p for p in (game/'EXEC/WAD/PS4_LE').glob('*.WAD') if not p.name.startswith('.'))
    for number,path in enumerate(files,1):
        relative = str(path.relative_to(game))
        with path.open('rb') as stream, mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ) as data:
            try:
                for start,size,tag,name in records(data):
                    if tag != 1 or not size:
                        continue
                    if name.startswith('MAT_') and size >= 128 and struct.unpack_from('<I',data,start)[0] == 8:
                        materials[relative].add(struct.unpack_from('<I',data,start+124)[0]);counts['materials']+=1
                    if not re.fullmatch('[0-9A-F]{8}_[0-9A-F]{8}',name):
                        continue
                    family = int(name[:8],16)
                    stages = package_stages(data,start,size,family)
                    if not stages:
                        continue
                    vs,fs = stages
                    if vs[0] not in programs or fs[0] not in programs:
                        continue
                    packages.append((relative,family,vs[0],fs[0],vs[4],fs[4]))
                    for raw,code,offset,length,digest in stages:
                        if programs[raw][0] == relative and programs[raw][1] == code:
                            headers[raw] = (offset,length,digest)
            except ValueError:
                counts['invalid_wads']+=1
        if progress: progress(number,len(files))
    global_families = set().union(*materials.values())
    recipes = {}; stage_ids = {}; occupied = set()
    known_code = set()
    for path in sorted(catalog.glob('*.recipe')):
        data = path.read_bytes();raw=struct.unpack_from('<Q',data,16)[0];permutation=struct.unpack_from('<Q',data,32)[0]
        if raw not in programs or raw not in headers:continue
        token = combine(raw,permutation);stage_ids[token]=(raw,path.name);occupied.add(token);known_code.add(programs[raw][4])
    pso_roles=defaultdict(list)
    for path in sorted(catalog.glob('*.pso')):
        data=path.read_bytes();ids=struct.unpack_from('<6Q',data,12)
        if not ids[0] or not ids[3] or any(ids[i] for i in [1,2,4,5]) or ids[0] not in stage_ids or ids[3] not in stage_ids:continue
        fs,vs=stage_ids[ids[0]],stage_ids[ids[3]]
        pso_roles[headers[vs[0]][2],headers[fs[0]][2]].append((path.name,vs,fs,ids[3],ids[0]))
    by_wad=defaultdict(list);unique=set()
    for wad,family,vs,fs,vs_sig,fs_sig in packages:
        if family not in global_families or vs not in headers or fs not in headers:continue
        if headers[vs][2]!=vs_sig or headers[fs][2]!=fs_sig:continue
        counts['material_packages']+=1
        if programs[vs][4] in known_code and programs[fs][4] in known_code:continue
        key=(programs[vs][4],programs[fs][4],vs_sig,fs_sig)
        if key in unique:continue
        unique.add(key)
        roles=pso_roles.get((vs_sig,fs_sig),[])
        if not roles:counts['unmatched_new_package_pairs']+=1;continue
        for role in roles:by_wad[wad].append((vs,fs,role))
    stage_lines={};pipeline_lines=[];wad_coverage=Counter();pairs=set()
    # Round-robin gives different resource packages a budget instead of filling
    # it exclusively with the first scene in alphabetical/file order.
    while by_wad and len(pipeline_lines)<max_pipelines:
        for wad in sorted(list(by_wad)):
            vs,fs,role=by_wad[wad].pop(0)
            if not by_wad[wad]:del by_wad[wad]
            name,source_vs,source_fs,vs_id,fs_id=role;tokens=[]
            for target,(source,recipe),old in [(vs,source_vs,vs_id),(fs,source_fs,fs_id)]:
                if target==source:tokens.append(old);continue
                identity=f'{target:016x}:{recipe}'
                token=int.from_bytes(hashlib.sha256(('material:'+identity).encode()).digest()[:8],'little')
                if token in occupied and identity not in stage_lines:raise ValueError('Material stage token collision')
                occupied.add(token);stage_lines[identity]=(token,target,recipe);tokens.append(token)
            identity=(name,*tokens)
            if identity not in pairs:
                pairs.add(identity);pipeline_lines.append(identity);wad_coverage[Path(wad).name]+=1
            if len(pipeline_lines)>=max_pipelines:break
    files_out={
        'material-headers.tsv': ''.join(f'{raw:016x}\t{off}\t{length}\t{digest}\n' for raw,(off,length,digest) in sorted(headers.items())),
        'material-stages.tsv': ''.join(f'{token:016x}\t{target:016x}\t{recipe}\n' for token,target,recipe in sorted(stage_lines.values())),
        'material-pipelines.tsv': ''.join(f'{name}\t{vs:016x}\t{fs:016x}\n' for name,vs,fs in pipeline_lines)}
    for name,text in files_out.items():
        tmp=output/(name+'.tmp');tmp.write_text(text);tmp.replace(output/name)
    report=dict(version=VERSION,fingerprint=fingerprint,files={name:hashlib.sha256(text.encode()).hexdigest() for name,text in files_out.items()},
        counters=dict(counts),header_programs=len(headers),source_pipeline_roles=len(pso_roles),
        planned_shader_variants=len(stage_lines),planned_pipelines=len(pipeline_lines),resource_packages=dict(sorted(wad_coverage.items())),
        remaining_budget_candidates=sum(map(len,by_wad.values())),seconds=time.monotonic()-started,reused=False,
        new_code_only=True,complete_campaign=False,contains_shader_code=False)
    tmp=report_path.with_suffix('.json.tmp');tmp.write_text(json.dumps(report,indent=2)+'\n');tmp.replace(report_path)
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',type=Path,required=True);p.add_argument('--catalog',type=Path,required=True);p.add_argument('--index',type=Path,required=True);p.add_argument('--max-pipelines',type=int,default=4096)
    a=p.parse_args();result=build(a.game,a.catalog,a.index,a.max_pipelines);print(json.dumps(result,indent=2))
