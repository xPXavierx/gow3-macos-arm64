"""First-run indexing stays separate from saves, preferences and CPU AOT."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from paths import ROOT
sys.path.insert(0, str(ROOT/'packaging/macos/launcher'))
sys.path.insert(0, str(ROOT/'packaging/macos/native'))
import session
from test_shader_asset_catalog import program


class LauncherShaderCatalog(unittest.TestCase):
    def test_prepares_reuses_and_does_not_touch_user_state(self):
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()):
            root = Path(temp); resources = root/'Resources'; catalog = resources/'ShaderCatalog'
            catalog.mkdir(parents=True); support = root/'Support'; support.mkdir()
            wad = root/'Dump/EXEC/WAD/PS4_LE/A.WAD'; wad.parent.mkdir(parents=True); wad.write_bytes(program())
            (catalog/'fixture.recipe').write_bytes(b'description')
            manifest = {'format': 1, 'files': {'fixture.recipe': hashlib.sha256(b'description').hexdigest()}}
            (catalog/'manifest.json').write_text(json.dumps(manifest))
            protected = ['preferences.json', 'User/savedata/slot', 'Prepared/CPU/library']
            for name in protected:
                path = support/name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'keep')
            env = {}; first = session.prepare_graphics_catalog(resources, support, root/'Dump', env)
            self.assertFalse(first['reused'])
            self.assertEqual(first['programs'], 1)
            self.assertTrue(Path(env['GOW3_SHADER_ASSET_INDEX']).is_file())
            self.assertEqual(env['GOW3_SHADER_CATALOG'], str(catalog))
            self.assertTrue(session.prepare_graphics_catalog(resources, support, root/'Dump', env)['reused'])
            for name in protected:
                self.assertEqual((support/name).read_bytes(), b'keep')
            # A damaged optional description corpus does not stop the game or
            # enable partial rendering; startup continues with its normal policy.
            (catalog/'fixture.recipe').write_bytes(b'damaged')
            fallback = {}
            self.assertIsNone(session.prepare_graphics_catalog(resources, support, root/'Dump', fallback))
            self.assertEqual(fallback, {})

    def test_canonical_catalogue_merge_and_invalid_import_fallback(self):
        from test_community_catalog import shader
        import community_catalog as c
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()):
            root=Path(temp);resources=root/'Resources';resources.mkdir();support=root/'Support';support.mkdir()
            wad=root/'Dump/EXEC/WAD/PS4_LE/A.WAD';wad.parent.mkdir(parents=True);wad.write_bytes(program())
            row=shader();c.save(resources/'CommunityCatalog.jsonl',{row['id']:row})
            imported=support/'Community/imported';imported.mkdir(parents=True);(imported/'invalid.jsonl').write_text('bad')
            (support/'preferences.json').write_text('preserved')
            env={};report=session.prepare_graphics_catalog(resources,support,root/'Dump',env)
            self.assertEqual(report['programs'],1)
            manifest=json.loads((Path(env['GOW3_SHADER_CATALOG'])/'manifest.json').read_text())
            self.assertEqual(manifest['shader_recipes'],1);self.assertIn('Community/recording/',env['GOW3_COMMUNITY_RECORD_DIR'])
            self.assertEqual((support/'preferences.json').read_text(),'preserved')

    def test_missing_catalog_keeps_existing_launch_path(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); env = {}
            self.assertIsNone(session.prepare_graphics_catalog(root, root, root, env))
            self.assertEqual(env, {})


if __name__ == '__main__':
    unittest.main()
