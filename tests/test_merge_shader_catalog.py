"""Recording-local permutation zero must not alias a different recording's zero."""
import ctypes
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from paths import ROOT
sys.path.insert(0, str(ROOT/'packaging/macos/native'))
from merge_shader_catalog import combine, merge


@unittest.skipUnless(os.environ.get('GOW3_TEST_XXHASH_LIBRARY'), 'Set native test xxhash library')
class MergeShaderCatalog(unittest.TestCase):
    def test_rewrites_colliding_recording_indices_and_pipeline_references(self):
        library = Path(os.environ['GOW3_TEST_XXHASH_LIBRARY'])
        native = ctypes.CDLL(str(library))
        checksum = native.XXH3_64bits
        checksum.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        checksum.restype = ctypes.c_uint64

        def seal(data):
            return data + struct.pack('<Q', checksum(data, len(data)))

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); sources = []
            for variant in (b'A', b'B'):
                source = root/variant.decode(); source.mkdir(); sources.append(source)
                (source/'compatibility.txt').write_text('0000000000000001\n')
                description = struct.pack('<IIIIQIIQ', 0x31525347, 1, 64, 48, 123, 0, 0, 0)
                description += struct.pack('<Q', 16) + bytes(64) + struct.pack('<Q', 0) + variant
                (source/'123_0.recipe').write_bytes(seal(description))
                (source/'binary-fingerprints.tsv').write_text('123_0.recipe\t0000000000000001\t'+'a'*64+'\n')
                pipeline = struct.pack('<III6Q', 0x31525047, 1, 48, combine(123, 0), 0, 0, 0, 0, 0)
                pipeline += struct.pack('<QQQ', 0, 0, 0) + variant
                (source/'recording.pso').write_bytes(seal(pipeline))
            result = merge(sources, root/'Merged', library)
            self.assertEqual(result['shader_recipes'], 2)
            self.assertEqual(result['pipeline_recipes'], 2)
            ids = {}
            for path in (root/'Merged').glob('*.recipe'):
                body = path.read_bytes()[:-8]
                ids[body[-1:]] = combine(123, struct.unpack_from('<Q', body, 32)[0])
            self.assertNotEqual(ids[b'A'], ids[b'B'])
            for path in (root/'Merged').glob('*.pso'):
                body = path.read_bytes()[:-8]
                self.assertEqual(struct.unpack_from('<Q', body, 12)[0], ids[body[-1:]])
            with self.assertRaisesRegex(ValueError, 'fresh output'):
                merge(sources, root/'Merged', library)
            # Identical specialization with different proven output must not be
            # silently deduplicated; that could hide a missing compilation input.
            (sources[1]/'123_0.recipe').write_bytes((sources[0]/'123_0.recipe').read_bytes())
            (sources[1]/'binary-fingerprints.tsv').write_text('123_0.recipe\t0000000000000002\t'+'a'*64+'\n')
            with self.assertRaisesRegex(ValueError, 'conflicting shader fingerprints'):
                merge(sources, root/'Rejected', library)
            self.assertFalse((root/'Rejected').exists())


if __name__ == '__main__':
    unittest.main()
