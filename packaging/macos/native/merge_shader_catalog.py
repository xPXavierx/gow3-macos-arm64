#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Merge validated render descriptions, remapping per-recording variant IDs.

Maintainer tool. Inputs are GSR1/GPR1 exports, never dump/cache/SPIR-V files.
The native importer subsequently remaps these catalogue IDs into each player's
existing local cache, preserving unrelated variants already at the same index.
"""
import argparse
from collections import defaultdict
import ctypes
import ctypes.util
import hashlib
import json
from pathlib import Path
import struct

MASK = (1 << 64) - 1


def combine(shader_hash, permutation):
    return (shader_hash ^ ((permutation + 0x9e3779b9 + (shader_hash << 12) + (shader_hash >> 4)) & MASK)) & MASK


def merge(sources, output, xxhash_library=None):
    if output.exists():
        raise ValueError('Choose a fresh output directory')
    library = ctypes.CDLL(str(xxhash_library or ctypes.util.find_library('xxhash')))
    xxh = library.XXH3_64bits
    xxh.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    xxh.restype = ctypes.c_uint64

    def checksum(data):
        return xxh(data, len(data))

    def checked(path, magic):
        data = path.read_bytes()
        if len(data) < 32 or len(data) > 1024**2 or data[:4] != magic:
            raise ValueError('Invalid render description: ' + str(path))
        if checksum(data[:-8]) != struct.unpack_from('<Q', data, len(data)-8)[0]:
            raise ValueError('Description checksum mismatch: ' + str(path))
        return data[:-8]

    def static_pipeline_state(data):
        # GraphicsPipeline enables eVertexInputBindingStride and the rasterizer
        # supplies actual strides via bindVertexBuffers2 before every draw.
        # Preserve the first valid description; ignore only its dynamic stride
        # when checking duplicate static state from another recording.
        body = bytearray(data[:-8])
        offset = 12 + struct.unpack_from('<I', body, 8)[0]
        attributes = struct.unpack_from('<Q', body, offset)[0]
        offset += 8 + attributes*16
        bindings = struct.unpack_from('<Q', body, offset)[0]
        offset += 8
        if attributes > 32 or bindings > 32 or offset+bindings*12 > len(body):
            raise ValueError('Vertex description bounds')
        for index in range(bindings):
            struct.pack_into('<I', body, offset+index*12+4, 0)
        return body

    compatibility = {int((p/'compatibility.txt').read_text().strip(), 16) for p in sources}
    if len(compatibility) != 1:
        raise ValueError('Cannot merge incompatible render-description ABIs')
    variants = defaultdict(dict)
    proofs = {}
    recordings = []
    for source in sources:
        fingerprints = {}
        for line in (source/'binary-fingerprints.tsv').read_text().splitlines():
            name, binary_hash, code_digest = line.split()
            if len(code_digest) != 64 or len(binary_hash) != 16:
                raise ValueError('Invalid shader fingerprint')
            int(binary_hash, 16); int(code_digest, 16)
            fingerprints[name] = (binary_hash, code_digest)
        recording = {}
        for path in sorted(source.glob('*.recipe')):
            data = checked(path, b'GSR1')
            shader_hash = struct.unpack_from('<Q', data, 16)[0]
            permutation = struct.unpack_from('<Q', data, 32)[0]
            flat_count = struct.unpack_from('<Q', data, 40)[0]
            walker_offset = 48 + 4*flat_count
            if walker_offset+8 > len(data):
                raise ValueError('Resource snapshot bounds')
            walker_count = struct.unpack_from('<Q', data, walker_offset)[0]
            spec_start = walker_offset+8+walker_count
            if spec_start >= len(data):
                raise ValueError('Missing specialization')
            # Neutral resource addresses/dimensions are not variant identity.
            # Keep the complete serialized specialization (including bindings).
            identity = hashlib.sha256(data[spec_start:]).hexdigest()
            proof = fingerprints[path.name]
            previous = proofs.setdefault((shader_hash, identity), proof)
            if previous != proof:
                raise ValueError('Same specialization has conflicting shader fingerprints')
            variants[shader_hash].setdefault(identity, data)
            key = combine(shader_hash, permutation)
            if key in recording and recording[key] != (shader_hash, identity):
                raise ValueError('Conflicting IDs within one recording')
            recording[key] = (shader_hash, identity)
        recordings.append(recording)
    output.mkdir(parents=True)
    canonical = {}
    merged_fingerprints = []
    for shader_hash, group in sorted(variants.items()):
        for permutation, (identity, original) in enumerate(sorted(group.items())):
            data = bytearray(original)
            struct.pack_into('<Q', data, 32, permutation)
            data += struct.pack('<Q', checksum(bytes(data)))
            name = f'{shader_hash:016x}_{permutation}.recipe'
            (output/name).write_bytes(data)
            binary_hash, code_digest = proofs[shader_hash, identity]
            merged_fingerprints.append(f'{name}\t{binary_hash}\t{code_digest}\n')
            canonical[shader_hash, identity] = combine(shader_hash, permutation)
    (output/'binary-fingerprints.tsv').write_text(''.join(merged_fingerprints))
    incomplete = 0
    pipeline_sources = {}
    dynamic_stride_variations = 0
    for source, recording in zip(sources, recordings):
        for path in sorted(source.glob('*.pso')):
            data = bytearray(checked(path, b'GPR1'))
            key_size = struct.unpack_from('<I', data, 8)[0]
            if key_size < 48 or 12+key_size > len(data):
                raise ValueError('Pipeline key bounds')
            complete = True
            for stage in range(6):
                old = struct.unpack_from('<Q', data, 12+stage*8)[0]
                if not old:
                    continue
                if old not in recording:
                    complete = False
                    break
                struct.pack_into('<Q', data, 12+stage*8, canonical[recording[old]])
            if not complete:
                incomplete += 1
                continue
            pipeline_hash = checksum(bytes(data[12:12+key_size]))
            data += struct.pack('<Q', checksum(bytes(data)))
            target = output/f'g_0x{pipeline_hash:016x}.pso'
            if target.exists() and target.read_bytes() != data:
                previous = target.read_bytes()
                if static_pipeline_state(previous) == static_pipeline_state(data):
                    dynamic_stride_variations += 1
                    continue
                positions = [i for i, (a,b) in enumerate(zip(previous, data)) if a != b]
                raise ValueError(f'Same pipeline key has conflicting state: {target.name}; '
                    f'{pipeline_sources[target]} vs {path}; sizes {len(previous)}, {len(data)}; offsets {positions[:24]}')
            target.write_bytes(data)
            pipeline_sources[target] = path
    (output/'compatibility.txt').write_text(f'{next(iter(compatibility)):016x}\n')
    files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(output.iterdir())}
    manifest = dict(format=1, version='opening-2026-10-23', title_id='CUSA01623', tested_version='1.02',
        source_eboot_sha256='d85c8135d330c3b601bf5dc3f1dd86bd6119fb8a9fce372bed2c9785f9a79299',
        shader_recipes=len(list(output.glob('*.recipe'))), pipeline_recipes=len(list(output.glob('*.pso'))),
        scope='Opening, menus and existing bounded validation recordings; exact-code asset index scans all WADs',
        complete_campaign_catalog=False, contains_game_shader_code=False,
        contains_spirv_or_device_binaries=False, neutral_resource_descriptions=True,
        recordings_merged=len(sources), incomplete_source_pipelines_omitted=incomplete,
        dynamic_stride_duplicates=dynamic_stride_variations, files=files)
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return {key: value for key, value in manifest.items() if key != 'files'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--xxhash-library', type=Path)
    args = parser.parse_args()
    print(json.dumps(merge(args.source, args.out, args.xxhash_library), indent=2))
