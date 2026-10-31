# SPDX-License-Identifier: GPL-2.0-or-later
"""Promote the pinned AOT emitter's private vector state to C function locals.

Guest addresses never expose BbCpu: each host thread owns its CPU state. A
register operation can therefore avoid round trips through that state. Preserve
all guest memory operations and fences. Synchronize touched registers at the
emitter's existing GPR ABI boundaries, including calls, returns and fallbacks.
"""
import re

_SPILL = 'memcpy(c->r, r, sizeof(r));'
_RELOAD = 'memcpy(r, c->r, sizeof(r));'
_MARKER = 'private vector register cache'
_FUNCTION = re.compile(
    r'(^[^\n]*void [fs]_[0-9a-f]+\(BbCpu \*c\) \{\n)(.*?)(^}\n)', re.M | re.S)


def transform(code):
    """Return (source, promoted function count); reject unknown state accesses.

    This is a pass over the bundled emitter's fixed output grammar, not a parser
    for arbitrary C. Existing promoted functions are left intact on recompilation.
    Untouched vector slots stay in BbCpu, so a callee's changes to them survive.
    """
    count = 0

    def promote(match):
        nonlocal count
        body = match[2]
        if _MARKER in body:
            return match[0]
        registers = sorted({int(i) for i in re.findall(r'c->v\[(\d+)\]', body)})
        if not registers:
            return match[0]
        if any(i >= 16 for i in registers):
            raise ValueError('Invalid vector register in AOT output')
        if 'c->v' in re.sub(r'c->v\[\d+\]', '', body):
            raise ValueError('Unknown vector state access in AOT output')
        callbacks = set(re.findall(r'\b([A-Za-z_]\w*(?:->\w+)?)\(c[,)]', body))
        for name in callbacks:
            if not re.fullmatch(r'd_[0-9a-f]+', name) and name not in {
                    'rc->call', 'rc->tail', 'rc->bail', 'rc->step'}:
                raise ValueError('Unknown CPU-state callback in AOT output: ' + name)
        if 'uint64_t r[16];' not in body or _RELOAD not in body or _SPILL not in body:
            raise ValueError('Unknown GPR ABI boundary in AOT output')
        store = ' '.join(f'c->v[{i}] = vr[{i}];' for i in registers)
        load = ' '.join(f'vr[{i}] = c->v[{i}];' for i in registers)
        body = re.sub(r'c->v\[(\d+)\]', r'vr[\1]', body)
        body = body.replace('uint64_t r[16];',
                            f'uint64_t r[16];\n    BbVec vr[16]; /* {_MARKER} */')
        body = body.replace(_SPILL, _SPILL + ' ' + store)
        body = body.replace(_RELOAD, _RELOAD + ' ' + load)
        # Trace already receives the current GPR locals separately. Its const
        # CPU argument must also observe current vectors at each instruction.
        body = re.sub(r'(rc->trace\([^;]+;)', lambda trace: store + ' ' + trace[0], body)
        count += 1
        return match[1] + body + match[3]

    return _FUNCTION.sub(promote, code), count
