#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bundle a local, private ARM64 app. The full game dump remains external.

Prepared AOT profiles are game-derived, so output is local-only. Source packages
must exclude Profiles, the app and generated C/libraries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
from branding import install_branding

p = argparse.ArgumentParser()
for name in ('launcher','host-app','renderer','moltenvk','python-prefix','out'):
    p.add_argument('--'+name,required=True,type=Path)
p.add_argument('--profiles',type=Path)
p.add_argument('--runtime-only',action='store_true',help='Create a runtime staging app for build_community.py; no game preparation is included')
p.add_argument('--branding-directory',type=Path,
    help='Local GOW-3-Icon.jpg, GOW-3-Art.jpg and GODOFWAR.TTF supplied by the user')
a = p.parse_args()
if (a.profiles is None) != a.runtime_only:
    p.error('Supply --profiles for a private app, or --runtime-only for community runtime staging')
repo = Path(__file__).resolve().parents[3]
app = a.out.resolve()
if app.exists():
    raise ValueError('Output already exists; choose a fresh staging directory')
contents = app/'Contents'
macos,resources,frameworks = (contents/x for x in ('MacOS','Resources','Frameworks'))
for folder in (macos,resources,frameworks):folder.mkdir(parents=True,exist_ok=True)
shutil.copy2(a.launcher,macos/'GOW3Launcher')
plist = {'CFBundleExecutable':'GOW3Launcher','CFBundleIdentifier':'org.gow3pc.launcher',
    'CFBundleName':'God of War III','CFBundleDisplayName':'God of War III · Apple Silicon',
    'CFBundlePackageType':'APPL','CFBundleVersion':'3','CFBundleShortVersionString':'0.3.1',
    'LSMinimumSystemVersion':'26.0','LSArchitecturePriority':['arm64'],
    'LSApplicationCategoryType':'public.app-category.games','LSSupportsGameMode':True,
    'GCSupportsGameMode':True,'NSHighResolutionCapable':True,
    'CFBundleDevelopmentRegion':'en','CFBundleLocalizations':['en','pt-BR','es']}
