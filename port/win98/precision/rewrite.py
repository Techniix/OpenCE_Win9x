"""Hybrid precision for x87: rewrite double arithmetic and maths-library calls
in LLVM IR into calls to x87_helpers.S, which run them in double precision.
Single-precision arithmetic stays inline (the x87 runs in single precision).
Integer-to-float conversions are rounded to single at once (fild is exact)."""
import re, sys
BIN = re.compile(r'^(\s*%[\w.]+ = )f(add|sub|mul|div) ((?:(?:nnan|ninf|nsz|arcp|contract|afn|reassoc|fast) )*)double (\S+), (\S+)$')
SQRT = re.compile(r'call double @llvm\.sqrt\.f64\(')
MATH = re.compile(r'@(halo_(?:sin|cos|tan|asin|acos|atan|atan2|exp|log|log2|log10|pow)|fmod|sqrt)\(')
ITOF = re.compile(r'^(\s*)(%[\w.]+) = ([su]itofp) (i\d+) (\S+) to float$')
counts = {'double ops': 0, 'sqrt': 0, 'math calls': 0, 'int->float': 0}
out = []
declared = set()
n = 0
for line in open(sys.argv[1]):
    line = line.rstrip('\n')
    m = BIN.match(line)
    if m:
        out.append(f'{m.group(1)}call double @x87_d{m.group(2)}(double {m.group(4)}, double {m.group(5)})')
        declared.add(f'x87_d{m.group(2)}'); counts['double ops'] += 1; continue
    if SQRT.search(line):
        line = SQRT.sub('call double @x87_dsqrt(', line); declared.add('x87_dsqrt'); counts['sqrt'] += 1
    if not line.startswith('declare') and not line.startswith('define'):
        def sub(m):
            declared.add('x87_' + m.group(1)); counts['math calls'] += 1
            return '@x87_' + m.group(1) + '('
        line = MATH.sub(sub, line)
    m = ITOF.match(line)
    if m:
        n += 1
        tmp = f'%x87.itof.{n}'
        out.append(f'{m.group(1)}{tmp} = {m.group(3)} {m.group(4)} {m.group(5)} to float')
        out.append(f'{m.group(1)}{m.group(2)} = call float @x87_round_f32(float {tmp})')
        declared.add('x87_round_f32'); counts['int->float'] += 1; continue
    out.append(line)
sigs = {'x87_round_f32': 'float @x87_round_f32(float)', 'x87_dsqrt': 'double @x87_dsqrt(double)'}
for name in sorted(declared):
    if name in sigs: sig = sigs[name]
    elif name in ('x87_halo_atan2', 'x87_halo_pow', 'x87_fmod') or name[5:] in ('add','sub','mul','div'): sig = f'double @{name}(double, double)'
    else: sig = f'double @{name}(double)'
    if not any(l.startswith(f'declare') and f'@{name}(' in l for l in out):
        out.append(f'declare {sig}')
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
print(sys.argv[1].split('/')[-1], counts, file=sys.stderr)
