"""Lower clang's floating-point contractions the way the GameCube rounds them.

The decomp sources are compiled with -ffp-contract=on: clang fuses a*b+c
only inside one expression (as the console compiler, MWCC, did) and marks
each fusion as an @llvm.fmuladd call in its front-end IR. The ARM11 VFP has
no fused multiply-add, and the backend would split every call into a rounded
multiply and a rounded add. This module rewrites those calls in the
front-end IR (before any optimization or inlining, so each call still sits
in the function whose source wrote it):

* llvm.fmuladd.f32 -> Gekko fmadds/fmsubs/fnmsubs as Dolphin computes them:
  round_single(round_double(a*c +- b)). The product of two floats is exact
  in double, so this is (float)((double)a*(double)c + (double)b) with
  ordinary double operations (fpext, fmul, fadd/fsub, fptrunc).
  A negated multiplicand (clang's form of "b - a*c") becomes PowerPC
  fnmsubs, -(a*c - b), so an exact cancellation gives -0 like the console.
* llvm.fmuladd.f64 -> the correctly rounded software double fma mp_fma()
  (port/engine/fp_exact.c): MWCC's fmadd/fnmsub, e.g. the Newton steps of
  MSL's inline sqrtf. Same fnmsub sign rule.
* In functions listed as no-contract (the console fused nothing there), the
  call becomes a plain multiply and add, each rounded.

See docs/slippi/determinism.md.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NO_CONTRACT_FILE = ROOT / 'tools/slippi/no_contract.txt'

DEFINE = re.compile(r'^define\b.*?@("?)([^"(\s]+)\1\(')
CALL = re.compile(r'^(\s*)(%[-\w.$]+) = (?:tail |notail )?call (?:[a-z]+ )*(float|double) '
                  r'@llvm\.fmuladd\.(f32|f64)\((.*)\)(.*)$')
VECTOR = re.compile(r'@llvm\.fmuladd\.v\d+')
FNEG = re.compile(r'^\s*(%[-\w.$]+) = fneg (?:[a-z]+ )*(float|double) (.+?)\s*(?:,\s*!.*)?$')


def load_no_contract(path=NO_CONTRACT_FILE):
    """{function name: set of kinds ('f32','f64')} from the no-contract list.

    Line format: `name` (single and double) or `name f32` / `name f64`;
    '#' starts a comment.
    """
    out = {}
    if not path.exists():
        return out
    for line in path.read_text(encoding='utf-8').splitlines():
        line = line.split('#', 1)[0].split()
        if not line:
            continue
        kinds = set(line[1:]) or {'f32', 'f64'}
        assert kinds <= {'f32', 'f64'}, line
        out.setdefault(line[0], set()).update(kinds)
    return out


def kinds_for(no_contract, module, name):
    """Kinds unfused in function `name` of module `module` (the engine
    build's object stem, e.g. upstream_melee_src_melee_lb_lbvector): a plain
    entry names any function, 'module::name' one static function."""
    return no_contract.get(name, set()) | no_contract.get(module + '::' + name, set())


def _operands(text):
    parts, depth, cur = [], 0, ''
    for ch in text:
        if ch in '([{<':
            depth += 1
        elif ch in ')]}>':
            depth -= 1
        if ch == ',' and depth == 0:
            parts.append(cur.strip())
            cur = ''
        else:
            cur += ch
    parts.append(cur.strip())
    return parts


def _value(operand, ty):
    # Drop the type and any parameter attributes ("float noundef %3").
    words = operand.split()
    assert words[0] == ty, operand
    return words[-1]


def rewrite(ir, no_contract=None, keep_fused=False, module=''):
    """Return (new_ir, stats). keep_fused leaves fused f32 calls in place
    (census builds count them after optimization)."""
    no_contract = no_contract or {}
    out, stats = [], {'f32': 0, 'f64': 0, 'f32_unfused': 0, 'f64_unfused': 0, 'fnmsub': 0}
    fn, kinds, negs, counter = None, set(), {}, 0
    uses_fma = False
    for line in ir.splitlines():
        m = DEFINE.match(line)
        if m:
            fn = m.group(2)
            kinds = kinds_for(no_contract, module, fn)
            negs = {}
            out.append(line)
            continue
        if line.startswith('}'):
            fn = None
        if VECTOR.search(line):
            raise ValueError('vector fmuladd is not lowered: ' + line.strip())
        n = FNEG.match(line)
        if n:
            negs[n.group(1)] = n.group(3).split()[-1]
        c = CALL.match(line)
        if not c or fn is None:
            out.append(line)
            continue
        indent, result, ty, kind, args, tail = c.groups()
        a, b, addend = (_value(x, ty) for x in _operands(args))
        counter += 1
        t = '%mp.fc' + str(counter) + '.'
        if kind in kinds:
            # The console fused nothing here: round the product, then add.
            out.append(f'{indent}{t}p = fmul {ty} {a}, {b}')
            out.append(f'{indent}{result} = fadd {ty} {t}p, {addend}')
            stats[kind + '_unfused'] += 1
            continue
        stats[kind] += 1
        negated = a in negs
        if negated:
            a = negs[a]
            stats['fnmsub'] += 1
        if kind == 'f32':
            if keep_fused:
                out.append(line)
                continue
            out.append(f'{indent}{t}a = fpext float {a} to double')
            out.append(f'{indent}{t}b = fpext float {b} to double')
            out.append(f'{indent}{t}c = fpext float {addend} to double')
            out.append(f'{indent}{t}p = fmul double {t}a, {t}b')
            if negated:
                # fnmsubs: -(a*c - b), rounded to double, then single.
                out.append(f'{indent}{t}s = fsub double {t}p, {t}c')
                out.append(f'{indent}{t}r = fptrunc double {t}s to float')
                out.append(f'{indent}{result} = fneg float {t}r')
            else:
                out.append(f'{indent}{t}s = fadd double {t}p, {t}c')
                out.append(f'{indent}{result} = fptrunc double {t}s to float')
        else:
            uses_fma = True
            if negated:
                out.append(f'{indent}{t}c = fneg double {addend}')
                out.append(f'{indent}{t}r = call double @mp_fma(double {a}, double {b}, double {t}c)')
                out.append(f'{indent}{result} = fneg double {t}r')
            else:
                out.append(f'{indent}{result} = call double @mp_fma(double {a}, double {b}, double {addend})')
    text = '\n'.join(out) + '\n'
    if uses_fma and not re.search(r'^(declare|define)\b[^\n]*@mp_fma\(', text, re.M):
        text += '\ndeclare double @mp_fma(double, double, double)\n'
    return text, stats


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('input')
    ap.add_argument('output')
    a = ap.parse_args()
    new, stats = rewrite(Path(a.input).read_text(encoding='utf-8'), load_no_contract())
    Path(a.output).write_text(new, encoding='utf-8')
    print(stats)
