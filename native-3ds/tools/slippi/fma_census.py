"""Fused multiply-add census: retail GameCube code versus this engine build.

Per decomp function, counts the console's fused operations in the retail
DOL (assets/GALE01/sys/main.dol, function bounds from the decomp's
config/GALE01/symbols.txt):
  single  fmadds/fmsubs/fnmadds/fnmsubs (opcode 59, XO 29/28/31/30) and the
          paired-single fused ops (opcode 4: ps_madd/msub/nmadd/nmsub,
          ps_madds0/1)
  double  fmadd/fmsub/fnmadd/fnmsub (opcode 63), split into the inline
          sqrtf refinement (within 24 instructions after an frsqrte) and
          the rest
versus clang's fusions in the engine build: llvm.fmuladd calls in each
function's front-end IR (build/<engine dir>/<source>.ll, written by
tools/engine_build.py), minus those tools/slippi/no_contract.txt unfuses,
plus explicit mp_fmadds/mp_fnmsubs calls, plus the counts of callees the
console compiler inlined (static helpers the retail program does not
contain; retail functions it has fewer bl to than we have calls), per call.
Our sqrtf calls count as the console's inline refinement (3 double fnmsub,
4 for sqrtf_accurate).

    python tools/slippi/fma_census.py [--engine-dir build/engine-opt]
        [--top 60] [--all] [--function NAME] [--write-no-contract]

--write-no-contract adds every function where the console fused nothing but
clang fuses in the function's own body (and static helpers reached only from
such functions, qualified 'source::name') to tools/slippi/no_contract.txt.
Re-build and re-run the census afterwards.
"""
import argparse
import collections
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import fp_contract  # noqa: E402

SYMBOLS = ROOT / 'upstream/melee/config/GALE01/symbols.txt'
DOL = ROOT / 'assets/GALE01/sys/main.dol'
RELEVANCE = ('melee_ft_', 'melee_lb_', 'melee_mp_', 'melee_it_', 'melee_gr_', 'melee_cm_',
             'sysdolphin_baselib_', 'MSL_', 'dolphin_math')
# tools/slippi_edits/determinism.py spells some fused operations out
# (port/include/mp_fp.h); each call is one fused single-precision op.
EXPLICIT = ('mp_fmadds', 'mp_fmsubs', 'mp_fnmsubs')
DEFINE = re.compile(r'^define\b(.*?)@("?)([^"(\s]+)\2\(')
CALLEE = re.compile(r'\bcall\b[^@]*@("?)([^"(\s]+)\1\(')


def dol_sections(data):
    offs = [int.from_bytes(data[i:i + 4], 'big') for i in range(0, 0x48, 4)]
    addrs = [int.from_bytes(data[0x48 + i:0x48 + i + 4], 'big') for i in range(0, 0x48, 4)]
    sizes = [int.from_bytes(data[0x90 + i:0x90 + i + 4], 'big') for i in range(0, 0x48, 4)]
    return [(a, o, s) for a, o, s in zip(addrs, offs, sizes) if s]


def read_code(data, sections, addr, size):
    for a, o, s in sections:
        if a <= addr and addr + size <= a + s:
            return data[o + addr - a:o + addr - a + size]
    return None


def retail_counts():
    """name -> dict(single, double, sqrt_double, frsqrte, addr, calls)"""
    data = DOL.read_bytes()
    sections = dol_sections(data)
    out = {}
    for line in SYMBOLS.read_text(errors='replace').splitlines():
        m = re.match(r'(\S+) = \.text:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)', line)
        if not m:
            continue
        name, addr, size = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
        code = read_code(data, sections, addr, size)
        if code is None:
            continue
        c = dict(single=0, double=0, sqrt_double=0, frsqrte=0, addr=addr, calls=collections.Counter())
        last_rsqrte = -100
        for i in range(0, len(code) - 3, 4):
            w = int.from_bytes(code[i:i + 4], 'big')
            op, xo = w >> 26, (w >> 1) & 0x1f
            if op == 18 and w & 1:  # bl
                disp = w & 0x03fffffc
                if disp & 0x02000000:
                    disp -= 0x04000000
                c['calls'][(0 if w & 2 else addr + i) + disp] += 1
            if op == 59 and xo in (28, 29, 30, 31):
                c['single'] += 1
            elif op == 4 and xo in (14, 15, 28, 29, 30, 31):
                c['single'] += 1
            elif op == 63 and xo in (28, 29, 30, 31):
                if i // 4 - last_rsqrte <= 24:
                    c['sqrt_double'] += 1
                else:
                    c['double'] += 1
            elif op == 63 and xo == 26:
                c['frsqrte'] += 1
                last_rsqrte = i // 4
        out.setdefault(name, c)
    return out


