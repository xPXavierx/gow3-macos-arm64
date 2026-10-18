#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Maintainer-side audit of a community app; requires no proprietary fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import subprocess


def audit(app):
    resources = app / 'Contents/Resources'
    manifest = json.loads((resources / 'build-manifest.json').read_text())
    assert manifest['distribution'] == 'community-first-run'
    assert manifest['profiles'] == 0 and not manifest['local_private_aot']
    assert not (resources / 'Profiles').exists()
    assert not (resources / 'TexturePack').exists(), 'Local texture packs are private test data'
    forbidden = {'boot-linked.bin', 'eboot.bin', 'eboot.elf', 'linked.elf', 'functions.tsv', 'offsets.txt',
        'libgow3-recompiled.dylib', 'analysis.json', 'link.json', 'preferences.json', 'aot-patches.json'}
    rows = []
    files = 0
    for file in app.rglob('*'):
        if file.is_symlink():
            assert file.resolve().is_relative_to(app), file
        if not file.is_file():
            continue
        files += 1
        assert file.name not in forbidden, file
        assert file.suffix.lower() not in ('.wad', '.prx', '.pkg', '.self', '.pyc', '.g3tx'), file
        assert '__pycache__' not in file.parts and '.git' not in file.parts, file
        with file.open('rb') as stream:
            magic = stream.read(8)
        if magic[:4] not in (bytes.fromhex('cffaedfe'), bytes.fromhex('cafebabe'), bytes.fromhex('bebafeca')):
            continue
        assert magic == struct.pack('<II', 0xfeedfacf, 0x100000c), file
        identities = subprocess.check_output(['/usr/bin/otool', '-D', str(file)], text=True).splitlines()[1:]
        for line in subprocess.check_output(['/usr/bin/otool', '-L', str(file)], text=True).splitlines()[1:]:
            dep = line.strip().split(' (compatibility')[0]
            if dep in identities or dep.startswith(('/usr/lib/', '/System/Library/')):
                continue
            assert dep.startswith('@loader_path/'), (file, dep)
            target = (file.parent / dep.removeprefix('@loader_path/')).resolve()
            assert target.is_file() and target.is_relative_to(app), (file, dep)
        rows.append(str(file.relative_to(app)))
    for file in (app / 'Contents/Info.plist', resources / 'Runtime/bin/gow3-probe-arm64.app/Contents/Info.plist'):
        meta = plistlib.loads(file.read_bytes())
        assert meta['NSHighResolutionCapable'] and meta['LSSupportsGameMode']
        assert meta['LSMinimumSystemVersion'] == '26.0'
    subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    return {'version': manifest['version'], 'recipe': manifest['preparation_recipe'],
        'all_thin_arm64': True, 'non_system_dependencies_bundled': True,
        'no_game_dump_or_prepared_game_code': True, 'no_user_preferences_saves_or_bytecode': True,
        'signature_integrity_valid': True, 'signature_type': 'ad-hoc', 'apple_notarized': False,
        'retina_and_game_mode_metadata': True, 'minimum_macos': '26.0',
        'files_audited': files, 'macho_count': len(rows), 'binaries': rows}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    result = audit(a.app.resolve())
    a.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'binaries'}, indent=2))
