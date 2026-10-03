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
* fptoui to i8/i16 -> fptosi.sat to i32, then trunc. MWCC converts a float
  to u8/u16 with fctiwz (signed, toward zero, saturating) and stores the low
  bits, so -102.0 becomes 0x9A. ARM's unsigned convert clamps it to 0. Nana
  records Popo's stick this way (ftCo_800B0918), so every left/down input
  reached her as neutral. 32-bit unsigned already agrees: MWCC calls
  __cvt_fp2unsigned, which clamps like ARM.

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
NARROW = re.compile(r'^\s*(%[-\w.$]+) = (fpext|sitofp|uitofp) (?:[a-z]+ )*(float|i1|i8|i16) (\S+) to double')
FPTOUI = re.compile(r'^(\s*)(%[-\w.$]+) = fptoui (float|double) (\S+) to (i8|i16)\s*$')
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


def _constant_bits(text):
    """Significant bits of an LLVM double constant, or None."""
    import struct
    try:
        if text.startswith('0x') and len(text) == 18:
            bits = int(text[2:], 16)
        else:
            bits = struct.unpack('>Q', struct.pack('>d', float(text)))[0]
    except ValueError:
        return None
    if not bits << 1 & ((1 << 64) - 1):
        return 0
    mantissa = (bits & ((1 << 52) - 1)) | (1 << 52)
    return 53 - ((mantissa & -mantissa).bit_length() - 1)


def _value(operand, ty):
    # Drop the type and any parameter attributes ("float noundef %3").
    words = operand.split()
    assert words[0] == ty, operand
    return words[-1]


def _width(value, narrow, negs):
    """Upper bound on the significant bits of a double SSA value."""
    while value in negs:
        value = negs[value]
    if value in narrow:
        return narrow[value]
    if not value.startswith('%'):
        bits = _constant_bits(value)
        if bits is not None:
            return bits
    return 53


def rewrite(ir, no_contract=None, keep_fused=False, module=''):
    """Return (new_ir, stats). keep_fused leaves fused f32 calls in place
    (census builds count them after optimization)."""
    no_contract = no_contract or {}
    out, stats = [], {'f32': 0, 'f64': 0, 'f32_unfused': 0, 'f64_unfused': 0, 'fnmsub': 0}
    fn, kinds, negs, narrow, counter = None, set(), {}, {}, 0
    uses_fma = False
    sat = set()
    for line in ir.splitlines():
        m = DEFINE.match(line)
        if m:
            fn = m.group(2)
            kinds = kinds_for(no_contract, module, fn)
            negs, narrow = {}, {}
            out.append(line)
            continue
        if line.startswith('}'):
            fn = None
        if VECTOR.search(line):
            raise ValueError('vector fmuladd is not lowered: ' + line.strip())
        n = FNEG.match(line)
        if n:
            negs[n.group(1)] = n.group(3).split()[-1]
        w = NARROW.match(line)
        if w:
            narrow[w.group(1)] = {'float': 24, 'i1': 1, 'i8': 8, 'i16': 16}[w.group(3)]
        u = FPTOUI.match(line)
        if u and fn is not None:
            indent, result, src, value, dst = u.groups()
            counter += 1
            t = '%mp.fc' + str(counter) + '.'
            suffix = 'f32' if src == 'float' else 'f64'
            sat.add((src, suffix))
            out.append(f'{indent}{t}i = call i32 @llvm.fptosi.sat.i32.{suffix}({src} {value})')
            out.append(f'{indent}{result} = trunc i32 {t}i to {dst}')
            stats['fptoui_narrow'] = stats.get('fptoui_narrow', 0) + 1
            continue
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
        elif _width(a, narrow, negs) + _width(b, narrow, negs) <= 53:
            # Both factors have few significant bits (floats, small integers,
            # short constants): the product is exact in double and the fused
            # result is one rounded double add, as in the f32 case.
            stats['f64_exact_product'] = stats.get('f64_exact_product', 0) + 1
            out.append(f'{indent}{t}p = fmul double {a}, {b}')
            if negated:
                out.append(f'{indent}{t}s = fsub double {t}p, {addend}')
                out.append(f'{indent}{result} = fneg double {t}s')
            else:
                out.append(f'{indent}{result} = fadd double {t}p, {addend}')
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
    for src, suffix in sorted(sat):
        if not re.search(r'^declare\b[^\n]*@llvm\.fptosi\.sat\.i32\.' + suffix + r'\(', text, re.M):
            text += f'\ndeclare i32 @llvm.fptosi.sat.i32.{suffix}({src})\n'
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