def native_counts(engine_dir, no_contract):
    """key -> dict(name, own32, own64, sqrt_calls, calls: Counter of keys,
    internal, source). Internal (static) functions are keyed 'source::name',
    the others by name."""
    funcs = {}
    for ll in sorted(Path(engine_dir).glob('*.ll')):
        if ll.name.endswith('.gc.ll'):
            continue
        source = ll.name[:-3]
        text = ll.read_text(encoding='utf-8', errors='replace')
        local = {m.group(3) for m in re.finditer(r'(?m)^define\b(.*?)@("?)([^"(\s]+)\2\(', text)
                 if 'internal' in m.group(1)}
        key, kinds = None, set()
        for line in text.splitlines():
            m = DEFINE.match(line)
            if m:
                name = m.group(3)
                internal = 'internal' in m.group(1)
                key = source + '::' + name if internal else name
                if key in funcs:  # a global defined twice (should not happen)
                    key = None
                    continue
                funcs[key] = dict(name=name, own32=0, own64=0, sqrt_calls=0, calls=collections.Counter(),
                                  internal=internal, source=source)
                kinds = fp_contract.kinds_for(no_contract, source, name)
                continue
            if key is None:
                continue
            if line.startswith('}'):
                key = None
                continue
            info = funcs[key]
            if '@llvm.fmuladd.f32' in line and 'call' in line:
                if 'f32' not in kinds:
                    info['own32'] += 1
                continue
            if '@llvm.fmuladd.f64' in line and 'call' in line:
                if 'f64' not in kinds:
                    info['own64'] += 1
                continue
            c = CALLEE.search(line)
            if c:
                callee = c.group(2)
                if callee == 'mp_be_sqrtf':
                    info['sqrt_calls'] += 3
                elif callee == 'mp_sqrtf_accurate':
                    info['sqrt_calls'] += 4
                elif callee in EXPLICIT:
                    info['own32'] += 1  # an explicit fused operation
                elif not callee.startswith('llvm.'):
                    info['calls'][source + '::' + callee if callee in local else callee] += 1
    return funcs


def retail_name(name):
    return name[6:] if name.startswith('mp_be_') else name


def retail_of(funcs, retail, key):
    return retail.get(retail_name(funcs[key]['name'])) if key in funcs else None


def totals(funcs, retail):
    """Fold inlined callees into their callers: internal helpers absent from
    the retail program, and calls to retail functions the console compiler
    inlined (fewer bl to them in the retail caller than calls here; a
    partially inlined function, e.g. recursion kept as a call, is missed)."""
    memo = {}

    def inlined(key, callee, n):
        ci = funcs.get(callee)
        if not ci or callee == key:
            return 0
        rc = retail_of(funcs, retail, callee)
        if rc is None:
            return n if ci['internal'] else 0
        rf = retail_of(funcs, retail, key)
        return max(0, n - rf['calls'][rc['addr']]) if rf is not None else 0

    def total(key, depth=0):
        if key in memo:
            return memo[key]
        info = funcs[key]
        t32, t64, sq = info['own32'], info['own64'], info['sqrt_calls']
        if depth < 12:
            for callee, n in info['calls'].items():
                n = inlined(key, callee, n)
                if n:
                    a, b, c = total(callee, depth + 1)
                    t32 += n * a
                    t64 += n * b
                    sq += n * c
        memo[key] = (t32, t64, sq)
        return memo[key]

    return {key: total(key) for key in funcs}


