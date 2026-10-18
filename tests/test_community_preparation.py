"""Distribution boundaries and recovery contracts; no copyrighted game fixtures."""
import json
import os
from pathlib import Path
import subprocess
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
        data = game / 'EXEC/WAD/PS4_LE/SHELL.WAD'; data.parent.mkdir(parents=True); data.write_bytes(b'data')
        expected = {'title_id': 'CUSA01623', 'files': {'eboot.bin': c.digest(game / 'eboot.bin')}}
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
