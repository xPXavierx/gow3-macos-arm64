#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inventory embedded BC1/BC3 DDS resources without extracting/modifying game data.

Looks for aligned, named texture resource headers directly. Other WAD record
types have different headers; treating all records as length-prefixed misparses
sound/entity tables. This inventories recognized DDS resources, not every game
asset or all container formats. Output is metadata only; keep it outside sources.
"""
import argparse
import collections
import json
import mmap
from pathlib import Path
import re
import struct

RESOURCE = re.compile(rb'\x01\x00\x0d\x00.{4}(TX[0-9A-Fa-f]{8}\.[0-9A-Fa-f]{8}\.dds)\x00', re.DOTALL)


def inventory(path):
    rows, rejected = [], []
    if not path.stat().st_size:
        return rows, rejected
    with path.open('rb') as file, mmap.mmap(file.fileno(),0,access=mmap.ACCESS_READ) as data:
        for match in RESOURCE.finditer(data):
            offset = match.start()
            if offset % 16:
                continue
            size = struct.unpack_from('<I',data,offset+4)[0]
            name = match.group(1).decode('ascii')
            end = offset+64+size
            reason = None
            if end > len(data) or size < 128:
                reason = 'resource payload outside archive or too short'
            else:
                start = data.find(b'DDS ',offset+64,min(end,offset+64+2048))
                if start < 0 or start+128 > end:
                    reason = 'DDS header not found in resource prefix'
                elif struct.unpack_from('<I',data,start+4)[0] != 124:
                    reason = 'invalid DDS header size'
                else:
                    height,width = struct.unpack_from('<II',data,start+12)
                    mips = max(1,struct.unpack_from('<I',data,start+28)[0])
                    fourcc = data[start+84:start+88]
                    caps2 = struct.unpack_from('<I',data,start+112)[0]
                    if not width or not height or width > 32768 or height > 32768 or mips > max(width,height).bit_length():
                        reason = 'invalid dimensions or mip count'
                    elif fourcc not in (b'DXT1',b'DXT5'):
                        reason = 'unsupported DDS format: '+repr(fourcc)
                    else:
                        block = 8 if fourcc == b'DXT1' else 16
                        faces = (caps2 & 0xfc00).bit_count() if caps2 & 0x200 else 1
                        expected = faces*sum(max(1,((width>>m)+3)//4)*max(1,((height>>m)+3)//4)*block for m in range(mips))
                        if not faces or end-start-128 < expected:
                            reason = 'truncated DDS mip/face payload'
                        else:
                            rows.append(dict(wad=path.name,offset=offset,name=name,width=width,height=height,
                                             mip_count=mips,faces=faces,fourcc=fourcc.decode('ascii'),expected_bytes=expected))
            if reason:
                rejected.append(dict(wad=path.name,offset=offset,name=name,reason=reason))
    return rows,rejected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump',type=Path,help='Extracted game root or EXEC/WAD/PS4_LE directory')
    parser.add_argument('--out',required=True,type=Path,help='Private metadata directory outside the source checkout')
    args = parser.parse_args()
    directory = args.dump/'EXEC/WAD/PS4_LE' if (args.dump/'EXEC/WAD/PS4_LE').is_dir() else args.dump
    archives = sorted(directory.glob('*.WAD'))
    if not archives:
        parser.error('No WAD archives found')
    rows,rejected = [],[]
    for i,path in enumerate(archives,1):
        found,bad = inventory(path);rows.extend(found);rejected.extend(bad)
        if i % 100 == 0:print(f'{i}/{len(archives)} archives; {len(rows)} recognized DDS resources',flush=True)
    sizes = collections.Counter(f"{r['width']}x{r['height']}" for r in rows)
    summary = dict(archives_scanned=len(archives),archive_bytes=sum(p.stat().st_size for p in archives),
                   recognized_bc1_bc3_resources=len(rows),distinct_resource_names=len({r['name'] for r in rows}),
                   dimensions=dict(sizes.most_common()),rejected_candidates=len(rejected),dump_modified=False,
                   scope='Recognized aligned named BC1/BC3 DDS resources only; no pixels decoded, no global integrity certificate',
                   two_x_texture_texel_multiplier=4)
    args.out.mkdir(parents=True,exist_ok=True)
    (args.out/'textures.json').write_text(json.dumps(rows,indent=2)+'\n')
    (args.out/'rejected.json').write_text(json.dumps(rejected,indent=2)+'\n')
    (args.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary),flush=True)


if __name__ == '__main__':
    main()
