#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Versioned, canonical community descriptions. Never carries shader/walker code.

The numeric specialization ABI is versioned separately from the JSON envelope.
A native importer regenerates resource walks and GPU programs from the user's
verified dump. These files are not Metal/SPIR-V caches. No network operations.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import struct
import stat

GAME = dict(kind='catalog', format=1, title_id='CUSA01623', tested_version='1.02',
            source_eboot_sha256='d85c8135d330c3b601bf5dc3f1dd86bd6119fb8a9fce372bed2c9785f9a79299')
MAX_BYTES = 64*1024*1024
MAX_RECORDS = 65536
MAX_ROW = 2*1024*1024
MASK = (1 << 64)-1
STAGES = {0: 'fragment', 3: 'vertex'}
# Verified ARM64 layout: Bindings (12) + RuntimeInfo.hw (72) + fs flag (214).
# These constants belong only to the explicitly supported schema transition.
EARLY_DEPTH_ABI = 'f9b9a649560a0b85'
EARLY_DEPTH_SPECIALIZATION_BYTE = 298


def canonical(row):
    return json.dumps(row, sort_keys=True, separators=(',', ':'), ensure_ascii=True, allow_nan=False).encode()


def strict_json(data):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate JSON field')
            result[key] = value
        return result
    def invalid_constant(_):
        raise ValueError('Non-finite JSON value')
    try:
        return json.loads(data, object_pairs_hook=unique, parse_constant=invalid_constant)
    except (RecursionError, UnicodeError) as error:
        raise ValueError('Invalid catalogue encoding/depth') from error


def identity(row):
    return hashlib.sha256(canonical({k: v for k, v in row.items() if k not in ('id', 'reference_spirv')})).hexdigest()


def hex_value(value, length):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{'+str(length)+'}', value):
        raise ValueError('Invalid hexadecimal field')
    return value


