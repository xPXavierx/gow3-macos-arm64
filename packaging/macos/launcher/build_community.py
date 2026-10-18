#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Package an offline ARM64 first-run compiler around the standard launcher.

The input runtime app is never copied wholesale: private prepared Profiles are
excluded. No dump, generated C, object, linked guest image or AOT game library is
accepted in the distribution. Branding and the runtime are identical to the local
launcher; only game preparation changes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import xml.etree.ElementTree as ET

from session import RESOLUTIONS, SCALES, plan

SUPPORTED = {
    'title': 'God of War III Remastered', 'title_id': 'CUSA01623', 'tested_version': '1.02',
    'note': 'Executable and module hashes are authoritative. The tested extracted param.sfo reports APP_VER 01.00; do not infer compatibility from that field alone.',
    'files': {
        'eboot.bin': 'd85c8135d330c3b601bf5dc3f1dd86bd6119fb8a9fce372bed2c9785f9a79299',
        'sce_module/libc.prx': '4378b47f46f1d856824a6f971db1a0c3f41833b77f49d2fba9538f667a139166',
        'sce_module/libSceFios2.prx': '3b8eb4d5a5c5a7b381d8d0610077e6cd404c29695a686274802f6b2737f6bf22',
    },
}


def presets_xml(source, destination):
    xml = ET.parse(source)
    template = next(x for x in xml.getroot().findall('Metadata') if x.get('Name') == 'Resolution Patch - 4K')
    known = {(1920, 1080), (1280, 720), (2560, 1440), (3840, 2160)}
    for size in sorted({tuple(plan(r, u, '60')['input']) for r in RESOLUTIONS for u in SCALES} - known):
        w, h = size
        entry = ET.fromstring(ET.tostring(template))
        entry.set('Name', f'Resolution Patch - {w}x{h}')
        entry.set('Author', 'kvicken + cuesta4; launcher preset adaptation')
        entry.set('Note', 'Exact desktop input size. Requires DMEM of 6144 MB or more. One resolution patch only.')
        for line in entry.find('PatchList'):
            data = bytes.fromhex(line.get('Value'))
            replacement = {3840: w, 2160: h, 1920: w//2, 1080: h//2}.get(int.from_bytes(data, 'little'))
            if replacement is not None:
                line.set('Value', replacement.to_bytes(len(data), 'little').hex())
        xml.getroot().append(entry)
    xml.write(destination, encoding='utf-8', xml_declaration=True)


def require_arm64(binary):
    arches = subprocess.check_output(['/usr/bin/lipo', str(binary), '-archs'], text=True).strip()
    if arches != 'arm64':
        raise ValueError(f'Expected a thin ARM64 tool: {binary}: {arches}')
    deps = subprocess.check_output(['/usr/bin/otool', '-L', str(binary)], text=True).splitlines()[1:]
    if any(not line.strip().startswith(('/usr/lib/', '/System/Library/')) for line in deps):
        raise ValueError(f'Tool has an unbundled dependency: {binary}')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('runtime-app', 'launcher', 'tools', 'recomp-source', 'decoder-include', 'decoder-source', 'zig', 'out'):
        p.add_argument('--' + name, required=True, type=Path)
    a = p.parse_args()
    if a.out.exists():
        raise ValueError('Choose a fresh output directory')
    repo = Path(__file__).resolve().parents[3]
    source = Path(__file__).resolve().parent
    # Explicit exclusions prevent accidental inclusion of the private game build.
    shutil.copytree(a.runtime_app, a.out, ignore=shutil.ignore_patterns('Profiles', '__pycache__', '*.pyc'))
    contents = a.out / 'Contents'
    resources = contents / 'Resources'
    shutil.copy2(a.launcher, contents / 'MacOS/GOW3Launcher')
    subprocess.run(['/usr/bin/xcrun', 'swiftc', '-O', '-target', 'arm64-apple-macos14.0',
        '-framework', 'CoreGraphics', str(source / 'DisplayMode.swift'),
        '-o', str(resources / 'DisplayMode')], check=True)
    for name in ('session.py', 'community.py', 'Help-pt-BR.txt'):
        shutil.copy2(source / name, resources / ('Help.txt' if name == 'Help-pt-BR.txt' else name))
    runtime = resources / 'Runtime'
    shutil.copy2(repo / 'run.py', runtime / 'run.py')
    shutil.rmtree(runtime / 'scripts')
    shutil.copytree(repo / 'scripts', runtime / 'scripts', ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    native = runtime / 'packaging/macos/native'
    native.mkdir(parents=True)
    for name in ('prepare_game.py', 'aot_patches.py'):
        shutil.copy2(repo / 'packaging/macos/native' / name, native / name)
    presets_xml(repo / 'patches/God_of_War_III_Remastered.xml', runtime / 'patches/God_of_War_III_Remastered.xml')
    tools = resources / 'Recompiler/bin'
    tools.mkdir(parents=True)
    for name in ('gow3-aot-scan', 'gow3-aot-recomp'):
        require_arm64(a.tools / name)
        shutil.copy2(a.tools / name, tools / name)
    headers = resources / 'Recompiler/include'
    shutil.copytree(a.recomp_source / 'src', headers / 'src',
        ignore=lambda folder, names: [n for n in names if (Path(folder) / n).is_file() and not n.endswith('.h')])
    for name in ('Zydis', 'Zycore'):
        shutil.copytree(a.decoder_include / name, headers / 'decoder' / name)
    require_arm64(a.zig / 'zig')
    compiler = resources / 'Compiler'
    compiler.mkdir()
    shutil.copy2(a.zig / 'zig', compiler / 'zig')
    shutil.copytree(a.zig / 'lib', compiler / 'lib')
    shutil.copy2(a.zig / 'LICENSE', compiler / 'LICENSE')
    (resources / 'supported-game.json').write_text(json.dumps(SUPPORTED, indent=2) + '\n')
    # Include the exact generator/runtime headers as corresponding source.
    licenses = resources / 'Licenses'
    for name in ('LICENSE', 'COPYING'):
        if (a.recomp_source / name).exists():
            shutil.copy2(a.recomp_source / name, licenses / ('bloodborne-recomp-' + name))
    shutil.copytree(a.recomp_source, resources / 'Recompiler/Source',
        ignore=shutil.ignore_patterns('.git', 'build', '__pycache__'))
    shutil.copytree(a.decoder_source, resources / 'Recompiler/ZydisSource',
        ignore=shutil.ignore_patterns('.git', 'build', '__pycache__'))
    for name, file in [('Zydis', a.decoder_source / 'LICENSE'),
                       ('Zycore', a.decoder_source / 'dependencies/zycore/LICENSE')]:
        shutil.copy2(file, licenses / (name + '-LICENSE'))
    (licenses / 'NOTICE.txt').write_text(
        'Community ARM64 first-run preparation build. No game dump or generated game code is bundled.\n'
        'Provide your own compatible plaintext dump. CUSA01623 v1.02 is the tested executable.\n'
        'God of War and its artwork are properties of their respective owners; this is an unofficial fan project.\n'
        'gow3_pc and bloodborne-recomp: GPL-2.0-or-later. Corresponding source: https://github.com/xPXavierx/gow3-macos-arm64\n'
        'The exact recompiler source is included under Recompiler/Source. Third-party licenses are retained in this directory and Compiler/lib.\n'
        'Zig 0.15.2 official macOS aarch64 release: https://ziglang.org/download/0.15.2/zig-aarch64-macos-0.15.2.tar.xz\n'
        'Archive SHA-256: 3cc2bab367e185cdfb27501c4b30b1b0653c28d9f73df8dc91488e66ece5fa6b\n')
    # Hash the preparation policy, headers and native tools, independently of UI
    # cosmetics. A changed recipe cannot reuse stale locally generated code.
    h = hashlib.sha256(b'community-aot-v1;zig-0.15.2;arm64-macos26;O2;fp-contract-off;no-strict-aliasing\n')
    inputs = [resources / 'community.py', resources / 'supported-game.json', compiler / 'zig']
    inputs += list((resources / 'Recompiler/include').rglob('*.h')) + list(tools.iterdir())
    inputs += list((runtime / 'scripts').glob('*.py')) + list(native.glob('*.py'))
    inputs += list((runtime / 'patches').glob('*.xml'))
    for file in sorted(inputs):
        h.update(str(file.relative_to(resources)).encode() + b'\0' + file.read_bytes())
    recipe = h.hexdigest()[:24]
    manifest = json.loads((resources / 'build-manifest.json').read_text())
    manifest.update(distribution='community-first-run', version='0.4.1', profiles=0,
        local_private_aot=False, automatic_preparation=True, preparation_recipe=recipe,
        compiler='Zig 0.15.2 / Clang 20.1.2', requires_xcode=False, requires_homebrew=False,
        initial_preparation_offline=True, tested_title_id='CUSA01623', tested_version='1.02')
    (resources / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    plist = plistlib.loads((contents / 'Info.plist').read_bytes())
    plist.update(CFBundleVersion='5', CFBundleShortVersionString='0.4.1')
    (contents / 'Info.plist').write_bytes(plistlib.dumps(plist))
    forbidden = {'boot-linked.bin', 'eboot.bin', 'eboot.elf', 'linked.elf', 'functions.tsv', 'offsets.txt',
                 'libgow3-recompiled.dylib', 'analysis.json', 'link.json', 'aot-patches.json', 'preferences.json'}
    for file in resources.rglob('*'):
        if file.is_file() and (file.name in forbidden or file.suffix.lower() in ('.wad', '.prx', '.pkg', '.self') or
            (recompiled_name(file.name) and file != resources / 'Recompiler/Source/src/cpu/recomp.c')):
            raise ValueError('Game-derived/private file in distribution: ' + str(file))
    for binary in [*tools.iterdir(), compiler / 'zig', contents / 'MacOS/GOW3Launcher']:
        subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', str(binary)], check=True)
    subprocess.run(['/usr/bin/codesign', '--force', '--sign', '-', str(a.out)], check=True)
    subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(a.out)], check=True)
    print('Built community app:', a.out, 'recipe:', recipe, flush=True)


def recompiled_name(name):
    return name.startswith('recomp') and name.endswith(('.c', '.o', '.dylib'))


if __name__ == '__main__':
    main()
