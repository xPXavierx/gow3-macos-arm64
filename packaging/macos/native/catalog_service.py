#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Official signed catalogue updates and consent-based GitHub contributions.

Never executes downloaded content. HTTPS and Ed25519 verification are handled by
the bundled native helper, using macOS trust roots. Only that helper is executed.
"""
import argparse
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
sys.path.insert(0,str(Path(__file__).resolve().parent))
import community_catalog as c

ORIGIN = 'https://raw.githubusercontent.com/xPXavierx/gow3-macos-arm64/community-catalog/v1'
ISSUE = 'https://github.com/xPXavierx/gow3-macos-arm64/issues/new?template=shader_contribution.yml'
OFFICIAL_LIMIT = 16*1024*1024
CONTRIBUTION_LIMIT = 8*1024*1024


def abi_for(resources):
    values = {r['abi'] for r in c.load(Path(resources)/'CommunityCatalog.jsonl').values()}
    if len(values) != 1:
        raise ValueError('Application catalogue ABI')
    return next(iter(values))


def bounded(path, limit):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > limit:
        raise ValueError('Catalogue file bounds')
    with path.open('rb') as stream:
        data = stream.read(limit+1)
    if len(data) > limit:
        raise ValueError('Catalogue file bounds')
    return data


def helper(resources, *args):
    executable = Path(resources)/'CatalogTrust'
    result = subprocess.run([str(executable), *map(str,args)], capture_output=True,
                            text=True, timeout=30, check=False)
    if result.returncode:
        raise ValueError('Official catalogue verification/download failed')
    return c.strict_json(result.stdout) if result.stdout.strip() else None


@contextlib.contextmanager
def locked(support):
    support = Path(support); support.mkdir(parents=True, exist_ok=True)
    path = support/'session.lock'
    if path.is_symlink():
        raise ValueError('Unsafe session lock')
    with path.open('a+') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError('Close the game before updating or contributing') from None
        yield


def official_root(support, abi):
    path = Path(support)/'Community/official'/abi
    # Remote data may select only a SHA-256 child, never a path/URL.
    if any(p.is_symlink() for p in [path, path.parent, path.parent.parent]):
        raise ValueError('Redirected official catalogue directory')
    return path


def verify_directory(resources, directory, abi):
    for name, limit in [('manifest.json',8192),('manifest.sig',64),('catalogue.jsonl',OFFICIAL_LIMIT)]:
        bounded(directory/name, limit)
    info = helper(resources, 'verify', Path(resources)/'CommunityCatalogue.pub',
                  directory/'manifest.json', directory/'manifest.sig', directory/'catalogue.jsonl', abi)
    rows = c.load(directory/'catalogue.jsonl')
    if not rows or {r['abi'] for r in rows.values()} != {abi}:
        raise ValueError('Signed catalogue renderer mismatch')
    return info, rows


def verified_official(resources, support):
    """Reverify on each launch; a cached file's name is not proof of trust."""
    abi = abi_for(resources); root = official_root(support, abi)
    pointer = root/'current.json'
    if not pointer.exists():
        return None
    current = c.strict_json(bounded(pointer, 4096))
    if not isinstance(current,dict) or set(current) != {'generation'} or not isinstance(current['generation'],str) or not re.fullmatch('[0-9a-f]{64}',current['generation']):
        raise ValueError('Invalid catalogue generation')
    directory = root/current['generation']
    if directory.is_symlink():
        raise ValueError('Redirected catalogue generation')
    info, _ = verify_directory(resources, directory, abi)
    if hashlib.sha256(bounded(directory/'manifest.json',8192)).hexdigest() != current['generation']:
        raise ValueError('Catalogue generation mismatch')
    return directory/'catalogue.jsonl'


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name+'.tmp')
    temp.write_bytes(c.canonical(data)+b'\n'); os.replace(temp,path)