def relevance(source):
    for i, prefix in enumerate(RELEVANCE):
        if prefix in source:
            return i
    return len(RELEVANCE)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine-dir', type=Path, default=ROOT / 'build/engine-opt')
    ap.add_argument('--top', type=int, default=60)
    ap.add_argument('--all', action='store_true', help='list every mismatch')
    ap.add_argument('--write-no-contract', action='store_true')
    ap.add_argument('--function', action='append', default=[], help='show one function in detail')
    a = ap.parse_args()
    no_contract = fp_contract.load_no_contract()
    retail = retail_counts()
    funcs = native_counts(a.engine_dir, no_contract)
    tot = totals(funcs, retail)

    for name in a.function:
        for key in [k for k in funcs if funcs[k]['name'] == name]:
            print(key, 'retail', retail_of(funcs, retail, key), 'native own',
                  {k: v for k, v in funcs[key].items() if k != 'calls'}, 'with inlined callees', tot.get(key))
            print('  calls', dict(funcs[key]['calls']))

    # Our sqrtf calls stand for sqrtf__Ff calls too (an out-of-line copy of
    # the inline sqrtf in the retail program).
    sqrtf_ff = retail['sqrtf__Ff']['addr'] if 'sqrtf__Ff' in retail else None
    rows, single_ok, double_ok, compared, fused = [], 0, 0, 0, 0
    for key, (t32, t64, sq) in tot.items():
        r = retail_of(funcs, retail, key)
        if r is None:
            continue
        compared += 1
        fused += r['single'] > 0
        retail64 = r['double'] + r['sqrt_double'] + 3 * r['calls'][sqrtf_ff]
        native64 = t64 + sq
        ok32, ok64 = r['single'] == t32, retail64 == native64
        single_ok += ok32
        double_ok += ok64
        if not (ok32 and ok64):
            # A loop the console unrolled shows a multiple of our count.
            note = 'unrolled?' if t32 and r['single'] > t32 and r['single'] % t32 == 0 else ''
            rows.append((relevance(funcs[key]['source']), -abs(r['single'] - t32), funcs[key]['name'], r['single'],
                         t32, retail64, native64, funcs[key]['source'].replace('upstream_melee_src_', ''), note))
    rows.sort()
    print(f'functions compared: {compared} ({fused} with single-precision fused ops on the console); '
          f'single-precision fused count equal: {single_ok}; double equal: {double_ok}; mismatches: {len(rows)}')
    print(f'{"function":44s} {"retail32":>8s} {"clang32":>8s} {"retail64":>8s} {"clang64":>8s}  source')
    for row in rows if a.all else rows[:a.top]:
        _, _, fn, r32, n32, r64, n64, src, note = row
        print(f'{fn:44s} {r32:8d} {n32:8d} {r64:8d} {n64:8d}  {src} {note}')

    if a.write_no_contract:
        add = collections.defaultdict(set)
        for key, (t32, t64, sq) in tot.items():
            r = retail_of(funcs, retail, key)
            if r is None:
                continue
            if r['single'] == 0 and funcs[key]['own32']:
                add[key].add('f32')
            if r['double'] + r['sqrt_double'] == 0 and funcs[key]['own64']:
                add[key].add('f64')
        # Static helpers whose every caller the console compiled unfused.
        callers = collections.defaultdict(set)
        for key, info in funcs.items():
            for callee in info['calls']:
                callers[callee].add(key)
        for helper, info in funcs.items():
            if not info['internal'] or retail_of(funcs, retail, helper) or not callers[helper]:
                continue
            for kind, own in (('f32', 'own32'), ('f64', 'own64')):
                if not info[own]:
                    continue
                rs = [retail_of(funcs, retail, c) for c in callers[helper]]
                if all(r is not None for r in rs) and all(
                        (r['single'] if kind == 'f32' else r['double'] + r['sqrt_double']) == 0 for r in rs):
                    add[helper].add(kind)
        path = fp_contract.NO_CONTRACT_FILE
        lines = path.read_text(encoding='utf-8').splitlines() if path.exists() else []
        added = 0
        for key in sorted(add):
            info = funcs[key]
            kinds = add[key] - fp_contract.kinds_for(no_contract, info['source'], info['name'])
            if kinds:
                lines.append(key + ' ' + ' '.join(sorted(kinds)) + '  # census: console fused none')
                added += 1
        path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        print(f'no_contract.txt: {added} entries added')


if __name__ == '__main__':
    main()
