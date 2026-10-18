#!/usr/bin/env python3
"""Build the pinned MoltenVK dependency with optional native presentation pacing.

No game files are inputs. Nothing is installed into system directories.
The stock driver remains usable when the opt-in extensions are not selected.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys

REVISION = 'db66022459ffb663aa2b50f6b018bc2e124f5edf'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-directory', type=Path, required=True)
p.add_argument('--output-directory', type=Path, required=True)
p.add_argument('--cmake', default='cmake')
p.add_argument('--sdk', type=Path)
p.add_argument('--jobs', type=int, default=6)
p.add_argument('--frame-pacing', action='store_true',
               help='Include the opt-in Metal minimum-presentation-duration extension')
a = p.parse_args()
if sys.platform != 'darwin' or platform.machine() != 'arm64':
    p.error('Run with an ARM64 Python on macOS, without Rosetta')
source, output = a.source_directory.resolve(), a.output_directory.resolve()
output.mkdir(parents=True, exist_ok=True)
patch = Path(__file__).with_name('moltenvk-stencil-only.patch')

def run(command, **kwargs):
    print(' '.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

if not source.exists():
    run(['git', 'clone', '--depth', '1', '--branch', 'v1.4.2',
         'https://github.com/KhronosGroup/MoltenVK.git', source])
head = subprocess.check_output(['git', '-C', source, 'rev-parse', 'HEAD'], text=True).strip()
if head != REVISION:
    p.error('Expected the pinned MoltenVK v1.4.2 revision; use a separate source directory')
applied = subprocess.run(['git', '-C', source, 'apply', '--reverse', '--check', patch],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
if not applied:
    run(['git', '-C', source, 'apply', '--check', patch])
    run(['git', '-C', source, 'apply', patch])
pacing_patch = Path(__file__).with_name('moltenvk-present-duration.patch')
pacing_applied = subprocess.run(['git', '-C', source, 'apply', '--reverse', '--check', pacing_patch],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
if a.frame_pacing and not pacing_applied:
    run(['git', '-C', source, 'apply', '--check', pacing_patch])
    run(['git', '-C', source, 'apply', pacing_patch])
    pacing_applied = True
sdk = a.sdk or Path(subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip())
build = source/'out/arm64-release'
run([a.cmake, '-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
     '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_SYSROOT='+str(sdk),
     '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0', '-DMVK_EXCLUDE_SPIRV_TOOLS=ON',
     '-DMOLTEN_VK_WITH_CCACHE=OFF', '-DCPM_SOURCE_CACHE='+str(source/'out/dependency-cache')], cwd=source)
run([a.cmake, '--build', build, '--target', 'MoltenVK', '--parallel', str(max(1, a.jobs))], cwd=source)
library = build/'MoltenVK/libMoltenVK.dylib'
architecture = subprocess.check_output(['lipo', '-archs', library], text=True).strip()
if architecture != 'arm64':
    p.error('The resulting driver is not ARM64-only')
installed = output/'libMoltenVK.dylib'
shutil.copy2(library, installed)
icd = output/'MoltenVK_icd.json'
icd.write_text(json.dumps({'file_format_version': '1.0.0', 'ICD': {
    'library_path': str(installed), 'api_version': '1.4.0', 'is_portability_driver': True}}, indent=2)+'\n')
(output/'build-manifest.json').write_text(json.dumps({
    'architecture': architecture, 'source_revision': head, 'source': 'KhronosGroup/MoltenVK',
    'patch_sha256': digest(patch), 'library_sha256': digest(installed), 'sdk': str(sdk),
    'stencil_only_dynamic_rendering_fix': True,
    'present_duration_extension': pacing_applied,
    'present_duration_patch_sha256': digest(pacing_patch) if pacing_applied else None}, indent=2)+'\n')
print('Built:', icd)
print('Optional stencil-plane and FIFO minimum-duration paths require separate validation.')
