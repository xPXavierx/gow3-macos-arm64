"""The index is a reference to validated source programs, never shader payloads."""
from pathlib import Path
import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from paths import ROOT
sys.path.insert(0, str(ROOT / 'packaging/macos/native'))
import shader_catalog


def program(value=12, shader_hash=0x123456789abcdef0):
    data = bytearray(60)
    struct.pack_into('<III', data, 0, 0xbeeb03ff, 3, value)
    data[32:40] = b'OrbShdr\x07'
    struct.pack_into('<I', data, 40, (12 << 8) | 0x47)
    struct.pack_into('<Q', data, 48, shader_hash)
    return data


class ShaderAssetCatalog(unittest.TestCase):
    def test_unaligned_program_and_usage_gap_are_supported(self):
        rows = list(shader_catalog.programs(b'abc'+program()+b'xyz'))
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][:4], (0x123456789abcdef0, 3, 12, 1))

    def test_rejects_truncated_markers_bad_length_and_bad_header(self):
        self.assertEqual(list(shader_catalog.programs(b'OrbShdr\x07')), [])
        for offset, value in [(4, 0xffffffff), (40, (65540 << 8) | 0x47), (40, (36 << 8) | 0x47)]:
            data = program()
            struct.pack_into('<I', data, offset, value)
            self.assertEqual(list(shader_catalog.programs(data)), [])

    def test_reuse_verifies_index_and_changed_dump_rebuilds(self):
        with tempfile.TemporaryDirectory() as temp, contextlib.redirect_stdout(io.StringIO()):
            root = Path(temp); game = root/'Dump'; wad = game/'EXEC/WAD/PS4_LE/A.WAD'
            wad.parent.mkdir(parents=True); wad.write_bytes(program())
            output = root/'Cache'
            first = shader_catalog.build(game, output)
            self.assertFalse(first['reused'])
            self.assertEqual(first['programs'], 1)
            self.assertTrue(shader_catalog.build(game, output)['reused'])
            self.assertNotIn('beeb03ff', (output/'programs.tsv').read_text())
            (output/'programs.tsv').write_text('damaged')
            self.assertFalse(shader_catalog.build(game, output)['reused'])
            wad.write_bytes(program()+program(shader_hash=0x123456789abcdeff))
            rebuilt = shader_catalog.build(game, output)
            self.assertEqual(rebuilt['programs'], 2)
            self.assertEqual(rebuilt['distinct_code'], 1)
            self.assertEqual(rebuilt['equivalent_identifiers'], 1)

    def test_conflicting_identifiers_do_not_publish_an_index(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); wad = root/'Dump/EXEC/WAD/PS4_LE/A.WAD'
            wad.parent.mkdir(parents=True); wad.write_bytes(program()+program(value=13))
            with self.assertRaisesRegex(ValueError, 'Conflicting'):
                shader_catalog.build(root/'Dump', root/'Cache')
            self.assertFalse((root/'Cache/programs.tsv').exists())


if __name__ == '__main__':
    unittest.main()
