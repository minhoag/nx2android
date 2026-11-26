# Explicit offline compatibility input; addresses belong in the caller's manifest, not here.
import re
import struct
from nso_asm import B, STP_PRE, LDP_POST, LDRX, ADDI, BR, BLR


def load(path, name, build_id):
    with open(path) as source:
        rows = [line.split() for line in source if line.strip() and not line.lstrip().startswith('#')]
    builds, hooks, callbacks, sites = {}, {}, {}, []
    runtime = []
    for row in rows:
        kind = row[0]
        if kind == 'build':
            assert len(row) == 3 and row[1] not in builds and re.fullmatch('[0-9a-f]{32}|[0-9a-f]{40}|[0-9a-f]{64}', row[2]), 'invalid compatibility build'
            builds[row[1]] = row[2]
            runtime.append([kind, row[1], row[2][:32]])
        elif kind == 'hook':
            assert len(row) == 6 and row[4] in ('function', 'inline') and re.fullmatch('[0-9a-fA-F]{8}', row[5]), 'hook needs type and expected instruction bytes'
            ident = int(row[1], 0)
            assert 0 <= ident < 64 and ident not in hooks, 'duplicate or invalid compatibility hook'
            hooks[ident] = row
            if row[2] == name: sites.append((int(row[3], 0), ident, row[4], bytes.fromhex(row[5])))
            runtime.append(row[:5])
        elif kind == 'callback':
            assert len(row) == 4, 'invalid compatibility callback'
            ident = int(row[1], 0)
            callbacks.setdefault(ident, []).append(row)
            runtime.append(row)
        elif kind == 'operation':
            assert len(row) == 4 and 1 <= int(row[3], 0) <= 4, 'invalid compatibility operation'
            assert row[1] != name, 'operation regeneration is not supported; preserve auxiliary modules'
            runtime.append(row)
        elif kind == 'write':
            assert len(row) == 5 and int(row[3], 0) in (4, 8), 'invalid compatibility write'
            int(row[4], 0)
            runtime.append(row)
        elif kind == 'base':
            assert len(row) == 4, 'invalid compatibility base'
            runtime.append(row)
        else: raise AssertionError('unknown compatibility row')
    assert name in builds and len(builds[name]) >= 40 and builds[name] == build_id[:len(builds[name])], 'compatibility full build ID mismatch'
    assert set(hooks) == set(range(len(hooks))) and set(callbacks) == set(hooks), 'incomplete compatibility hooks'
    for ident, values in callbacks.items():
        assert 1 <= len(values) <= 8, 'too many compatibility callbacks'
    for row in rows:
        modules = [row[2]] if row[0] in ('hook', 'callback') else [row[1]] if row[0] in ('operation', 'write', 'base') else []
        if row[0] == 'base': modules.append(row[3])
        assert all(module in builds for module in modules), 'unverified compatibility module'
        if row[0] in ('hook', 'callback', 'operation', 'write', 'base'):
            assert int(row[3] if row[0] in ('hook', 'callback') else row[2], 0) >= 0, 'negative compatibility offset'
    assert all(row[2] == name for row in hooks.values()), 'one explicit rewrite must own all hook trampolines; preserve auxiliary modules'
    assert sites and len({s[0] for s in sites}) == len(sites), 'missing or duplicate compatibility sites'
    return sites, '\n'.join('\t'.join(row) for row in runtime) + '\n'


def validate(sites, image, text_end, dis, blocked, windows):
    for off, ident, mode, expected in sites:
        assert off % 4 == 0 and 0 <= off <= text_end - 4, 'compatibility hook outside text'
        assert off not in blocked and not any(lo <= off < hi for lo, hi in windows), f'compatibility hook conflicts with rewrite {off:#x}'
        assert image[off:off + 4] == expected, f'compatibility original instruction mismatch {off:#x}'
        ins = dis(struct.unpack('<I', expected)[0], off)
        # Only ordinary position-independent integer instructions. Reject branches, system,
        # literal loads, exclusives and platform-register accesses rather than relocate them.
        allowed = ins.mnemonic in ('add', 'adds', 'sub', 'subs', 'mov', 'movk', 'movn', 'movz', 'and', 'ands', 'orr', 'eor', 'tst', 'cmp', 'cmn', 'nop', 'stp', 'ldp', 'str', 'ldr', 'strb', 'ldrb', 'strh', 'ldrh')
        assert allowed and not re.search(r'\b[xw]18\b', ins.op_str) and not (ins.mnemonic.startswith('ldr') and '[' not in ins.op_str), f'unsupported compatibility displaced instruction {off:#x}'


def emit_hooks(sites, here, emit, emit_gate_call, put_branch, gates):
    trampolines = []
    for off, ident, mode, expected in sites:
        word = struct.unpack('<I', expected)[0]
        trampolines.append((ident, here()))
        emit(word); emit(B(here(), off + 4))
        start = here()
        emit(STP_PRE(29, 30, 31, -16)); emit_gate_call(); emit(BLR(30))
        ret = here()
        if mode == 'function':
            emit(LDP_POST(29, 30, 31, 16)); emit(BR(16))
        else:
            emit(LDRX(30, 31, 1)); emit(ADDI(31, 31, 16)); emit(word); emit(B(here(), off + 4))
            assert here() == ret + 16, 'inline callback resume ABI'
            emit(LDRX(30, 31, 1)); emit(ADDI(31, 31, 16)); emit(B(here(), off + 4))
        gates.append((ret, 'compathook', ident, off))
        put_branch(off, start)
    return trampolines