def number(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError('Numeric field outside schema bounds')
    return value


def blob(value, limit):
    if not isinstance(value, str) or len(value) % 2 or len(value) > 2*limit or not re.fullmatch('[0-9a-f]*', value):
        raise ValueError('Invalid numeric description')
    return bytes.fromhex(value)


def validate(row):
    if not isinstance(row, dict):
        raise ValueError('Description must be an object')
    common = {'kind', 'id', 'abi', 'key_size'}
    hex_value(row.get('abi'), 16)
    number(row.get('key_size'), 48, 4096)
    if row.get('kind') == 'shader':
        fields = common | {'program', 'source_sha256', 'hardware_stage', 'software_stage',
                           'runtime_size', 'flat', 'layout_size', 'layout_fingerprint',
                           'specialization', 'reference_spirv'}
        if set(row) != fields:
            raise ValueError('Unknown/missing shader field; executable payloads are not supported')
        hex_value(row['program'], 16); hex_value(row['source_sha256'], 64)
        hex_value(row['layout_fingerprint'], 16); hex_value(row['reference_spirv'], 16)
        number(row['hardware_stage'], 0, 1); number(row['software_stage'], 0, 3)
        if (row['hardware_stage'], row['software_stage']) not in ((0, 0), (1, 3)):
            raise ValueError('Unsupported stage')
        number(row['runtime_size'], 8, 16384)
        number(row['layout_size'], 0, 65536)
        if row['layout_size'] % 4 or not int(row['layout_fingerprint'], 16):
            raise ValueError('Resource-layout fingerprint')
        flat = row['flat']
        if not isinstance(flat, list) or not 16 <= len(flat) <= 65536:
            raise ValueError('Resource snapshot bounds')
        for value in flat:
            number(value, 0, 0xffffffff)
        if len(blob(row['specialization'], 256*1024)) < row['runtime_size']:
            raise ValueError('Missing specialization')
    elif row.get('kind') == 'pipeline':
        if set(row) != common | {'stages', 'state'}:
            raise ValueError('Unknown/missing pipeline field')
        stages = row['stages']
        if not isinstance(stages, dict) or not stages or set(stages)-set(STAGES.values()):
            raise ValueError('Unsupported pipeline stages')
        for value in stages.values():
            hex_value(value, 64)
        state = blob(row['state'], 65536)
        if state[:48] != bytes(48):
            raise ValueError('Pipeline identities must be canonical stage IDs, not local permutation numbers')
        validate_pipeline_state(state, row['key_size'])
    else:
        raise ValueError('Unsupported description kind')
    if hex_value(row.get('id'), 64) != identity(row):
        raise ValueError('Description identity mismatch')
    return row


def validate_pipeline_state(state, key_size):
    offset = key_size
    for size in (16, 12, 8):  # vertex attributes, bindings and instance divisors
        if offset+8 > len(state):
            raise ValueError('Truncated vertex description')
        count = struct.unpack_from('<Q', state, offset)[0]
        if count > 32:
            raise ValueError('Excessive vertex description')
        offset += 8 + count*size
    if offset+24 != len(state):
        raise ValueError('Pipeline state/trailing bytes')
    flags, samples, shading, minimum, coverage, alpha = struct.unpack_from('<III f II', state, offset)
    if flags or samples not in (1, 2, 4, 8, 16) or shading > 1 or coverage > 1 or alpha > 1 or not 0 <= minimum <= 1:
        raise ValueError('Unsupported multisampling state')


def load(path):
    path = Path(path)
    if path.is_symlink():
        raise ValueError('Catalogue path/size')
    rows = {}
    # Reject devices, FIFOs and symlinks before reading; bound the bytes actually
    # consumed as well as the stat size (a writer can otherwise grow a file).
    try:
        descriptor = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | os.O_NONBLOCK)
    except OSError as error:
        raise ValueError('Catalogue path/size') from error
    with os.fdopen(descriptor, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_BYTES:
            raise ValueError('Catalogue path/size')
        header = stream.readline(MAX_ROW+1)
        if len(header) > MAX_ROW or canonical(strict_json(header)) != canonical(GAME):
            raise ValueError('Catalogue game/version does not match CUSA01623 1.02')
        while True:
            line = stream.readline(MAX_ROW+1)
            if not line:
                break
            if len(line) > MAX_ROW or stream.tell() > MAX_BYTES or len(rows) >= MAX_RECORDS:
                raise ValueError('Catalogue bounds')
            row = validate(strict_json(line))
            if row['id'] in rows and canonical(rows[row['id']]) != canonical(row):
                # Different GPUs may prove identical input semantics with different emitted binaries.
                previous = rows[row['id']]
                if {k:v for k,v in previous.items() if k!='reference_spirv'} != {k:v for k,v in row.items() if k!='reference_spirv'}:
                    raise ValueError('Conflicting description')
            rows.setdefault(row['id'], row)
    return rows


def save(path, rows):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    content = canonical(GAME)+b'\n'+b''.join(canonical(validate(row))+b'\n' for _, row in sorted(rows.items()))
    if len(content) > MAX_BYTES:
        raise ValueError('Catalogue exceeds size limit')
    temporary = path.with_name(path.name+'.tmp')
    temporary.write_bytes(content); os.replace(temporary, path)
    return dict(shaders=sum(r['kind']=='shader' for r in rows.values()),
                pipelines=sum(r['kind']=='pipeline' for r in rows.values()),
                sha256=hashlib.sha256(content).hexdigest())


def combine(program, permutation):
    return (program ^ ((permutation+0x9e3779b9+(program<<12)+(program>>4)) & MASK)) & MASK


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte)*1099511628211) & MASK
    return value


def checked_native(path, legacy_xxh=None):
    if path.is_symlink() or not 40 <= path.stat().st_size <= MAX_ROW:
        raise ValueError('Native description bounds')
    data = path.read_bytes()
    if data[:4] in (b'GSR2', b'GPR2'):
        if hashlib.sha256(data[:-32]).digest() != data[-32:]:
            raise ValueError('Native description SHA256')
        return data[:-32]
    # One-time maintainer conversion of our existing trusted local seed only.
    if legacy_xxh and data[:4] in (b'GSR1', b'GPR1') and legacy_xxh(data[:-8]) == struct.unpack_from('<Q', data, len(data)-8)[0]:
        return data[:-8]
    raise ValueError('Only non-executable GSR2/GPR2 contributions are accepted')


