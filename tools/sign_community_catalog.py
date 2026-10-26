#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Maintainer-only: package a reviewed catalogue and sign using macOS Keychain.

No uploads, key export, shell interpolation or automatic approval of contributions.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'packaging/macos/native'))
import community_catalog as c

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalogue',required=True,type=Path)
    p.add_argument('--revision',required=True,type=int)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--signer',required=True,type=Path)
    a=p.parse_args();rows=c.load(a.catalogue)
    if not rows or {r['abi'] for r in rows.values()}!={c.EARLY_DEPTH_ABI} or not 0<a.revision<1_000_000_000:
        raise ValueError('Reviewed catalogue identity/revision')
    data=c.canonical(c.GAME)+b'\n'+b''.join(c.canonical(r)+b'\n' for _,r in sorted(rows.items()))
    if len(data)>16*1024*1024:raise ValueError('Official catalogue limit')
    now=int(time.time());digest=hashlib.sha256(data).hexdigest()
    target=a.output/'v1'/c.EARLY_DEPTH_ABI;target.mkdir(parents=True,exist_ok=True)
    (target/(digest+'.jsonl')).write_bytes(data)
    manifest=dict(format=1,title_id=c.GAME['title_id'],tested_version=c.GAME['tested_version'],
        source_eboot_sha256=c.GAME['source_eboot_sha256'],abi=c.EARLY_DEPTH_ABI,
        revision=a.revision,sha256=digest,bytes=len(data),issued_at=now,expires_at=now+90*86400)
    (target/'latest.json').write_bytes(c.canonical(manifest)+b'\n')
    subprocess.run([str(a.signer.resolve()),'sign',str(target/'latest.json'),str(target/'latest.sig')],check=True)
    print(json.dumps(manifest,indent=2))

if __name__=='__main__':main()
