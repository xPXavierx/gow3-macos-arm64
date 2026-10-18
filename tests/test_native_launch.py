"""Prepared-AOT launch contracts with synthetic images, never game-derived bytes."""
import json
from pathlib import Path
import struct
import tempfile
import sys
import unittest
from unittest import mock

from paths import ROOT
sys.path.insert(0, str(ROOT))
import native_launch as n
import patches


class NativePatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        # One executable and one writable segment, with a non-file-backed tail.
        image = bytes(range(64))
        self.boot = struct.pack('<8s6Q', b'G3PROBE5', 64, 0, 2, 0, 0, 0)+bytes(40)
        self.boot += struct.pack('<QQ', 0, 0)+struct.pack('<3Q', 0, 32, 5)+struct.pack('<3Q', 32, 32, 6)+image

    def tearDown(self):
        self.temp.cleanup()

    def check(self, writes):
        patches.write_patches(self.root, writes)
        n.validate_writes(self.boot, self.root/'patches.bin')

    def test_baked_code_and_data_initialization_are_allowed(self):
        self.check([(4, bytes(range(4, 8))), (56, b'NEW DATA')])

    def test_new_code_requires_recompilation(self):
        with self.assertRaisesRegex(ValueError, 'recompile'):
            self.check([(4, b'NEW CODE')])

    def test_write_crossing_code_boundary_cannot_bypass_check(self):
        with self.assertRaises(ValueError):
            self.check([(30, b'BAD DATA')])

    def test_range_and_truncation_rejected(self):
        for writes in ([(63, b'XX')], [(0, b'')]):
            with self.assertRaises(ValueError):
                self.check(writes)
        patches.write_patches(self.root, [(56, b'DATA')])
        f = self.root/'patches.bin'
        f.write_bytes(f.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            n.validate_writes(self.boot, f)

    def test_deselecting_baked_patch_is_rejected(self):
        session = object.__new__(n.Session)
        session.patches = {'names': ['a baked code fix']}
        with self.assertRaisesRegex(ValueError, 'deselected'):
            session.verify_patches([])

    def test_wrong_host_architecture_is_rejected_before_launch(self):
        f = self.root/'probe'
        f.write_bytes(struct.pack('<II', 0xfeedfacf, 0x1000007))
        with self.assertRaisesRegex(ValueError, 'ARM64'):
            n.require_arm64(f)
        f.write_bytes(struct.pack('<II', 0xfeedfacf, 0x100000c))
        n.require_arm64(f)

    def test_existing_config_is_migrated_without_changing_async_options(self):
        session = object.__new__(n.Session)
        session.patches = {'names': ['a texture fix']}
        f = self.root/'display.ini'
        f.write_text('render_resolution=1920x1080\nstale_readback=0\noutput_res=1920x1080\n')
        session.seed_config(f)
        import graphics_options as g
        options = g.read_ini(f)
        self.assertEqual(options['engine_fps'], '60')
        self.assertEqual(options['render_resolution'], 'native')
        self.assertEqual(options['stale_readback'], '0')
        self.assertEqual(options['output_res'], '1920x1080')


class SharedStartupTests(unittest.TestCase):
    def test_native_launch_uses_shared_graphics_patches_and_save_backups(self):
        import run
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game, out, user = root/'game', root/'out', root/'user'
            for path in (game, out, user/'savedata'): path.mkdir(parents=True)
            (game/'eboot.bin').write_bytes(b'synthetic')
            (user/'savedata/save').write_bytes(b'player save')
            probe = root/'probe'
            probe.touch()
            config = root/'graphics.ini'
            config.write_text('engine_fps=60\nrender_resolution=native\n')
            native = mock.Mock(out=out)
            native.patches = {'names': ['Resolution Patch - 2272x1278']}
            calls = []
            def script(name, *args, **kwargs):
                calls.append(name)
                if name == 'mods.py': return str(game)
                if name == 'patches.py':
                    (out/'patch_status.json').write_text(json.dumps({'applied': [
                        'Bug Fix - Texture Corruption Fix', 'Bug Fix - Startup Black Screen']}))
            process = mock.Mock()
            process.wait.return_value = 0
            process.poll.return_value = 0
            environment = {'GOW3_GAME_DIR': str(game), 'GOW3_DATA_DIR': str(root),
                           'GOW3_CONFIG': str(config), 'GOW3_PROBE': str(probe), 'GOW3_USER_DIR': str(user)}
            import os, sys
            previous_dir = Path.cwd()
            try:
                with mock.patch.dict(os.environ, environment, clear=True), mock.patch.object(sys, 'argv', ['run.py']), \
                     mock.patch.object(run, 'run_script', side_effect=script), \
                     mock.patch.object(patches, 'game_profile_for', return_value=patches.GAMES[0]), \
                     mock.patch.object(run, 'glyph_mounts', return_value=([], None)), \
                     mock.patch.object(run.subprocess, 'Popen', return_value=process), \
                     mock.patch.object(run, 'launch_prefix', return_value=[]):
                    self.assertEqual(run.launch(native), 0)
                    for key in ('GOW3_READBACK_LINEAR', 'GOW3_DEFERRED_READBACK', 'GOW3_STALE_READBACK', 'GOW3_ASYNC_SHADERS'):
                        self.assertEqual(os.environ[key], '1')
                    self.assertEqual(os.environ['GOW3_ENGINE_FPS'], '60')
                    self.assertEqual(os.environ['GOW3_EXTRA_RESOLUTIONS'], '2272x1278')
                    if sys.platform == 'darwin':
                        self.assertEqual(os.environ['GOW3_LAZY_COLOR'], '1')
                    self.assertEqual(os.environ['GOW3_DMEM_MB'], '7104')
                    self.assertNotIn('GOW3_PREP_WORKERS', os.environ)
                    self.assertNotIn('GOW3_DRAW_PIPE', os.environ)
                    self.assertNotIn('GOW3_FULL_BARRIERS', os.environ)
                    native.verify_inputs.assert_called_once_with(game)
                    native.verify_patches.assert_called_once()
                    self.assertEqual(calls, ['mods.py', 'content_profile.py', 'patches.py'])
                    copies = list((user/'savedata_backups').glob('*/save'))
                    self.assertEqual(len(copies), 1)
                    self.assertEqual(copies[0].read_bytes(), b'player save')
            finally:
                os.chdir(previous_dir)

    def test_native_restart_releases_session_before_next_launch(self):
        import os, run
        sessions = []
        class SyntheticSession:
            def __init__(self, *_): pass
            def __enter__(self): sessions.append('open'); return self
            def __exit__(self, *_): sessions.append('close')
        def start(_):
            sessions.append('launch')
            return 75 if sessions.count('launch') == 1 else 0
        with mock.patch.dict(os.environ, {'GOW3_NATIVE_AOT_DIR': '/synthetic'}), \
             mock.patch.object(n, 'Session', SyntheticSession), mock.patch.object(run, 'launch', side_effect=start):
            self.assertEqual(run.main(), 0)
        self.assertEqual(sessions, ['open', 'launch', 'close', 'open', 'launch', 'close'])


if __name__ == '__main__':
    unittest.main()
