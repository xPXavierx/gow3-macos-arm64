#!/usr/bin/env python3
"""Synthetic transaction checks; no game-derived bytes."""
import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'scripts'))
from isa_rewrite import read_boot_linked
from aot_patches import apply_file_writes

class CodePatchTests(unittest.TestCase):
    def fixture(self,relocs=()):
        image=bytes(range(64))
        elf=bytearray(128)+bytearray(image)
        struct.pack_into('<Q',elf,32,64)
        struct.pack_into('<HH',elf,54,56,1)
        struct.pack_into('<II6Q',elf,64,1,5,128,0,0,64,80,16)
        boot=struct.pack('<8s6Q',b'G3PROBE5',64,0,1,len(relocs),0,0)+bytes(40)
        boot+=struct.pack('<QQ3Q',0,0,0,64,5)
        boot+=b''.join(struct.pack('<QQqq',r,0,0,0) for r in relocs)+image
        return bytes(elf),boot
    def test_matching_scanner_loader(self):
        elf,boot=self.fixture();e,b=apply_file_writes(elf,boot,[(4,b'ABC'),(12,b'Z')],read_boot_linked)
        self.assertEqual(e[128:],read_boot_linked(b)[1]);self.assertEqual(e[132:135],b'ABC')
        self.assertEqual(elf[132:135],bytes([4,5,6]))
    def test_relocation(self):
        e,b=self.fixture([8])
        with self.assertRaisesRegex(ValueError,'relocation'):apply_file_writes(e,b,[(14,b'xx')],read_boot_linked)
    def test_overlap(self):
        e,b=self.fixture()
        with self.assertRaisesRegex(ValueError,'overlap'):apply_file_writes(e,b,[(1,b'abc'),(3,b'x')],read_boot_linked)
    def test_bss(self):
        e,b=self.fixture()
        with self.assertRaises(ValueError):apply_file_writes(e,b,[(64,b'x')],read_boot_linked)
    def test_linker_change(self):
        e,b=self.fixture();b=b[:-1]+b'X'
        with self.assertRaisesRegex(ValueError,'Linker changed'):apply_file_writes(e,b,[(63,b'A')],read_boot_linked)
    def test_data_segment(self):
        e,b=self.fixture();e=bytearray(e);struct.pack_into('<I',e,68,4)
        modified,_=apply_file_writes(e,b,[(4,b'A')],read_boot_linked)
        self.assertEqual(modified[132:133],b'A')
    def test_empty_write(self):
        e,b=self.fixture()
        with self.assertRaises(ValueError):apply_file_writes(e,b,[(4,b'')],read_boot_linked)

if __name__=='__main__':unittest.main(verbosity=2)
