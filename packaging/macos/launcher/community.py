#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline first-run AOT preparation. No generated game data belongs in the app.

All inputs come from the selected plaintext dump. The bundled native scanner,
generator and Zig C toolchain run without Xcode, Homebrew or downloaded code.
Only a sealed, successfully linked profile becomes available for launch.
"""
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import struct
import subprocess
import sys
import threading
import time


class PreparationError(ValueError):
    def __init__(self, code, detail):
        super().__init__(detail)
        self.code = code


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    os.replace(temporary, path)


def status(support, phase, fraction=None, **details):
    atomic_json(support / 'preparation-status.json', {
        'phase': phase, 'fraction': fraction, 'updated': time.time(), **details})
    print('PREPARATION: ' + json.dumps({'phase': phase, **details}), flush=True)


def validate_dump(resources, game):
    sys.path.insert(0, str(resources / 'Runtime/scripts'))
    from prepare import read_sfo
    expected = json.loads((resources / 'supported-game.json').read_text())
    sfo = read_sfo(game / 'sce_sys/param.sfo')
    if not sfo or sfo.get('TITLE_ID') != expected['title_id']:
        raise PreparationError('dump_version', 'Expected an extracted God of War III Remastered CUSA01623 dump.')
    for relative, sha in expected['files'].items():
        source = game / relative
        if not source.is_file():
            raise PreparationError('dump_missing', 'Required file is missing: ' + relative)
        if digest(source) != sha:
            raise PreparationError('dump_version', 'Unsupported executable/module: ' + relative +
                '. Tested CUSA01623 v1.02, eboot SHA-256 ' + expected['files']['eboot.bin'] +
                '. The SFO version alone is not sufficient; this dump reports APP_VER ' + str(sfo.get('APP_VER')) + '.')
    # Do not accept a folder containing executable placeholders but no game data.
    data = game / 'EXEC/WAD/PS4_LE/SHELL.WAD'
    if not data.is_file() or data.stat().st_size == 0:
        raise PreparationError('dump_missing', 'The extracted game data is missing (EXEC/WAD/PS4_LE/SHELL.WAD).')
    return expected


class Commands:
    """Track all subprocess groups, including parallel compiler workers."""
    def __init__(self, env):
        self.env = env
        self.children = set()
        self.lock = threading.Lock()
        self.cancelled = False

    def run(self, argv, log=None, env=None):
        with self.lock:
            if self.cancelled:
                raise InterruptedError('Preparation cancelled')
            child = subprocess.Popen(list(map(str, argv)), env=env or self.env,
                stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            self.children.add(child)
        try:
            code = child.wait()
            if code:
                raise subprocess.CalledProcessError(code, argv)
        finally:
            with self.lock:
                # An interrupted main-thread wait must leave the live process
                # registered so cancel() can terminate its entire group.
                if child.poll() is not None:
                    self.children.discard(child)

    def cancel(self):
        with self.lock:
            self.cancelled = True
            children = list(self.children)
        for child in children:
            if child.poll() is None:
                try:
                    os.killpg(child.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
        for child in children:
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass


def seal(directory, names, metadata):
    atomic_json(directory / 'complete.json', {
        **metadata, 'files': {n: digest(directory / n) for n in names}})


def valid_cache(directory, recipe):
    try:
        complete = json.loads((directory / 'complete.json').read_text())
        return (complete['recipe'] == recipe and bool(complete['files']) and
            all(digest(directory / n) == sha for n, sha in complete['files'].items()))
    except (OSError, ValueError, KeyError):
        return False


def generate_profile(resources, game, base, out, selection, commands):
    sys.path.insert(0, str(resources / 'Runtime/packaging/macos/native'))
    from link_modules import module
    from isa_rewrite import read_boot_linked
    from patches import compile_patches, eboot_segments, eboot_image, game_profile_for
    from aot_patches import apply_file_writes
    original = module(game / 'eboot.bin')['elf']
    profile = game_profile_for(game)
    w, h = selection['input']
    resolution = {(1920, 1080): 'native', (1280, 720): '720p',
                  (2560, 1440): '1440p', (3840, 2160): '4K'}.get((w, h), f'{w}x{h}')
    names = ['Skip Any Video With X Button', 'Bug Fix - Texture Corruption Fix' if resolution == 'native'
             else 'Resolution Patch - ' + resolution]
    if selection['engine'] != 60:
        names.append(f"Frame Rate Patch - {selection['engine']} FPS")
    writes = compile_patches(resources / 'Runtime/patches/God_of_War_III_Remastered.xml',
        names, profile[2], eboot_segments(original), eboot_image(original))
    _, boot = apply_file_writes(original, (base / 'boot-linked.bin').read_bytes(), writes, read_boot_linked)
    out.mkdir(parents=True, exist_ok=True)
    for name in ('analysis.json', 'link.json'):
        shutil.copy2(base / name, out / name)
    (out / 'boot-linked.bin').write_bytes(boot)
    atomic_json(out / 'native-inputs.json', {'boot_sha256': hashlib.sha256(boot).hexdigest()})
    atomic_json(out / 'aot-patches.json', {'names': names, 'writes': len(writes),
        'boot_sha256': hashlib.sha256(boot).hexdigest()})
    generated = out / 'aot'
    generated.mkdir(exist_ok=True)
    for name in ('functions.tsv', 'offsets.txt'):
        shutil.copy2(base / 'aot' / name, generated / name)
    segments, image = read_boot_linked(boot)
    entry = struct.unpack_from('<Q', boot, 16)[0]
    header = struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01'.ljust(16, b'\0'),
        3, 62, 1, entry, 64, 0, 0, 64, 56, len(segments), 0, 0, 0)
    ph = b''.join(struct.pack('<II6Q', 1, f, 16384+a, a, 0, n, n, 16384) for a, n, f in segments)
    elf = generated / 'linked.elf'
    elf.write_bytes((header + ph).ljust(16384, b'\0') + image)
    for old in generated.glob('recomp*.c'):
        old.unlink()
    with (out / 'generation.log').open('w') as log:
        commands.run([resources / 'Recompiler/bin/gow3-aot-recomp', elf,
            generated / 'functions.tsv', generated / 'recomp.c', '@' + str(generated / 'offsets.txt')], log=log)
    for source in generated.glob('recomp*.c'):
        text = source.read_text().replace('    unsigned poll_spins = 0;\n', '')
        text = re.sub(r' \{ rc_poll_wait\(&poll_spins\); goto (L_[0-9a-f]+); \} else \{ poll_spins = 0; \}', r' goto \1;', text)
        if 'poll_spins' in text or 'rc_poll_wait(' in text:
            raise PreparationError('prepare_failed', 'Unknown generated polling instrumentation')
        source.write_text(text)
    return {**selection, 'resolution_patch': resolution, 'patches': names,
            'generator_policy': 'checkpoint-no-poll-hooks-v1'}


def ensure_profile(resources, support, game, selection):
    started = time.monotonic()
    if sys.platform != 'darwin' or platform.machine() != 'arm64':
        raise PreparationError('architecture', 'An Apple Silicon Mac running ARM64 is required. Rosetta is not supported.')
    status(support, 'checking', 0)
    expected = validate_dump(resources, game)
    manifest = json.loads((resources / 'build-manifest.json').read_text())
    recipe = manifest['preparation_recipe']
    root = support / 'Prepared' / recipe / expected['files']['eboot.bin'][:16]
    out = root / 'Profiles' / selection['profile']
    if valid_cache(out, recipe):
        status(support, 'ready', 1, reused=True, preparation_seconds=round(time.monotonic() - started, 3))
        return out, out, json.loads((out / 'profile.json').read_text())
    root.mkdir(parents=True, exist_ok=True)
    # Leave room for generated C, incremental objects, compiler cache and linking.
    if shutil.disk_usage(root).free < 3 * 1024**3:
        raise PreparationError('disk_space', 'At least 3 GB of free space is required for initial preparation.')
    cache = support / 'CompilerCache'
    cache.mkdir(exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(
        ('GOW3_', 'BB_', 'MVK_', 'VK_', 'MTL_', 'DYLD_', 'PYTHON', 'ZIG_', 'SDKROOT', 'DEVELOPER_DIR', 'CPATH', 'LIBRARY_PATH'))}
    env.update(PATH='/usr/bin:/bin:/usr/sbin:/sbin', BB_JIT='0',
        GOW3_AOT_INCLUDE_STUBS='1', GOW3_AOT_GUARDED_TABLES='1', PYTHONDONTWRITEBYTECODE='1',
        ZIG_GLOBAL_CACHE_DIR=str(cache), ZIG_LOCAL_CACHE_DIR=str(cache / 'local'),
        ZIG_LIB_DIR=str(resources / 'Compiler/lib'))
    commands = Commands(env)
    try:
        base = root / 'Base'
        if not valid_cache(base, recipe):
            status(support, 'scanning', .04)
            base.mkdir(exist_ok=True)
            with (base / 'preparation.log').open('w') as log:
                commands.run([sys.executable, '-B', resources / 'Runtime/packaging/macos/native/prepare_game.py',
                    game, '--out', base, '--tools', resources / 'Recompiler/bin', '--scan-only'], log=log)
            seal(base, ['boot-linked.bin', 'analysis.json', 'link.json', 'aot/functions.tsv', 'aot/offsets.txt'], {'recipe': recipe})
        status(support, 'generating', .13)
        metadata = generate_profile(resources, game, base, out, selection, commands)
        files = sorted((out / 'aot').glob('recomp*.c'))
        if not files:
            raise PreparationError('prepare_failed', 'The recompiler did not generate source files.')
        objects = root / 'Objects'
        objects.mkdir(exist_ok=True)
        flags = ['-target', 'aarch64-macos.26.0', '-O2', '-std=gnu11', '-fPIC',
                 '-ffp-contract=off', '-fno-strict-aliasing', '-fno-sanitize=all',
                 '-I' + str(resources / 'Recompiler/include/src'),
                 '-I' + str(resources / 'Recompiler/include/decoder')]
        done = 0
        status(support, 'compiling', .20, completed=done, total=len(files))

        def compile_one(source):
            key = hashlib.sha256((recipe + '\n').encode() + source.read_bytes()).hexdigest()
            obj = objects / (key + '.o')
            stamp = obj.with_suffix('.sha256')
            if not (obj.exists() and stamp.exists() and stamp.read_text() == digest(obj)):
                temporary = obj.with_suffix('.tmp.o')
                with (out / 'aot' / (source.stem + '.log')).open('w') as log:
                    commands.run([resources / 'Compiler/zig', 'cc', *flags, '-c', source, '-o', temporary], log=log)
                os.replace(temporary, obj)
                stamp.write_text(digest(obj))
            return obj

        pool = ThreadPoolExecutor(max_workers=min(4, max(1, (os.cpu_count() or 4) // 2)))
        try:
            futures = {pool.submit(compile_one, source): i for i, source in enumerate(files)}
            result = [None] * len(files)
            for future in as_completed(futures):
                result[futures[future]] = future.result()
                done += 1
                status(support, 'compiling', .20 + .72 * done / len(files), completed=done, total=len(files))
        except BaseException:
            commands.cancel()
            for future in futures:
                future.cancel()
            raise
        finally:
            pool.shutdown(wait=True, cancel_futures=True)
        status(support, 'linking', .94)
        library = out / 'aot/libgow3-recompiled.dylib'
        temporary = library.with_suffix('.tmp.dylib')
        # A response file avoids ARG_MAX and handles paths containing spaces.
        response = out / 'aot/objects.rsp'
        response.write_text('\n'.join('"' + str(obj).replace('\\', '\\\\').replace('"', '\\"') + '"' for obj in result))
        with (out / 'linking.log').open('w') as log:
            commands.run([resources / 'Compiler/zig', 'cc', '-target', 'aarch64-macos.26.0',
                '-dynamiclib', '@' + str(response), '-o', temporary], log=log)
        from native_launch import require_arm64
        require_arm64(temporary)
        commands.run(['/usr/bin/codesign', '--force', '--sign', '-', temporary])
        commands.run(['/usr/bin/codesign', '--verify', '--strict', temporary])
        os.replace(temporary, library)
        metadata.update(library_sha256=digest(library), compiler='Zig 0.15.2 / Clang 20.1.2',
                        preparation_recipe=recipe, generated_locally=True)
        atomic_json(out / 'profile.json', metadata)
        # Recheck source modules before committing the transaction.
        validate_dump(resources, game)
        seal(out, ['boot-linked.bin', 'analysis.json', 'link.json', 'native-inputs.json',
                   'aot-patches.json', 'aot/libgow3-recompiled.dylib', 'profile.json'], {'recipe': recipe})
        status(support, 'ready', 1, reused=False, preparation_seconds=round(time.monotonic() - started, 3))
        return out, out, metadata
    except BaseException:
        commands.cancel()
        raise
