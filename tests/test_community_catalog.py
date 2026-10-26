"""Canonical community identities and bounded reconstruction descriptions."""
import copy,hashlib,json,struct,sys,tempfile,unittest
from pathlib import Path
from paths import ROOT
sys.path.insert(0,str(ROOT/'packaging/macos/native'))
import community_catalog as c

def shader(program='0000000000001234'):
 r=dict(kind='shader',abi='1234567890abcdef',key_size=64,program=program,source_sha256='a'*64,
        hardware_stage=1,software_stage=3,runtime_size=16,flat=[0]*16,layout_size=0,
        layout_fingerprint=f'{c.fnv(b""):016x}',specialization=bytes(128).hex(),reference_spirv='1'*16)
 r['id']=c.identity(r);return r

def pipeline(s):
 r=dict(kind='pipeline',abi=s['abi'],key_size=64,stages={'vertex':s['id']},
        state=(bytes(64)+struct.pack('<QQQIII f II',0,0,0,0,1,0,0.,0,0)).hex())
 r['id']=c.identity(r);return r

class CommunityCatalog(unittest.TestCase):
 def test_canonical_roundtrip_dedup_and_local_variant_mapping(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=Path(tmp);s=shader();p=pipeline(s);a=d/'a';b=d/'b';c.save(a,{s['id']:s,p['id']:p})
   alt=copy.deepcopy(s);alt['reference_spirv']='f'*16
   self.assertEqual(c.identity(alt),c.identity(s));c.save(b,{alt['id']:alt,p['id']:p})
   out=d/'out';r=c.materialize([a,b],out)
   self.assertEqual((r['shader_recipes'],r['pipeline_recipes']),(1,1))
   self.assertFalse(r['contains_shader_code']);self.assertFalse(r['contains_walker_code'])
   raw=next(out.glob('*.recipe')).read_bytes();self.assertEqual(raw[:4],b'GSR2')
   self.assertEqual(hashlib.sha256(raw[:-32]).digest(),raw[-32:])
   self.assertEqual(struct.unpack_from('<QQ',raw,112),(0,c.fnv(b'')))
   c.collect(out,d/'roundtrip');self.assertEqual(set(c.load(d/'roundtrip')),{s['id'],p['id']})
   self.assertEqual(c.materialize([a,b],out),r)
 def test_unknown_fields_tampering_and_numeric_types_rejected(self):
  s=shader();s['walker_code']='deadbeef'
  with self.assertRaisesRegex(ValueError,'Unknown'):c.validate(s)
  s=shader();s['program']='0000000000009999'
  with self.assertRaisesRegex(ValueError,'identity'):c.validate(s)
  s=shader();s['flat']=[False]*16;s['id']=c.identity(s)
  with self.assertRaises(ValueError):c.validate(s)
 def test_version_symlinks_legacy_and_corrupt_proofs_rejected(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=Path(tmp);s=shader();p=d/'a';c.save(p,{s['id']:s});alias=d/'alias';alias.symlink_to(p)
   with self.assertRaises(ValueError):c.load(alias)
   p.write_bytes(c.canonical(dict(c.GAME,tested_version='1.00'))+b'\n')
   with self.assertRaisesRegex(ValueError,'version'):c.load(p)
   raw=d/'bad.recipe';raw.write_bytes(b'GSR1'+bytes(80))
   with self.assertRaisesRegex(ValueError,'GSR2'):c.checked_native(raw)
   raw.write_bytes(b'GSR2'+bytes(80))
   with self.assertRaisesRegex(ValueError,'SHA256'):c.checked_native(raw)
 def test_incomplete_dependencies_and_abi_mismatch(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=Path(tmp);s=shader();p=pipeline(s);p['stages']['fragment']='f'*64;p['id']=c.identity(p)
   c.save(d/'a',{s['id']:s,p['id']:p});self.assertEqual(c.materialize([d/'a'],d/'out')['pipeline_recipes'],0)
   s2=shader('0000000000005678');s2['abi']='0'*16;s2['id']=c.identity(s2);c.save(d/'b',{s2['id']:s2})
   with self.assertRaisesRegex(ValueError,'ABIs'):c.materialize([d/'a',d/'b'],d/'out')
 def test_explicit_import_export_and_renderer_compatibility(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=Path(tmp);s=shader();c.save(d/'seed.jsonl',{s['id']:s});
   report=c.import_catalogue(d/'seed.jsonl',d/'Support/Community/imported',d/'seed.jsonl')
   self.assertEqual(report['shaders'],1);self.assertEqual(len(list((d/'Support/Community/imported').glob('*.jsonl'))),1)
   c.save(d/'Support/Community/contribution.jsonl',{s['id']:s})
   c.export_catalogue(d/'Support',d/'shared.jsonl',d/'seed.jsonl');self.assertEqual(c.load(d/'shared.jsonl'),c.load(d/'seed.jsonl'))
   other=shader();other['abi']='0'*16;other['id']=c.identity(other);c.save(d/'other.jsonl',{other['id']:other})
   with self.assertRaisesRegex(ValueError,'ABI'):c.import_catalogue(d/'other.jsonl',d/'Support/Community/imported',d/'seed.jsonl')
 def test_local_permutation_numbers_are_scoped_per_session(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=Path(tmp);first=shader();second=shader();second['flat'][0]=1;second['id']=c.identity(second)
   c.save(d/'seed',{first['id']:first})
   for name,row in [('A',first),('B',second)]:
    pso=pipeline(row);source=d/name;c.save(source,{row['id']:row,pso['id']:pso})
    c.materialize([source],d/'Support/Community/recording'/name)
   report=c.export_catalogue(d/'Support',d/'export',d/'seed')
   self.assertEqual((report['shaders'],report['pipelines']),(2,2))
   rows=c.load(d/'export');self.assertEqual({p['stages']['vertex'] for p in rows.values() if p['kind']=='pipeline'},{first['id'],second['id']})
 def test_push_data_migration_preserves_descriptions_and_remaps_pipeline_stages(self):
  s=shader();s['abi']='f9bf6c9321b85a41';s['id']=c.identity(s);p=pipeline(s)
  original=copy.deepcopy(s)
  rows=c.migrate_descriptions({s['id']:s,p['id']:p},'f9baa7c07fa3310e',{})
  new_shader=next(r for r in rows.values() if r['kind']=='shader')
  new_pipeline=next(r for r in rows.values() if r['kind']=='pipeline')
  self.assertEqual(s,original)
  self.assertEqual(new_shader['reference_spirv'],'0'*16)
  self.assertEqual(new_shader['flat'],s['flat'])
  self.assertEqual(new_pipeline['stages'],{'vertex':new_shader['id']})
  for row in rows.values():c.validate(row)
  with self.assertRaisesRegex(ValueError,'No compatible'):
   c.migrate_descriptions(rows,'1234567890abcdef',{})
 def test_migration_resolves_seed_dependencies_without_relabelling_foreign_abis(self):
  legacy=shader();legacy['abi']='f9bf6c9321b85a41';legacy['id']=c.identity(legacy)
  current=dict(legacy,abi='f9baa7c07fa3310e');current['id']=c.identity(current)
  p=pipeline(legacy)
  rows=c.migrate_descriptions({p['id']:p},current['abi'],{current['id']:current})
  self.assertEqual(next(iter(rows.values()))['stages']['vertex'],current['id'])
 def test_vertex_container_bounds(self):
  p=pipeline(shader());raw=bytearray.fromhex(p['state']);struct.pack_into('<Q',raw,64,1<<32)
  p['state']=raw.hex();p['id']=c.identity(p)
  with self.assertRaisesRegex(ValueError,'Excessive'):c.validate(p)
 def test_early_depth_migration_prepares_both_states_and_keeps_original(self):
  s=shader();s.update(abi='f9baa7c07fa3310e',key_size=352,runtime_size=288,
      hardware_stage=0,software_stage=0,specialization=bytes(484).hex())
  s['id']=c.identity(s);original=copy.deepcopy(s)
  p=dict(kind='pipeline',abi=s['abi'],key_size=352,stages={'fragment':s['id']},
      state=(bytes(352)+struct.pack('<QQQIII f II',0,0,0,0,1,0,0.,0,0)).hex())
  p['id']=c.identity(p)
  rows=c.migrate_descriptions({s['id']:s,p['id']:p},c.EARLY_DEPTH_ABI,{})
  shaders=[r for r in rows.values() if r['kind']=='shader'];pipelines=[r for r in rows.values() if r['kind']=='pipeline']
  self.assertEqual(s,original);self.assertEqual((len(shaders),len(pipelines)),(2,2))
  self.assertEqual({bytes.fromhex(r['specialization'])[298] for r in shaders},{0,1})
  self.assertEqual({r['reference_spirv'] for r in shaders},{'0'*16})
  self.assertEqual({r['stages']['fragment'] for r in pipelines},{r['id'] for r in shaders})
  for r in shaders:
   raw=bytearray.fromhex(r['specialization']);raw[298]=0
   self.assertEqual(raw,bytes.fromhex(original['specialization']))
  only=c.migrate_descriptions({p['id']:p},c.EARLY_DEPTH_ABI,{r['id']:r for r in shaders})
  self.assertEqual(len(only),2) # both dependencies resolved against the new seed
  with self.assertRaisesRegex(ValueError,'layout'):
   bad=dict(s,runtime_size=280);bad['id']=c.identity(bad)
   c.migrate_descriptions({bad['id']:bad},c.EARLY_DEPTH_ABI,{})
  with self.assertRaisesRegex(ValueError,'layout'):
   bad=dict(s,specialization=bytes(288).hex());bad['id']=c.identity(bad)
   c.migrate_descriptions({bad['id']:bad},c.EARLY_DEPTH_ABI,{})
  with self.assertRaisesRegex(ValueError,'padding'):
   raw=bytearray.fromhex(s['specialization']);raw[298]=1
   bad=dict(s,specialization=raw.hex());bad['id']=c.identity(bad)
   c.migrate_descriptions({bad['id']:bad},c.EARLY_DEPTH_ABI,{})

if __name__=='__main__':unittest.main()