def collect(directory, output, *, legacy_library=None):
    directory = Path(directory)
    xxh = None
    if legacy_library:
        library = ctypes.CDLL(str(legacy_library)); native_xxh = library.XXH3_64bits
        native_xxh.argtypes = [ctypes.c_void_p, ctypes.c_size_t]; native_xxh.restype = ctypes.c_uint64
        xxh = lambda data: native_xxh(data, len(data))
    abi = hex_value((directory/'compatibility.txt').read_text().strip(), 16)
    proofs = {}
    for line in (directory/'binary-fingerprints.tsv').read_text().splitlines():
        name, fingerprint, digest = line.split()
        proofs[name] = (hex_value(fingerprint, 16), hex_value(digest, 64))
    rows, mapping = {}, {}
    for path in sorted(directory.glob('*.recipe')):
        data = checked_native(path, xxh)
        magic, version, runtime_size, key_size, program, hw, sw, permutation, count = struct.unpack_from('<4sIIIQIIQQ', data)
        if count > 65536 or len(data) < 48+4*count+16:
            raise ValueError('Resource description bounds')
        offset = 48+4*count
        size = struct.unpack_from('<Q', data, offset)[0]
        if magic == b'GSR1':
            if offset+8+size >= len(data):
                raise ValueError('Legacy layout bounds')
            layout_hash = fnv(data[offset+8:offset+8+size]); spec_start = offset+8+size
        else:
            layout_hash = struct.unpack_from('<Q', data, offset+8)[0]; spec_start = offset+16
        fingerprint, digest = proofs[path.name]
        row = dict(kind='shader', abi=abi, program=f'{program:016x}', source_sha256=digest,
                   hardware_stage=hw, software_stage=sw, runtime_size=runtime_size, key_size=key_size,
                   flat=list(struct.unpack_from('<'+str(count)+'I', data, 48)), layout_size=size,
                   layout_fingerprint=f'{layout_hash:016x}', specialization=data[spec_start:].hex(), reference_spirv=fingerprint)
        row['id'] = identity(row); validate(row); rows[row['id']] = row
        mapping[combine(program, permutation)] = row['id']
    incomplete = 0
    for path in sorted(directory.glob('*.pso')):
        data = checked_native(path, xxh)
        key_size = struct.unpack_from('<I', data, 8)[0]
        state = bytearray(data[12:]); stages = {}
        for index in range(6):
            key = struct.unpack_from('<Q', state, index*8)[0]
            if not key:
                continue
            if index not in STAGES or key not in mapping:
                incomplete += 1; break
            stages[STAGES[index]] = mapping[key]
        else:
            state[:48] = bytes(48)
            row = dict(kind='pipeline', abi=abi, key_size=key_size, stages=stages, state=state.hex())
            row['id'] = identity(row); validate(row); rows[row['id']] = row
    if Path(output).exists():
        old = load(output); old.update(rows); rows = old
    return {**save(output, rows), 'incomplete_pipelines_deferred': incomplete}


def import_catalogue(source, directory, reference):
    """Validate before copying; imports are data, never instructions or GPU code."""
    rows = load(source)
    expected = {r['abi'] for r in load(reference).values()}
    if not rows or {r['abi'] for r in rows.values()} != expected:
        raise ValueError('Catalogue renderer ABI does not match this application')
    digest = hashlib.sha256(canonical(rows)).hexdigest()
    directory=Path(directory)
    files=list(directory.glob('*.jsonl'))
    destination=directory/(digest+'.jsonl')
    if len(files)>=64 and not destination.exists():
        raise ValueError('At most 64 imported catalogues are supported; merge contributions first')
    if sum(p.stat().st_size for p in files if p!=destination)+Path(source).stat().st_size>MAX_BYTES:
        raise ValueError('Imported catalogue collection exceeds 64 MiB')
    return save(destination, rows)


def export_catalogue(support, output, reference):
    """Export local discoveries only; users explicitly choose where to share."""
    support = Path(support)
    recording = support/'Community/recording'
    expected = {r['abi'] for r in load(reference).values()}
    if len(expected)!=1:raise ValueError('Application catalogue ABI')
    contribution = support/'Community/contributions'/(next(iter(expected))+'.jsonl')
    # A raw permutation number is scoped to one process. Never mix raw files
    # from different sessions before converting them to canonical stage IDs.
    candidates=[recording]+sorted(recording.glob('*'))
    for directory in candidates:
        compatibility=directory/'compatibility.txt'
        if not directory.is_symlink() and compatibility.is_file() and compatibility.read_text().strip() in expected:
            collect(directory,contribution)
    legacy=support/'Community/contribution.jsonl'
    if not contribution.exists() and legacy.exists(): contribution=legacy
    if not contribution.exists():
        raise ValueError('No local discoveries yet. Play a session before exporting.')
    rows = load(contribution)
    expected = {r['abi'] for r in load(reference).values()}
    if {r['abi'] for r in rows.values()} != expected:
        raise ValueError('Local discoveries belong to another renderer version')
    return save(output, rows)


