#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The prepared-AOT part of run.py; all game startup policy stays in run.py.

Native code cannot follow an instruction patch applied only to guest memory.
Validate the prepared inputs and runtime writes before opening the game. Data
initialization patches (including the upstream startup fix) still use patches.py.
"""
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys

RESTART_EXIT = 75


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require_arm64(path):
    with Path(path).open('rb') as source:
        if source.read(8) != struct.pack('<II', 0xfeedfacf, 0x100000c):
            raise ValueError(f'{Path(path).name} is not a thin ARM64 Mach-O binary')


def validate_writes(boot, patch_file):
    """Allow data initialization and idempotent baked code writes, never new code."""
    from isa_rewrite import read_boot_linked
    segments, image = read_boot_linked(boot)
    data = Path(patch_file).read_bytes()
    if len(data) < 24 or data[:8] != b'G3PATCH2':
        raise ValueError('Invalid native runtime patch table')
    count, = struct.unpack_from('<Q', data, 16)
    pos = 24
    for _ in range(count):
        if pos+16 > len(data):
            raise ValueError('Truncated native runtime patch table')
        at, size = struct.unpack_from('<QQ', data, pos)
        pos += 16
        end = at+size
        if not size or size > 4096 or pos+size > len(data) or not any(a <= at and end <= a+n for a,n,f in segments):
            raise ValueError('Invalid native runtime patch range')
        for start, length, flags in segments:
            if flags & 1 and at < start+length and start < end:
                lo, hi = max(at, start), min(end, start+length)
                if image[lo:hi] != data[pos+lo-at:pos+hi-at]:
                    raise ValueError('Selected patch changes native instructions; prepare and recompile this patch selection first')
        pos += size
    if pos != len(data):
        raise ValueError('Trailing bytes in native runtime patch table')


class Session:
    def __init__(self, out, env):
        if sys.platform != 'darwin' or platform.machine() != 'arm64':
            raise ValueError('The native AOT launcher requires macOS and an ARM64 Python process (no Rosetta)')
        self.out, self.env = Path(out).resolve(), env
        self.lock = None
        self.pid_file = self.out/'.native-test.pid'
        manifest = self.out/'aot-patches.json'
        self.patches = json.loads(manifest.read_text()) if manifest.exists() else {'names': []}

    def __enter__(self):
        import fcntl
        self.lock = (self.out/'.native-test.lock').open('a+')
        try:
            fcntl.flock(self.lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            self.lock.close()
            raise ValueError('Another native launch is using this prepared image') from None
        # Covers an orphan from an interrupted supervisor as well as other user dirs.
        running = subprocess.run(['/usr/bin/pgrep', '-x', 'gow3-probe-arm64'], capture_output=True, text=True)
        if running.returncode == 0:
            self.lock.close()
            raise ValueError('A native game process is already running; no second instance was opened')
        try:
            return self.enter_locked()
        except BaseException:
            self.lock.close()
            raise

    def enter_locked(self):
        self.pid_file.unlink(missing_ok=True)
        self.env['BB_JIT'] = '0'
        library = Path(self.env.get('GOW3_RECOMP_LIB', self.out/'aot/libgow3-recompiled.dylib')).resolve()
        self.env['GOW3_RECOMP_LIB'] = str(library)
        # A thin ARM64 Mach-O is required; never silently select an x86 build.
        require_arm64(library)
        require_arm64(self.env['GOW3_PROBE'])
        self.env.setdefault('GOW3_PATCHES', ';'.join(self.patches['names']))
        self.env.setdefault('GOW3_PATCHES_ONLY', '1')
        self.env['GOW3_NATIVE_SUPERVISED'] = '1'
        if self.env.get('GOW3_NATIVE_LOG_LINK') and self.env.get('GOW3_NATIVE_LOG_FILE'):
            latest = Path(self.env['GOW3_NATIVE_LOG_LINK'])
            latest.unlink(missing_ok=True)
            latest.symlink_to(Path(self.env['GOW3_NATIVE_LOG_FILE']))
        return self

    def __exit__(self, *_):
        self.pid_file.unlink(missing_ok=True)
        if self.lock:
            self.lock.close()

    def seed_config(self, config):
        """Keep the existing AOT rate until explicitly rebuilt with another code patch."""
        import graphics_options as g
        ini = g.read_ini(config)
        updates = {}
        if 'engine_fps' not in ini:
            updates['engine_fps'] = str(g.applied_engine_fps(self.patches['names']))
        # The old test harness wrote a pixel size where upstream expects 'native'.
        if ini.get('render_resolution') == '1920x1080':
            updates['render_resolution'] = 'native'
        if updates:
            g.write_keys(config, updates)

    def verify_inputs(self, game):
        analysis = json.loads((self.out/'analysis.json').read_text())
        linked = json.loads((self.out/'link.json').read_text())
        if sha256(Path(game)/'eboot.bin') != analysis['source_sha256']:
            raise ValueError('The game/update/mod executable differs from the AOT input; prepare and recompile it first')
        for module in linked['modules']:
            if sha256(Path(game)/'sce_module'/module['file']) != module['sha256']:
                raise ValueError(f"AOT module {module['file']} changed; prepare and recompile it first")
        # New preparations always seal the linked image, including unpatched builds.
        seal = self.out/'native-inputs.json'
        expected = json.loads(seal.read_text())['boot_sha256'] if seal.exists() else self.patches.get('boot_sha256')
        if not expected or sha256(self.out/'boot-linked.bin') != expected:
            raise ValueError('Prepared native image hash does not match its manifest')
        print('Native AOT: ARM64 inputs verified; Rosetta/JIT/interpreter disabled', flush=True)

    def verify_patches(self, applied):
        missing = set(self.patches['names'])-set(applied)
        if missing:
            raise ValueError(f'Baked AOT patches were deselected: {sorted(missing)}; prepare and recompile this selection first')
        validate_writes((self.out/'boot-linked.bin').read_bytes(), self.out/'patches.bin')
        print('Native AOT: runtime patches verified against compiled instructions', flush=True)

    def record_policy(self, graphics, applied):
        keys = ('GOW3_READBACK_LINEAR', 'GOW3_DEFERRED_READBACK', 'GOW3_STALE_READBACK',
                'GOW3_ASYNC_SHADERS', 'GOW3_ENGINE_FPS', 'GOW3_DMEM_MB', 'GOW3_VBLANK_HZ',
                'GOW3_PREP_WORKERS', 'GOW3_DRAW_PIPE', 'GOW3_FULL_BARRIERS', 'GOW3_UPSCALER')
        settings = {key: self.env.get(key, 'renderer default') for key in keys}
        (self.out/'native-launch-policy.json').write_text(json.dumps({
            'launcher': 'run.py', 'architecture': 'arm64', 'graphics': graphics,
            'applied_patches': applied, 'runtime': settings,
        }, indent=2)+'\n')
        print('Launch policy: '+json.dumps(settings, sort_keys=True), flush=True)

    def child_started(self, pid):
        self.pid_file.write_text(str(pid))
