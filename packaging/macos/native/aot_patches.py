"""Bake opt-in upstream patches into both loader and scanner inputs before AOT.

Never patch only the live guest bytes: native generated functions must be rebuilt
from exactly the same instructions. The player's original dump is read-only.
"""
import hashlib
import json
import struct


def apply_file_writes(elf, boot, writes, read_boot):
    """Validate the complete transaction before returning either modified image.

    Only file-backed bytes without loader relocations are supported.
    Reject BSS/pointer patches instead of silently producing stale AOT code.
    """
    segments, image = read_boot(boot)
    start = len(boot) - len(image)
    size, _, ns, nr, ni, _ = struct.unpack_from('<6Q', boot, 8)
    pos = 96
    nm, = struct.unpack_from('<Q', boot, pos)
    pos += 8 + nm * 56
    nb, = struct.unpack_from('<Q', boot, pos)
    pos += 8 + nb * 24 + ns * 24 + ni * 128
    assert pos + nr * 32 == start and size == len(image)
    relocs = [struct.unpack_from('<Q', boot, pos+i*32)[0] for i in range(nr)]
    phoff, = struct.unpack_from('<Q', elf, 32)
    phsize, phnum = struct.unpack_from('<HH', elf, 54)
    loads = [struct.unpack_from('<II6Q', elf, phoff+i*phsize) for i in range(phnum)]
    planned = []
    for at, data in writes:
        end = at + len(data)
        match = [p for p in loads if p[0] == 1 and p[3] <= at and end <= p[3]+p[5]]
        if not data or len(match) != 1 or not any(a <= at and end <= a+n for a,n,f in segments):
            raise ValueError('AOT patch must be inside one file-backed segment')
        if any(at < r+8 and r < end for r in relocs):
            raise ValueError('AOT code patch overlaps a loader relocation')
        if any(at < b and a < end for a,b,_,_ in planned):
            raise ValueError('AOT code patches overlap')
        p = match[0]; file_at = p[2] + at - p[3]
        if elf[file_at:file_at+len(data)] != image[at:end]:
            raise ValueError('Linker changed patch site; refusing inconsistent AOT inputs')
        planned.append((at,end,file_at,data))
    patched_elf, patched_boot = bytearray(elf), bytearray(boot)
    for at,end,file_at,data in planned:
        patched_elf[file_at:file_at+len(data)] = data
        patched_boot[start+at:start+end] = data
    return bytes(patched_elf), bytes(patched_boot)


def bake(game, out, names, module, read_boot):
    from patches import compile_patches, eboot_image, eboot_segments, game_profile_for, patch_requirements
    profile = game_profile_for(game)
    if not profile:
        raise ValueError('No upstream patch profile for this game')
    _, xml, version, _ = profile
    original = module(game/'eboot.bin')['elf']
    writes = compile_patches(xml,names,version,eboot_segments(original),eboot_image(original))
    boot = (out/'boot-linked.bin').read_bytes()
    elf, boot = apply_file_writes(original,boot,writes,read_boot)
    (out/'boot-linked.bin').write_bytes(boot)
    dmem,vblank = patch_requirements(xml,names,version)
    (out/'aot-patches.json').write_text(json.dumps({
        'names':names,'writes':len(writes),'runtime_environment':{k:str(v) for k,v in
            [('GOW3_DMEM_MB',dmem),('GOW3_VBLANK_HZ',vblank)] if v is not None},'boot_sha256':hashlib.sha256(boot).hexdigest(),
        'scanner_elf_sha256':hashlib.sha256(elf).hexdigest(),
        'source':'Upstream patch definitions; see patches/God_of_War_III_Remastered.xml for authors',
    },indent=2)+'\n')
    return elf
