#!/usr/bin/env python3
"""Verify the supported macOS source surface without a game dump or GPU."""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
removed=('README.upstream.md','launcher','extras/mods/Ultrawide','packaging/windows',
         'packaging/linux','packaging/appimage.sh','packaging/default.nix','msys2-env.sh',
         'shell.nix','docs/agents','docs/ultrawide.md','gpu/dlss_bridge',
         'gpu/shadps4/video_core/renderer_vulkan/vk_fsr4.cpp',
         'gpu/shadps4/video_core/renderer_vulkan/vk_dlss.cpp')
errors=[name+' is outside the macOS distribution' for name in removed if (root/name).exists()]
for file in [*root.glob('README*.md'),*root.glob('docs/**/*.md'),*root.glob('packaging/macos/**/*.md')]:
    for link in re.findall(r'\]\(([^)]+)\)',file.read_text()):
        link=link.strip('<>').split('#')[0]
        if not link or ':' in link or link.startswith('/'):continue
        if not (file.parent/link).exists():errors.append(str(file.relative_to(root))+': missing '+link)
patches=(root/'patches/God_of_War_III_Remastered.xml').read_text()
if 'Ultrawide' in patches or re.search(r'Name="Resolution Patch - (?:2560x1080|3440x1440|3840x1600|5120x1440|1920x1200|2560x1600)"',patches):
    errors.append('Unsupported aspect-ratio patches are still exposed')
modules=(root/'.gitmodules').read_text()
for unused in ('fsr-vulkan','xbyak'):
    if unused in modules:errors.append('Unused submodule '+unused)
cmake=(root/'gpu/CMakeLists.txt').read_text()
for unused in ('../packaging/windows/','ffx-vulkan::','add_subdirectory(third_party/fsr-vulkan'):
    if unused in cmake:errors.append('Inactive platform/build dependency: '+unused)
if errors:raise SystemExit('\n'.join(errors))
print('macOS source scope, submodules and documentation links verified')
