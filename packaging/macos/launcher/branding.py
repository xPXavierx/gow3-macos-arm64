#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Copy optional user-supplied artwork and build a full-resolution macOS icon.

Branding files are local inputs, not fetched or installed as system fonts.
The app remains usable with the repository's existing icon/title font alone.
"""
from pathlib import Path
import argparse
import shutil
import struct
import subprocess
import tempfile


def install_branding(resources: Path, directory: Path | None = None) -> dict:
    repo = Path(__file__).resolve().parents[3]
    destination = resources / 'Branding'
    destination.mkdir(parents=True, exist_ok=True)
    icon = repo / 'launcher/gow3.png'
    font = repo / 'launcher/gowfont.ttf'
    if directory is not None:
        # Fail on a partial/mistyped branding input instead of silently mixing it
        # with the stock assets. All three files came from the user in this build.
        for name in ('GOW-3-Icon.jpg', 'GOW-3-Art.jpg', 'GODOFWAR.TTF'):
            if not (directory / name).is_file():
                raise FileNotFoundError(directory / name)
        icon = directory / 'GOW-3-Icon.jpg'
        font = directory / 'GODOFWAR.TTF'
        shutil.copy2(directory / 'GOW-3-Art.jpg', destination / 'GOW-3-Art.jpg')
    shutil.copy2(font, destination / 'GODOFWAR.TTF')
    # Native build-time clipping, transparent margins and shadow. Artwork and
    # launcher controls are unchanged; only the application icon is rounded.
    chunks = []
    with tempfile.TemporaryDirectory(prefix='gow3-icon-') as temporary:
        converter = Path(temporary) / 'make-icon'
        subprocess.run(['/usr/bin/xcrun','swiftc','-O','-target','arm64-apple-macos14.0',
                        '-module-cache-path',str(Path(temporary)/'swift-cache'),
                        str(Path(__file__).with_name('Icon.swift')),'-o',str(converter)],check=True)
        subprocess.run([str(converter),str(icon),temporary],check=True)
        for code, size in (('icp4',16),('icp5',32),('icp6',64),('ic07',128),
                           ('ic08',256),('ic09',512),('ic10',1024)):
            target = Path(temporary) / f'{size}.png'
            data = target.read_bytes()
            chunks.append(code.encode() + struct.pack('>I',len(data)+8) + data)
    payload = b''.join(chunks)
    (resources / 'AppIcon.icns').write_bytes(b'icns'+struct.pack('>I',len(payload)+8)+payload)
    return {'user_artwork':directory is not None,'title_font':'GodOfWar',
            'menu_font':'Copperplate-Bold (macOS)','icon_max_pixels':1024,
            'icon_shape':'rounded-square','icon_transparent_corners':True}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--branding-directory', type=Path)
    args = parser.parse_args()
    print(install_branding(args.resources, args.branding_directory))
