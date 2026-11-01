"""Distribution boundaries and recovery contracts; no copyrighted game fixtures."""
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'packaging/macos/launcher'))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import community as c


class CommunityPreparationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_incomplete_or_modified_profile_is_never_reused(self):
        d = self.root / 'profile'; d.mkdir()
        (d / 'game.dylib').write_bytes(b'synthetic native output')
        self.assertFalse(c.valid_cache(d, 'recipe-1'))
        c.seal(d, ['game.dylib'], {'recipe': 'recipe-1'})
        self.assertTrue(c.valid_cache(d, 'recipe-1'))
        self.assertFalse(c.valid_cache(d, 'recipe-2'))
        (d / 'game.dylib').write_bytes(b'damaged cached output')
        self.assertFalse(c.valid_cache(d, 'recipe-1'))

    def fixture(self):
        game = self.root / 'Dump'; game.mkdir()
        (game / 'eboot.bin').write_bytes(b'synthetic executable')
        (game / 'sce_module').mkdir()
        (game / 'sce_module/libc.prx').write_bytes(b'tested libc')
        (game / 'sce_module/libSceFios2.prx').write_bytes(b'tested fios')
        data = game / 'EXEC/WAD/PS4_LE/SHELL.WAD'; data.parent.mkdir(parents=True); data.write_bytes(b'data')
        expected = {'title_id': 'CUSA01623', 'files': {name: c.digest(game / name) for name in
            ('eboot.bin', 'sce_module/libc.prx', 'sce_module/libSceFios2.prx')}}
        (self.root / 'supported-game.json').write_text(json.dumps(expected))
        return game

    def test_executable_hash_is_authoritative_over_sfo_version(self):
        game = self.fixture()
        with mock.patch('prepare.read_sfo', return_value={'TITLE_ID': 'CUSA01623', 'APP_VER': '01.00'}):
            c.validate_dump(self.root, game)
            (game / 'eboot.bin').write_bytes(b'other update or modified executable')
            with self.assertRaises(c.PreparationError) as error:
                c.validate_dump(self.root, game)
            self.assertEqual(error.exception.code, 'dump_version')

    def test_player_accepted_executable_is_opt_in_and_never_a_module(self):
        game = self.fixture()
        (game / 'eboot.bin').write_bytes(b'second retail extraction of the same title')
        other = c.digest(game / 'eboot.bin')
        sfo = {'TITLE_ID': 'CUSA01623', 'APP_VER': '01.02'}
        with mock.patch('prepare.read_sfo', return_value=sfo):
            # Absent override file: the tested executable stays authoritative.
            with self.assertRaises(c.PreparationError) as error:
                c.validate_dump(self.root, game, self.root)
            self.assertEqual(error.exception.code, 'dump_unverified')
            self.assertEqual(error.exception.details['executable_sha256'], other)
            override = self.root / 'accepted-executables.json'
            override.write_text(json.dumps({'eboot.bin': [other]}))
            result = c.validate_dump(self.root, game, self.root)
            # The matched digest keys the preparation cache, and the override is
            # reported so a prepared profile is never silently attributed to the
            # tested build.
            self.assertEqual(result['files']['eboot.bin'], other)
            self.assertEqual(result['player_accepted'], {'eboot.bin': other})
            # A different module is never accepted, whatever the player declares.
            override.write_text(json.dumps({'sce_module/libc.prx': [other]}))
            with self.assertRaises(c.PreparationError) as error:
                c.validate_dump(self.root, game, self.root)
            self.assertEqual(error.exception.code, 'accepted_invalid')
            override.write_text(json.dumps({'eboot.bin': ['not-a-digest']}))
            with self.assertRaises(c.PreparationError) as error:
                c.validate_dump(self.root, game, self.root)
            self.assertEqual(error.exception.code, 'accepted_invalid')
            override.write_text('{')
            with self.assertRaises(c.PreparationError) as error:
                c.validate_dump(self.root, game, self.root)
            self.assertEqual(error.exception.code, 'accepted_invalid')

    def test_consent_survives_replaced_app_and_changed_preparation_recipe(self):
        game = self.fixture()
        (game / 'eboot.bin').write_bytes(b'another executable')
        sha = c.digest(game / 'eboot.bin')
        support = self.root / 'Support'
        with mock.patch('prepare.read_sfo', return_value={'TITLE_ID': 'CUSA01623', 'APP_VER': '01.02'}):
            c.accept_executable(self.root, support, game, sha)
            approval = (support / c.ACCEPTED_OVERRIDE).read_bytes()
            for release, recipe in [('old-app', 'old-recipe'), ('new-app', 'new-recipe')]:
                resources = self.root / release; resources.mkdir()
                (resources / 'supported-game.json').write_bytes((self.root / 'supported-game.json').read_bytes())
                (resources / 'build-manifest.json').write_text(json.dumps({'preparation_recipe': recipe}))
                out = support / 'Prepared' / recipe / sha[:16] / 'Profiles/review'
                c.atomic_json(out / 'profile.json', {'source': sha, 'recipe': recipe})
                c.seal(out, ['profile.json'], {'recipe': recipe})
                with mock.patch.object(c.sys, 'platform', 'darwin'), mock.patch.object(c.platform, 'machine', return_value='arm64'):
                    actual, _, metadata = c.ensure_profile(resources, support, game, {'profile': 'review'})
                self.assertEqual(actual, out)
                self.assertEqual(metadata['source'], sha)
                self.assertEqual((support / c.ACCEPTED_OVERRIDE).read_bytes(), approval)
            (support / c.ACCEPTED_OVERRIDE).unlink()
            with self.assertRaises(c.PreparationError) as error:
                c.validate_dump(resources, game, support)
            self.assertEqual(error.exception.code, 'dump_unverified')

    def test_module_and_title_failures_never_offer_executable_consent(self):
        game = self.fixture()
        (game / 'eboot.bin').write_bytes(b'another executable')
        sha = c.digest(game / 'eboot.bin')
        with mock.patch('prepare.read_sfo', return_value={'TITLE_ID': 'CUSA01623'}):
            for name in ('libc.prx', 'libSceFios2.prx'):
                path = game / 'sce_module' / name; original = path.read_bytes()
                path.write_bytes(b'different module')
                with self.assertRaises(c.PreparationError) as error:
                    c.accept_executable(self.root, self.root, game, sha)
                self.assertEqual(error.exception.code, 'dump_version')
                self.assertFalse((self.root / c.ACCEPTED_OVERRIDE).exists())
                path.write_bytes(original)
        with mock.patch('prepare.read_sfo', return_value={'TITLE_ID': 'CUSA99999'}):
            with self.assertRaises(c.PreparationError) as error:
                c.accept_executable(self.root, self.root, game, sha)
            self.assertEqual(error.exception.code, 'dump_version')

    def test_changed_hash_requires_fresh_consent_without_erasing_prior_approval(self):
        game = self.fixture()
        (game / 'eboot.bin').write_bytes(b'alternative one')
        first = c.digest(game / 'eboot.bin')
        with mock.patch('prepare.read_sfo', return_value={'TITLE_ID': 'CUSA01623'}):
            c.accept_executable(self.root, self.root, game, first)
            approval = (self.root / c.ACCEPTED_OVERRIDE).read_bytes()
            (game / 'eboot.bin').write_bytes(b'alternative two')
            second = c.digest(game / 'eboot.bin')
            with self.assertRaises(c.PreparationError) as error:
                c.accept_executable(self.root, self.root, game, first)
            self.assertEqual(error.exception.code, 'accepted_changed')
            self.assertEqual((self.root / c.ACCEPTED_OVERRIDE).read_bytes(), approval)
            with self.assertRaises(c.PreparationError) as error:
                c.validate_dump(self.root, game, self.root)
            self.assertEqual(error.exception.code, 'dump_unverified')
            c.accept_executable(self.root, self.root, game, second)
            self.assertEqual(c.player_accepted(self.root)['eboot.bin'], {first, second})

    def test_consent_does_not_write_while_a_session_owns_the_lock(self):
        import fcntl
        game = self.fixture()
        with (self.root / 'session.lock').open('a+') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaises(c.PreparationError) as error:
                c.accept_executable(self.root, self.root, game, c.digest(game / 'eboot.bin'))
            self.assertEqual(error.exception.code, 'accepted_busy')
            self.assertFalse((self.root / c.ACCEPTED_OVERRIDE).exists())

    def test_launcher_consent_command_runs_isolated_and_rechecks_the_digest(self):
        game = self.fixture()
        (game / 'eboot.bin').write_bytes(b'alternative executable')
        sha = c.digest(game / 'eboot.bin')
        (game / 'sce_sys').mkdir()
        keys, values, entries = bytearray(), bytearray(), bytearray()
        for key, value in [('TITLE_ID', 'CUSA01623'), ('APP_VER', '01.02')]:
            data = value.encode() + b'\0'
            entries += struct.pack('<HHIII', len(keys), 0x204, len(data), len(data), len(values))
            keys += key.encode() + b'\0'; values += data
        header = struct.pack('<4sIIII', b'\0PSF', 0x101, 20+len(entries), 20+len(entries)+len(keys), 2)
        (game / 'sce_sys/param.sfo').write_bytes(header + entries + keys + values)
        scripts = self.root / 'Runtime/scripts'; scripts.mkdir(parents=True)
        (scripts / 'prepare.py').write_bytes((Path(__file__).resolve().parents[1] / 'scripts/prepare.py').read_bytes())
        support = self.root / 'Support'
        def command(digest):
            return subprocess.run([sys.executable, '-I', '-S', '-B', c.__file__,
                '--resources', self.root, '--support', support, '--game', game,
                '--accept-executable', digest], capture_output=True, text=True)
        refused = command('0'*64)
        self.assertEqual(refused.returncode, 1)
        self.assertEqual(json.loads(refused.stdout)['error_code'], 'accepted_changed')
        self.assertFalse((support / c.ACCEPTED_OVERRIDE).exists())
        accepted = command(sha)
        self.assertEqual(accepted.returncode, 0, accepted.stderr)
        self.assertEqual(json.loads(accepted.stdout), {'accepted': True, 'executable_sha256': sha})
        self.assertEqual(c.player_accepted(support)['eboot.bin'], {sha})
        again = command(sha)
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertEqual(json.loads(again.stdout)['executable_sha256'], sha)

    def test_wrong_region_and_missing_game_data_are_explained(self):
        game = self.fixture()
        with mock.patch('prepare.read_sfo', return_value={'TITLE_ID': 'CUSA99999'}):
            with self.assertRaises(c.PreparationError) as error:
                c.validate_dump(self.root, game)
            self.assertEqual(error.exception.code, 'dump_version')
        (game / 'EXEC/WAD/PS4_LE/SHELL.WAD').unlink()
        with mock.patch('prepare.read_sfo', return_value={'TITLE_ID': 'CUSA01623'}):
            with self.assertRaises(c.PreparationError) as error:
                c.validate_dump(self.root, game)
            self.assertEqual(error.exception.code, 'dump_missing')

    def test_cancellation_stops_active_compilers_and_rejects_new_work(self):
        commands = c.Commands(dict(os.environ))
        errors = []
        def run():
            try:
                commands.run([sys.executable, '-c', 'import time; time.sleep(30)'])
            except subprocess.CalledProcessError as error:
                errors.append(error)
        worker = threading.Thread(target=run); worker.start()
        deadline = time.monotonic() + 5
        while not commands.children and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertTrue(commands.children)
        children = list(commands.children)
        commands.cancel(); worker.join(timeout=5)
        self.assertFalse(worker.is_alive())
        self.assertTrue(all(child.poll() is not None for child in children))
        self.assertTrue(errors)
        with self.assertRaises(InterruptedError):
            commands.run([sys.executable, '-c', 'raise RuntimeError()'])


if __name__ == '__main__':
    unittest.main()