(contents/'Info.plist').write_bytes(plistlib.dumps(plist))
# Real game process keeps its own app identity, fullscreen/Game Mode metadata.
rt = resources/'Runtime'
(rt/'bin').mkdir(parents=True)
shutil.copytree(a.host_app,rt/'bin/gow3-probe-arm64.app')
host = rt/'bin/gow3-probe-arm64.app/Contents/MacOS/gow3-probe-arm64'
shutil.copy2(host,rt/'bin/gow3-probe-arm64')
shutil.copy2(repo/'run.py',rt/'run.py')
shutil.copytree(repo/'scripts',rt/'scripts',ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
shutil.copytree(repo/'patches',rt/'patches')
# patches.py locates profiles relative to its script; append only exact sizes.
if a.profiles:
    shutil.copy2(a.profiles/'God_of_War_III_Remastered.xml',rt/'patches/God_of_War_III_Remastered.xml')
shutil.copy2(Path(__file__).with_name('session.py'),resources/'session.py')
shutil.copy2(Path(__file__).with_name('Help-pt-BR.txt'),resources/'Help.txt')
prof = resources/'Profiles'
prof.mkdir()
profile_count = 0
for source in sorted(a.profiles.iterdir()) if a.profiles else []:
    if not (source/'profile.json').exists():continue
    target = prof/source.name
    (target/'aot').mkdir(parents=True)
    for name in ('boot-linked.bin','analysis.json','link.json','native-inputs.json','aot-patches.json','profile.json'):
        shutil.copy2(source/name,target/name)
    shutil.copy2(source/'aot/libgow3-recompiled.dylib',target/'aot/libgow3-recompiled.dylib')
    profile_count += 1
if not a.runtime_only and profile_count != 24:
    raise ValueError(f'Expected 24 complete static profiles, got {profile_count}')
# Relocatable Python, stdlib only. No Homebrew/Python installation required at runtime.
py = resources/'Python'
(py/'bin').mkdir(parents=True)
shutil.copy2(a.python_prefix/'Resources/Python.app/Contents/MacOS/Python',py/'bin/python3.14')
shutil.copytree(a.python_prefix/'lib/python3.14',py/'lib/python3.14',
    ignore=shutil.ignore_patterns('site-packages','__pycache__','*.pyc','test','tests','idlelib','tkinter','turtledemo','ensurepip','config-*','sitecustomize.py'))
branding = install_branding(resources,a.branding_directory)
child=rt/'bin/gow3-probe-arm64.app/Contents'
(child/'Resources').mkdir(exist_ok=True)
shutil.copy2(resources/'AppIcon.icns',child/'Resources/AppIcon.icns')
child_plist=plistlib.loads((child/'Info.plist').read_bytes());child_plist['CFBundleIconFile']='AppIcon';child_plist['LSMinimumSystemVersion']='26.0'
(child/'Info.plist').write_bytes(plistlib.dumps(child_plist))
plist['CFBundleIconFile']='AppIcon';(contents/'Info.plist').write_bytes(plistlib.dumps(plist))

known = {'libgow3gpu.dylib':a.renderer.resolve(),'libMoltenVK.dylib':a.moltenvk.resolve()}
originals = {}
processed = set()
queue = []
for src in (a.renderer,a.moltenvk):
    dest=frameworks/src.name
    shutil.copy2(src,dest); originals[dest]=src.resolve();queue.append(dest)
for binary in (macos/'GOW3Launcher',host,rt/'bin/gow3-probe-arm64',py/'bin/python3.14'):
    queue.append(binary);originals[binary]=binary
for binary in (py/'lib/python3.14/lib-dynload').glob('*.so'):
    queue.append(binary)
    originals[binary]=(a.python_prefix/'lib/python3.14/lib-dynload'/binary.name).resolve()

def dependencies(binary):
    return [x.strip().split(' (compatibility')[0] for x in subprocess.check_output(['/usr/bin/otool','-L',str(binary)],text=True).splitlines()[1:]]

def non_system(dep):
    return not dep.startswith(('/usr/lib/','/System/Library/'))

# Copy the complete transitive dependency graph. Resolve @rpath by the source's
# paths, never from the user's environment at runtime.
while queue:
    binary=queue.pop(0)
    if binary in processed:continue
    processed.add(binary)
    arches=subprocess.check_output(['/usr/bin/lipo',str(binary),'-archs'],text=True).split()
    if 'arm64' not in arches:raise ValueError(f'No ARM64 slice: {binary}')
    if len(arches)>1:
        tmp=binary.with_suffix(binary.suffix+'.thin')
        subprocess.run(['/usr/bin/lipo',str(binary),'-thin','arm64','-output',str(tmp)],check=True)
        os.replace(tmp,binary)
    changes=[]
    identities=subprocess.check_output(['/usr/bin/otool','-D',str(binary)],text=True).splitlines()[1:]
    for dep in dependencies(binary):
        if dep in identities:continue
        if not non_system(dep):continue
        original=originals[binary]
        if dep.startswith('/'):
            source=Path(dep).resolve()
        elif dep.startswith('@loader_path/'):
            source=(original.parent/dep.removeprefix('@loader_path/')).resolve()
        elif dep.startswith('@rpath/'):
            name=Path(dep).name
            if name in known:source=known[name]
            else:
                options=[original.parent/name,Path('/opt/homebrew/lib')/name]
                matches=[x.resolve() for x in options if x.exists()]
                if not matches:raise ValueError(f'Cannot resolve {dep} from {original}')
                source=matches[0]
        else:raise ValueError(f'Unexpected dependency: {dep}')
        # LC_ID_DYLIB is printed by otool alongside dependencies.
        if source == original or source == binary.resolve():continue
        name='libPython3.14.dylib' if source.name=='Python' else source.name
        target=frameworks/name
        if not target.exists():
            shutil.copy2(source,target);originals[target]=source;queue.append(target)
        elif target in originals and originals[target] != source:
            if hashlib.sha256(target.read_bytes()).digest()!=hashlib.sha256(source.read_bytes()).digest():
                raise ValueError(f'Conflicting library {name}: {source}')
        new='@loader_path/'+os.path.relpath(target,binary.parent)
        changes += ['-change',dep,new]
    # Remove inherited build-tree rpaths; every non-system load is now relative.
    lines=subprocess.check_output(['/usr/bin/otool','-l',str(binary)],text=True).splitlines()
    for i,line in enumerate(lines):
        if line.strip()=='cmd LC_RPATH':changes += ['-delete_rpath',lines[i+2].strip().split(' (offset')[0].removeprefix('path ')]
    if binary.parent==frameworks:changes += ['-id','@rpath/'+binary.name]
    if changes:subprocess.run(['/usr/bin/install_name_tool',*changes,str(binary)],check=True)
    subprocess.run(['/usr/bin/codesign','--force','--sign','-',str(binary)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
# Relative ICD library_path is relative to this manifest, per Vulkan loader.
(rt/'MoltenVK_icd.json').write_text(json.dumps({'file_format_version':'1.0.0','ICD':{
    'library_path':'../../Frameworks/libMoltenVK.dylib','api_version':'1.4.0','is_portability_driver':True}},indent=2))
licenses=resources/'Licenses';licenses.mkdir()
for name in ('LICENSE','COPYING'):
    if (repo/name).exists():shutil.copy2(repo/name,licenses/('gow3-'+name))
(licenses/'NOTICE.txt').write_text('Local development build for the owner of the selected dump. The full game dump remains external.\n'
    'Optional launcher artwork and title font are provided locally by the user; no game font atlas is copied.\n'
    'Profiles contains private game-derived ARM64 code and linked images. Do not redistribute this app.\n'
    'gow3_pc and bloodborne-recomp: GPL-2.0-or-later; source and patches accompany this local project.\n'
    'MoltenVK (Apache-2.0), Vulkan Loader (Apache-2.0), SDL3 (zlib), Python (PSF), FFmpeg and their codec dependencies retain their licenses.\n')
# Preserve installed package license texts for bundled third-party dependencies.
for source in set(originals.values()):
    parts=source.parts
    if 'Cellar' in parts:
        idx=parts.index('Cellar');package=Path(*parts[:idx+3]);label=parts[idx+1]
        for item in package.iterdir():
            if item.is_file() and any(x in item.name.upper() for x in ('LICENSE','COPYING','NOTICE','COPYRIGHT')):
                dest=licenses/label;dest.mkdir(exist_ok=True);shutil.copy2(item,dest/item.name)
for binary in prof.rglob('*.dylib'):
    subprocess.run(['/usr/bin/codesign','--force','--sign','-',str(binary)],stderr=subprocess.DEVNULL,check=True)
    metadata_path=binary.parent.parent/'profile.json'
    metadata=json.loads(metadata_path.read_text())
    metadata['source_library_sha256']=metadata['library_sha256']
    metadata['library_sha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
    metadata_path.write_text(json.dumps(metadata,indent=2))
subprocess.run(['/usr/bin/codesign','--force','--sign','-',str(rt/'bin/gow3-probe-arm64.app')],check=True)
subprocess.run(['/usr/bin/codesign','--force','--sign','-',str(app)],check=True)
subprocess.run(['/usr/bin/codesign','--verify','--deep','--strict',str(app)],check=True)
(resources/'build-manifest.json').write_text(json.dumps({'architecture':'arm64','minimum_macos':'26.0','profiles':profile_count,
    'bundled_libraries':len(list(frameworks.iterdir())),'full_game_dump_bundled':False,
    'local_private_aot':not a.runtime_only,'runtime_staging_only':a.runtime_only,
    'display_info_hud':True,'default_language':'en','branding':branding},indent=2))
# The manifest is a sealed resource too.
subprocess.run(['/usr/bin/codesign','--force','--sign','-',str(app)],check=True)
print('Built',app,flush=True)
