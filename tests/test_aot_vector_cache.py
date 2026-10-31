"""Private AOT register caching: ABI observations, not game-derived fixtures."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'packaging/macos/launcher'))
from aot_vector_cache import transform

BODY = '''void f_10(BbCpu *c) {
    uint64_t r[16];
    memcpy(r, c->r, sizeof(r));
    rc_vset(&c->v[3], rc_vgpr(r[0]), 16, 0);
    rc->trace(0x10, r, c);
    if (r[1] & 1) {
        memcpy(c->r, r, sizeof(r));
        rc->call(c, 20, 30);
        memcpy(r, c->r, sizeof(r));
    }
    BbVec sum = c->v[3];
    sum.q[0] += c->v[5].q[0];
    rc_vset(&c->v[5], sum, 16, 1);
    memcpy(c->r, r, sizeof(r));
    if (r[1] & 2) rc->tail(c);
    else if (r[1] & 4) rc->bail(c, 40);
    return;
}
'''
# Independent synthetic state/observer. Every byte of the final state and the
# state observed inside callbacks must agree with the unmodified generated form.
PREFIX = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef union { uint64_t q[4]; unsigned char b[32]; } BbVec;
typedef struct { uint64_t r[16]; BbVec v[16]; } BbCpu;
static void rc_vset(BbVec *d,BbVec v,int bytes,int vex) {
 memcpy(d->b,v.b,bytes);if(vex && bytes<32)memset(d->b+bytes,0,32-bytes);
}
static BbVec rc_vgpr(uint64_t n) { BbVec v={0};v.q[0]=n;return v; }
static uint64_t trace_value;
static void callback(BbCpu *c,uint64_t target,uint64_t next) {
 c->v[5].q[0]=c->v[3].q[0]*17;
 c->v[15].q[3]^=c->v[3].q[0];c->r[0]+=target+next;
}
static void tail(BbCpu *c) { callback(c,1,2); }
static void bail(BbCpu *c,uint64_t rsp) { callback(c,rsp,2); }
static void trace(uint64_t off,const uint64_t r[16],const BbCpu *c) {
 trace_value^=off+r[0]+c->v[3].q[0]+c->v[3].q[3];
}
static const struct {
 void (*call)(BbCpu*,uint64_t,uint64_t);
 void (*tail)(BbCpu*);
 void (*bail)(BbCpu*,uint64_t);
 void (*trace)(uint64_t,const uint64_t*,const BbCpu*);
} api={callback,tail,bail,trace}, *rc=&api;
'''
MAIN = r'''
int main(void) {
 uint64_t seed=1234567;
 for(unsigned trial=0;trial<10000;++trial) {
  BbCpu a={0},b;
  for(unsigned j=0;j<16;++j) {
   a.r[j]=trial*(j+2);
   for(unsigned k=0;k<4;++k) {
    seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;a.v[j].q[k]=seed;
   }
  }
  memcpy(&b,&a,sizeof(a));trace_value=0;f_10(&a);
  uint64_t observed=trace_value;trace_value=0;cached_10(&b);
  assert(observed==trace_value);assert(memcmp(&a,&b,sizeof(a))==0);
 }
 puts("PASS: 10,000 vector state ABI cases");
}
'''


class VectorCacheTests(unittest.TestCase):
    def test_idempotence_and_no_scalar_only_change(self):
        cached, count = transform(BODY)
        self.assertEqual(count, 1)
        self.assertEqual(transform(cached), (cached, 0))
        scalar = 'void f_1(BbCpu *c) {\n    c->r[0] = 1;\n}\n'
        self.assertEqual(transform(scalar), (scalar, 0))

    def test_unknown_state_access_and_callback_are_rejected(self):
        for body in (BODY.replace('c->v[5]', 'c->v[index]'),
                     BODY.replace('rc->call(c, 20, 30)', 'unknown(c, 20, 30)')):
            with self.assertRaises(ValueError):
                transform(body)

    @unittest.skipUnless(shutil.which('clang'), 'native C compiler unavailable')
    def test_native_calls_trace_upper_lanes_and_untouched_slots(self):
        cached, _ = transform(BODY)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / 'abi.c'
            source.write_text(PREFIX + BODY + cached.replace('void f_10(', 'void cached_10(') + MAIN)
            binary = root / 'abi'
            subprocess.run(['clang', '-O2', '-std=c11', '-fno-strict-aliasing',
                            str(source), '-o', str(binary)], check=True, capture_output=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('10,000 vector state ABI cases', result.stdout)


if __name__ == '__main__':
    unittest.main()