def migrate_descriptions(rows, target_abi, reference):
    """Rebase the unchanged portable schema; the renderer must rebuild/verify SPIR-V.

    Only explicitly supported transitions are accepted. Never relabel compiled code.
    Original catalogues stay untouched, and unresolved PSOs remain on-demand.
    """
    if target_abi == EARLY_DEPTH_ABI:
        return migrate_early_depth_descriptions(rows, reference)
    old_abi = 'f9bf6c9321b85a41'
    if target_abi != 'f9baa7c07fa3310e' or {r['abi'] for r in rows.values()} != {old_abi}:
        raise ValueError('No compatible catalogue description migration')
    mapping, migrated = {}, {}
    for row in reference.values():
        validate(row)
        if row['kind'] == 'shader' and row['abi'] == target_abi:
            previous = dict(row, abi=old_abi)
            mapping[identity(previous)] = row['id']
    for row in rows.values():
        validate(row)
        if row['kind'] != 'shader':
            continue
        replacement = dict(row, abi=target_abi, reference_spirv='0'*16)
        replacement['id'] = identity(replacement)
        validate(replacement)
        mapping[row['id']] = replacement['id']
        migrated[replacement['id']] = replacement
    for row in rows.values():
        if row['kind'] != 'pipeline' or any(stage not in mapping for stage in row['stages'].values()):
            continue
        replacement = dict(row, abi=target_abi,
            stages={name:mapping[stage] for name,stage in row['stages'].items()})
        replacement['id'] = identity(replacement)
        validate(replacement)
        migrated[replacement['id']] = replacement
    return migrated


def migrate_early_depth_descriptions(rows, reference):
    """Old records lost the guest bit: prepare both legal possibilities.

    No previous GPU binary is reused and no missing bit is guessed from a
    shader hash. Live specialization selects and checks the actual guest state.
    """
    abis = {r['abi'] for r in rows.values()}
    if len(abis) != 1 or not abis <= {'f9bf6c9321b85a41', 'f9baa7c07fa3310e'}:
        raise ValueError('No compatible catalogue description migration')
    old_abi = next(iter(abis))
    mapping, migrated = {}, {}
    def check_layout(row):
        if row['runtime_size'] != 288 or row['key_size'] != 352 or len(row['specialization']) < 600:
            raise ValueError('Unsupported early-depth description layout')
    # Pipeline-only contributions may reference shaders already in the seed.
    for row in reference.values():
        validate(row)
        if row['kind'] != 'shader' or row['abi'] != EARLY_DEPTH_ABI:
            continue
        check_layout(row)
        raw = bytearray.fromhex(row['specialization'])
        if row['hardware_stage'] == 0:
            raw[EARLY_DEPTH_SPECIALIZATION_BYTE] = 0 # old zero-initialized padding
        previous = dict(row, abi=old_abi, specialization=raw.hex())
        mapping.setdefault(identity(previous), []).append(row['id'])
    for row in rows.values():
        validate(row)
        if row['kind'] != 'shader':
            continue
        check_layout(row)
        if row['hardware_stage'] == 0 and bytes.fromhex(row['specialization'])[EARLY_DEPTH_SPECIALIZATION_BYTE] != 0:
            raise ValueError('Nonzero legacy early-depth padding')
        alternatives = []
        for early in ((0, 1) if row['hardware_stage'] == 0 else (0,)):
            raw = bytearray.fromhex(row['specialization'])
            if row['hardware_stage'] == 0:
                raw[EARLY_DEPTH_SPECIALIZATION_BYTE] = early
            replacement = dict(row, abi=EARLY_DEPTH_ABI, specialization=raw.hex(), reference_spirv='0'*16)
            replacement['id'] = identity(replacement)
            validate(replacement)
            migrated[replacement['id']] = replacement
            alternatives.append(replacement['id'])
        mapping[row['id']] = alternatives
    for row in rows.values():
        if row['kind'] != 'pipeline' or any(stage not in mapping for stage in row['stages'].values()):
            continue
        names = sorted(row['stages'])
        for stages in itertools.product(*(sorted(set(mapping[row['stages'][name]])) for name in names)):
            replacement = dict(row, abi=EARLY_DEPTH_ABI, stages=dict(zip(names, stages)))
            replacement['id'] = identity(replacement)
            validate(replacement)
            migrated[replacement['id']] = replacement
    return migrated