def update(resources, support):
    resources, support = Path(resources), Path(support)
    abi = abi_for(resources); root = official_root(support,abi)
    with locked(support):
        root.mkdir(parents=True, exist_ok=True)
        state = {'revision':0,'manifest_sha256':''}
        if (root/'trust-state.json').exists():
            state = c.strict_json(bounded(root/'trust-state.json',4096))
            if not isinstance(state,dict) or set(state) != {'revision','manifest_sha256'} or type(state['revision']) is not int or not 0 <= state['revision'] < 1_000_000_000:
                raise ValueError('Invalid catalogue trust state')
        with tempfile.TemporaryDirectory(prefix='.download-',dir=root) as temporary:
            stage = Path(temporary)
            for name, remote, limit in [('manifest.json','latest.json',8192),('manifest.sig','latest.sig',64)]:
                helper(resources,'download',f'{ORIGIN}/{abi}/{remote}',limit,stage/name)
            # Signature, game, ABI, revision and expiry are checked before the body download.
            info = helper(resources,'manifest',resources/'CommunityCatalogue.pub',
                          stage/'manifest.json',stage/'manifest.sig',abi)
            digest = hashlib.sha256(bounded(stage/'manifest.json',8192)).hexdigest()
            if info['revision'] < state['revision'] or (info['revision'] == state['revision'] and digest != state['manifest_sha256']):
                raise ValueError('Catalogue rollback/revision conflict rejected')
            helper(resources,'download',f'{ORIGIN}/{abi}/{info["sha256"]}.jsonl',info['bytes'],stage/'catalogue.jsonl')
            _, rows = verify_directory(resources,stage,abi)
            target = root/digest
            if target.exists():
                verify_directory(resources,target,abi)
            else:
                generation = stage/'verified'; generation.mkdir()
                for name in ['manifest.json','manifest.sig','catalogue.jsonl']:
                    os.replace(stage/name,generation/name)
                os.replace(generation,target)
            # Commit verified generation atomically. Bad downloads never replace a good one.
            write_json(root/'trust-state.json',{'revision':info['revision'],'manifest_sha256':digest})
            write_json(root/'current.json',{'generation':digest})
            write_json(root/'last-check.json',{'checked_at':int(time.time())})
        return dict(updated=digest != state['manifest_sha256'],revision=info['revision'],
                    shaders=sum(r['kind']=='shader' for r in rows.values()),
                    pipelines=sum(r['kind']=='pipeline' for r in rows.values()),verified=True)


def prepare_contribution(resources,support):
    resources, support = Path(resources), Path(support)
    with locked(support):
        known = c.load(resources/'CommunityCatalog.jsonl')
        try:
            official = verified_official(resources,support)
            if official:
                known.update(c.load(official))
        except (ValueError,OSError,subprocess.SubprocessError):
            pass # A failed optional download must not prevent a local contribution.
        output = support/'Community/outgoing'; output.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.prepare-',dir=output) as temp:
            exported = Path(temp)/'discovered.jsonl'
            try:
                c.export_catalogue(support,exported,resources/'CommunityCatalog.jsonl')
            except ValueError as error:
                if str(error).startswith('No local discoveries'):
                    return dict(empty=True)
                raise
            rows = c.load(exported)
            delta = {key:row for key,row in rows.items() if key not in known}
            if not delta:
                return dict(empty=True)
            new_count = len(delta)
            # Include dependencies so the reviewer need not guess the contributor's seed.
            for row in list(delta.values()):
                if row['kind'] == 'pipeline':
                    for stage in row['stages'].values():
                        if stage not in delta:
                            dependency = rows.get(stage,known.get(stage))
                            if dependency is None:
                                raise ValueError('Incomplete contribution dependencies')
                            delta[stage] = dependency
            prepared = Path(temp)/'contribution.txt'; report = c.save(prepared,delta)
            if prepared.stat().st_size > CONTRIBUTION_LIMIT:
                raise ValueError('Contribution exceeds 8 MiB; contact the maintainer')
            target = output/f'GOW3-{report["sha256"][:20]}.txt'
            os.replace(prepared,target); target.chmod(0o600)
            return dict(empty=False,file=str(target),issue_url=ISSUE,new_records=new_count,**report)


def refresh(resources,support):
    """Optional daily startup check; offline/invalid updates keep the last good data."""
    root=official_root(support,abi_for(resources)); root.mkdir(parents=True,exist_ok=True)
    attempt=root/'last-attempt.json'
    if attempt.exists():
        try:
            last=c.strict_json(bounded(attempt,4096))['checked_at']
            if type(last) is int and 0 <= time.time()-last < 86400:
                return dict(cached=True)
        except (ValueError,KeyError,TypeError):
            pass
    write_json(attempt,{'checked_at':int(time.time())})
    try:
        return update(resources,support)
    except (ValueError,OSError,subprocess.SubprocessError):
        return dict(offline_or_unavailable=True,bundled_catalogue_preserved=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['update','refresh','contribute','pending','verify'])
    parser.add_argument('--resources',required=True,type=Path)
    parser.add_argument('--support',required=True,type=Path)
    args=parser.parse_args()
    if args.action == 'update': result=update(args.resources,args.support)
    elif args.action == 'refresh': result=refresh(args.resources,args.support)
    elif args.action in ('contribute','pending'): result=prepare_contribution(args.resources,args.support)
    else:
        path=verified_official(args.resources,args.support)
        result={'catalogue':str(path) if path else None}
    print(json.dumps(result))

if __name__ == '__main__':
    main()
