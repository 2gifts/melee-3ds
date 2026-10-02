"""Carry Melee Unlocked's floating-point source annotations over to this port.

Melee Unlocked (GPL-3.0-or-later) compiles the same decomp commit (039c4bf)
and matches Slippi replays. Its sourceport/patches/melee-native.patch mixes
x86-64 adaptations with float annotations; this extracts only the latter:

* MU_P(x): a product rounded on its own (no contraction into the sum);
* mu_fnmsubs / mu_fmadds / mu_fmsubs / MU_FMADDS: explicit fused operations
  where the console compiler fused across statements.

(MU_NO_CONTRACT function tags are not ported as source: the census,
tools/slippi/fma_census.py, derives the equivalent list for clang from the
retail code itself; --check-tags compares the two.)

Each '-'/'+' change block whose '+' side carries an annotation is checked:
with the annotations stripped and parentheses ignored, the '+' tokens must
equal the '-' tokens (a pure annotation, no x86 adaptation mixed in). The
'-' text must occur in this port's overlaid source; context lines are added
until it is unique. Pure blocks are written to
tools/slippi_edits/determinism_ported.py; everything else is listed for
hand porting (docs/slippi/determinism.md records what was done).

    python tools/slippi/port_mu_annotations.py --patch <melee-native.patch>
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
UPSTREAM = ROOT / 'upstream/melee'
DEFAULT_PATCH = ROOT.parents[1] / 'slippi-research/melee-unlocked/sourceport/patches/melee-native.patch'
OUT = ROOT / 'tools/slippi_edits/determinism_ported.py'

ANNOT = re.compile(r'\bMU_P\s*\(|\bmu_fnmsubs\s*\(|\bMU_FMADDS\s*\(|\bmu_fmadds\s*\(|\bmu_fmsubs\s*\(')
TOKEN = re.compile(r'0[xX][0-9a-fA-F]+[uUlL]*|\d+\.\d*(?:[eE][-+]?\d+)?[fF]?|\.\d+(?:[eE][-+]?\d+)?[fF]?|'
                   r'\d+(?:[eE][-+]?\d+)?[fFuUlL]*|[A-Za-z_]\w*|->|\+\+|--|<<=|>>=|<<|>>|[-+*/%&|^!<>=]=|&&|\|\||\S')


def tokens(text):
    text = re.sub(r'//[^\n]*|/\*.*?\*/', ' ', text, flags=re.S)
    return TOKEN.findall(text)


def unwrap_mu_p(text):
    """MU_P(x) -> (x), MU_NO_CONTRACT dropped."""
    text = text.replace('MU_NO_CONTRACT ', '')
    while True:
        m = re.search(r'\bMU_P\s*\(', text)
        if not m:
            return text
        i = m.end() - 1
        depth = 0
        for j in range(i, len(text)):
            if text[j] == '(':
                depth += 1
            elif text[j] == ')':
                depth -= 1
                if depth == 0:
                    break
        text = text[:m.start()] + text[i:j + 1] + text[j + 1:]


def parse_patch(path):
    """Yield (decomp path, [(first original line, hunk lines)])."""
    text = Path(path).read_text(encoding='utf-8', errors='replace')
    for part in re.split(r'(?m)^(?=diff --git )', text):
        m = re.match(r'diff --git a/(\S+) b/(\S+)', part)
        if not m:
            continue
        hunks = re.split(r'(?m)^(?=@@ )', part)[1:]
        yield m.group(1), [(int(re.match(r'@@ -(\d+)', h).group(1)), h.splitlines()[1:]) for h in hunks]


def blocks(first, hunk):
    """Yield (line, minus, plus) per change block; line is the 0-based line
    of the block in the original file."""
    i, n, line = 0, len(hunk), first - 1
    while i < n:
        if hunk[i][:1] in '-+':
            at, minus, plus = line, [], []
            while i < n and hunk[i][:1] in '-+':
                if hunk[i][0] == '-':
                    minus.append(hunk[i][1:])
                    line += 1
                else:
                    plus.append(hunk[i][1:])
                i += 1
            yield at, minus, plus
        else:
            if hunk[i][:1] in (' ', ''):
                line += 1
            i += 1


def overlaid_text(source):
    """This port's text of a decomp source before the Slippi edits."""
    import engine_overlays
    import layout_fixes
    import gameplay_mods
    import presentation_mods
    text = engine_overlays._adapt(source).read_text(encoding='utf-8')
    for module in (layout_fixes, gameplay_mods, presentation_mods):
        for old, new, count in module.edits_for(source):
            text = text.replace(old, new)
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--patch', default=str(DEFAULT_PATCH))
    ap.add_argument('--report', type=Path, help='write the list of blocks not ported')
    a = ap.parse_args()
    ported, manual, stats = {}, [], dict(blocks=0, ported=0, impure=0, missing=0)
    texts = {}
    for path, hunks in parse_patch(a.patch):
        source = UPSTREAM / path
        if not source.exists() or source.suffix not in ('.c', '.h'):
            continue
        original = source.read_text(encoding='utf-8').split('\n')
        for first, hunk in hunks:
            for at, minus, plus in blocks(first, hunk):
                plus_text = '\n'.join(plus)
                if not ANNOT.search(plus_text):
                    continue
                stats['blocks'] += 1
                minus_text = '\n'.join(minus)
                where = f'{path}:{at + 1}: ' + (minus[0].strip() if minus else plus[0].strip())[:90]
                if not minus or [t for t in tokens(minus_text) if t not in '()'] != \
                        [t for t in tokens(unwrap_mu_p(plus_text)) if t not in '()']:
                    stats['impure'] += 1
                    manual.append(('not a pure annotation', where, minus_text, plus_text))
                    continue
                assert original[at:at + len(minus)] == minus, (path, at, minus[:1])
                if source.suffix == '.h':
                    # Overlays apply to compiled .c files only. A header's
                    # static inline helper is unfused through
                    # tools/slippi/no_contract.txt instead (by name, in every
                    # file that includes it).
                    stats['header'] = stats.get('header', 0) + 1
                    manual.append(('header: tag the helper in no_contract.txt', where, minus_text, plus_text))
                    continue
                if source not in texts:
                    texts[source] = overlaid_text(source)
                text = texts[source]
                old, new = minus_text, plus_text.replace('MU_NO_CONTRACT ', '')
                # Widen with the original file's own lines until unique.
                b, c = at - 1, at + len(minus)
                while text.count(old) > 1 and (b >= 0 or c < len(original)) and at - b < 80:
                    if b >= 0:
                        old, new = original[b] + '\n' + old, original[b] + '\n' + new
                        b -= 1
                    if text.count(old) > 1 and c < len(original):
                        old, new = old + '\n' + original[c], new + '\n' + original[c]
                        c += 1
                if text.count(old) != 1:
                    stats['missing'] += 1
                    manual.append((f'text found {text.count(old)} times', where, minus_text, plus_text))
                    continue
                key = path[len('src/'):] if path.startswith('src/') else path
                ported.setdefault(key, []).append((old, new, 1))
                stats['ported'] += 1
    # Blocks are applied one after another: a widened context must not
    # contain another block's original text.
    for key, edits in ported.items():
        for i, (old, new, _) in enumerate(edits):
            for other, _, _ in edits[i + 1:]:
                assert other not in old and old not in other, ('overlapping blocks', key, old[:60])
    lines = ['"""Generated by tools/slippi/port_mu_annotations.py from Melee Unlocked\'s',
             'sourceport/patches/melee-native.patch (GPL-3.0-or-later): its MU_P(...) and',
             'explicit fused-operation annotations, verified to change nothing but the',
             'rounding. Do not edit; re-run the script (see determinism.py)."""', '', 'PORTED = {']
    for key in sorted(ported):
        lines.append(f'    {key!r}: [')
        for old, new, count in ported[key]:
            lines.append(f'        ({old!r},\n         {new!r}, {count}),')
        lines.append('    ],')
    lines.append('}')
    OUT.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(stats)
    report = []
    for why, where, minus_text, plus_text in manual:
        report.append(f'### {where}\n({why})\n--- console source\n{minus_text}\n+++ Melee Unlocked\n{plus_text}\n')
    if a.report:
        a.report.write_text('\n'.join(report), encoding='utf-8')
    else:
        print('\n'.join(report))


if __name__ == '__main__':
    main()
