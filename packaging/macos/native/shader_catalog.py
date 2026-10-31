#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Index bounded GCN programs in the player's dump; never copy shader code.

The index alone is NOT a pipeline catalogue: render state and resource/fetch
specializations are separate inputs. Identical bytecode may have different IDs.
"""
import argparse
from collections import Counter
import hashlib
import json
import mmap
from pathlib import Path
import struct
import time

MAGIC = b"OrbShdr"
TOKEN = b"\xff\x03\xeb\xbe"
VERSION = 2


def programs(data):
    """Yield only a validated header/code/footer relationship, at any alignment."""
    end = 0
    while (footer := data.find(MAGIC, end)) >= 0:
        end = footer + len(MAGIC)
        if footer + 28 > len(data) or data[footer + 7] != 7:
            continue
        flags = struct.unpack_from('<I', data, footer + 8)[0]
        length = flags >> 8
        stage = (flags >> 2) & 15
        if stage not in (0, 1, 2, 3, 4, 5) or length < 8 or length > 65536 or length % 4:
            continue
        # Input-usage records can separate the actual code and the footer.
        # Do not assume footer - code length is the beginning of the program.
        code = footer
        while (code := data.rfind(TOKEN, max(0, footer - 65536), code)) >= 0:
            distance = (struct.unpack_from('<I', data, code + 4)[0] + 1) * 8
            if code + distance != footer or code + length > footer:
                continue
            shader_hash = struct.unpack_from('<Q', data, footer + 16)[0]
            if shader_hash:
                yield shader_hash, code, length, stage, hashlib.sha256(data[code:code+length]).hexdigest()
            break


def build(game, output, progress=None):
    game = game.resolve(strict=True)
    files = sorted(p for p in (game / 'EXEC/WAD/PS4_LE').glob('*.WAD') if not p.name.startswith('.'))
    if not files:
        raise ValueError('No PS4 WAD files found in the selected dump')
    started = time.monotonic()
    sources = [(str(p.relative_to(game)), p.stat().st_size, p.stat().st_mtime_ns) for p in files]
    fingerprint = hashlib.sha256(json.dumps([VERSION, sources]).encode()).hexdigest()
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / 'catalog.json'
    if report_path.exists() and (output / 'programs.tsv').exists():
        try:
            report = json.loads(report_path.read_text())
            index_hash = hashlib.sha256((output / 'programs.tsv').read_bytes()).hexdigest()
            if report.get('fingerprint') == fingerprint and report.get('index_sha256') == index_hash:
                return {**report, 'reused': True, 'seconds': time.monotonic() - started}
        except (ValueError, OSError):
            pass
    entries = {}
    groups = {}
    occurrences = 0
    for number, path in enumerate(files, 1):
        with path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
            for shader_hash, offset, length, stage, digest in programs(data):
                occurrences += 1
                value = [str(path.relative_to(game)), offset, length, stage, digest]
                if shader_hash in entries and entries[shader_hash][-1] != digest:
                    raise ValueError(f'Conflicting code for shader ID {shader_hash:016x}')
                entries.setdefault(shader_hash, value)
                groups.setdefault(digest, set()).add(shader_hash)
        if number % 100 == 0:
            print(f'Shader asset index: {number}/{len(files)} files, {len(entries)} programs', flush=True)
        if progress:
            progress(number, len(files))
    temporary = output / 'programs.tsv.tmp'
    with temporary.open('w') as stream:
        for shader_hash, (name, offset, length, stage, digest) in sorted(entries.items()):
            stream.write(f'{shader_hash:016x}\t{offset}\t{length}\t{stage}\t{digest}\t{name}\n')
    temporary.replace(output / 'programs.tsv')
    report = dict(version=VERSION, fingerprint=fingerprint, files=len(files), occurrences=occurrences,
                  programs=len(entries), distinct_code=len(groups),
                  equivalent_identifiers=sum(len(g)-1 for g in groups.values()),
                  stages=dict(Counter(e[3] for e in entries.values())),
                  seconds=time.monotonic()-started, reused=False,
                  index_sha256=hashlib.sha256((output / 'programs.tsv').read_bytes()).hexdigest(),
                  pipeline_catalog_complete=False, contains_shader_code=False)
    temp = report_path.with_suffix('.json.tmp')
    temp.write_text(json.dumps(report, indent=2)+'\n')
    temp.replace(report_path)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.game, args.out), indent=2))