def materialize(catalogues, output):
    """Resolve canonical stage IDs to process-local variant IDs, preserving native cache remapping."""
    rows = {}
    for path in catalogues:
        rows.update(load(path))
        if len(rows)>MAX_RECORDS or len(canonical(rows))>MAX_BYTES:
            raise ValueError('Merged catalogue exceeds bounds')
    output = Path(output)
    shader_rows = sorted((r for r in rows.values() if r['kind']=='shader'), key=lambda r:(r['program'], r['id']))
    pipeline_rows = [r for r in rows.values() if r['kind']=='pipeline']
    abis = {r['abi'] for r in rows.values()}
    if len(abis) != 1:
        raise ValueError('Catalogue renderer ABIs differ')
    abi = next(iter(abis))
    output.mkdir(parents=True, exist_ok=True)
    source_fingerprint = hashlib.sha256(canonical(rows)).hexdigest()
    manifest = output/'manifest.json'
    if manifest.exists():
        previous = json.loads(manifest.read_text())
        if previous.get('source_fingerprint') == source_fingerprint and all(
                (output/name).is_file() and hashlib.sha256((output/name).read_bytes()).hexdigest() == digest
                for name,digest in previous.get('files',{}).items()):
            return previous
    stage_map, counts, proofs = {}, {}, []
    expected = set()
    def write(name, data):
        (output/name).write_bytes(data+hashlib.sha256(data).digest()); expected.add(name)
    for row in shader_rows:
        program = int(row['program'], 16); permutation = counts.get(program, 0); counts[program] = permutation+1
        if permutation > 1024:
            raise ValueError('Excessive specializations for one program')
        name = f'{program:016x}_{permutation}.recipe'
        flat = row['flat']
        data = struct.pack('<4sIIIQIIQQ', b'GSR2', 2, row['runtime_size'], row['key_size'], program,
                           row['hardware_stage'], row['software_stage'], permutation, len(flat))
        data += struct.pack('<'+str(len(flat))+'I', *flat)
        data += struct.pack('<QQ', row['layout_size'], int(row['layout_fingerprint'], 16))
        data += bytes.fromhex(row['specialization']); write(name, data)
        stage_map[row['id']] = combine(program, permutation)
        proofs.append(f'{name}\t{row["reference_spirv"]}\t{row["source_sha256"]}\n')
    complete = 0
    for row in pipeline_rows:
        if any(stage not in stage_map for stage in row['stages'].values()):
            continue
        state = bytearray.fromhex(row['state'])
        for index,name in STAGES.items():
            if name in row['stages']:
                struct.pack_into('<Q', state, index*8, stage_map[row['stages'][name]])
        write('g_'+row['id']+'.pso', struct.pack('<4sII', b'GPR2', 2, row['key_size'])+state); complete += 1
    (output/'compatibility.txt').write_text(abi+'\n'); expected.add('compatibility.txt')
    (output/'binary-fingerprints.tsv').write_text(''.join(proofs)); expected.add('binary-fingerprints.tsv')
    for path in output.iterdir():
        if path.name not in expected | {'manifest.json'} and path.suffix in ('.recipe','.pso'):
            path.unlink()
    files = {name:hashlib.sha256((output/name).read_bytes()).hexdigest() for name in sorted(expected)}
    report = dict(format=1, title_id=GAME['title_id'], tested_version=GAME['tested_version'],
                  source_eboot_sha256=GAME['source_eboot_sha256'], source_fingerprint=source_fingerprint,
                  shader_recipes=len(shader_rows), pipeline_recipes=complete,
                  contains_shader_code=False, contains_walker_code=False, files=files)
    manifest.write_text(json.dumps(report, indent=2)+'\n'); return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    c = sub.add_parser('collect'); c.add_argument('recording', type=Path); c.add_argument('output', type=Path)
    c.add_argument('--trusted-legacy-library', type=Path)
    m = sub.add_parser('prepare'); m.add_argument('output', type=Path); m.add_argument('catalogues', nargs='+', type=Path)
    v = sub.add_parser('validate'); v.add_argument('catalogue', type=Path)
    i = sub.add_parser('import'); i.add_argument('source', type=Path); i.add_argument('directory', type=Path); i.add_argument('reference', type=Path)
    e = sub.add_parser('export'); e.add_argument('support', type=Path); e.add_argument('output', type=Path); e.add_argument('reference', type=Path)
    a = parser.parse_args()
    if a.action == 'collect':
        result = collect(a.recording, a.output, legacy_library=a.trusted_legacy_library)
    elif a.action == 'prepare':
        result = materialize(a.catalogues, a.output)
    elif a.action == 'import':
        result = import_catalogue(a.source, a.directory, a.reference)
    elif a.action == 'export':
        result = export_catalogue(a.support, a.output, a.reference)
    else:
        rows = load(a.catalogue); result = dict(valid=True, records=len(rows))
    print(json.dumps({k:v for k,v in result.items() if k!='files'}, indent=2))

if __name__ == '__main__':
    main()
