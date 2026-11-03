"""Builds the precision test three ways (sse2, naive x87, hybrid x87) from the
game's own maths sources; run each OUT/<variant>/test and compare the hashes.
Needs a configured Linux build (python configure.py) for the compile flags.
EXTRA_CFLAGS adds flags (e.g. include paths for 32-bit glibc headers).

  python3 port/win98/precision/test/build.py sse2 naive hybrid
"""
import shlex, subprocess, sys, os, re, glob
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '../../../..'))
OUT = os.environ.get('OUT', '/tmp/x87_precision_test')
PRECISION = os.path.dirname(HERE)
all_cmds = subprocess.run(['ninja', '-C', REPO, '-t', 'commands', 'linux'], capture_output=True, text=True).stdout.splitlines()
def cmd_for(src):
    for c in all_cmds:
        if c.startswith('clang') and f' -c {src} ' in c:
            return c
    raise SystemExit('no command for ' + src)
files = ['source/math/real_math.c', 'source/math/matrix_math.c', 'source/math/periodic_functions.c', 'source/math/random_math.c'] + \
    sorted('port/third_party/musl-math/src/' + os.path.basename(f) for f in glob.glob(REPO + '/port/third_party/musl-math/src/*.c'))
game = {'source/math/real_math.c', 'source/math/matrix_math.c', 'source/math/periodic_functions.c', 'source/math/random_math.c', 'driver.c'}
def flags(c, march, extra):
    a = shlex.split(c)
    i = a.index('-c'); del a[i:i + 2]
    i = a.index('-o'); del a[i:i + 2]
    i = a.index('-MF'); del a[i:i + 2]
    a = [x for x in a if x not in ('-flto=full', '-g', '-MMD', '-fstack-protector-strong') and not x.startswith('-march')]
    return a + [march, '-g0', '-fno-stack-protector', *os.environ.get('EXTRA_CFLAGS', '').split()] + extra
def run(a):
    r = subprocess.run(a, cwd=REPO, capture_output=True, text=True)
    if r.returncode: print(' '.join(a)[:300], r.stderr[-2000:]); raise SystemExit(1)
def build(variant):
    out = f'{OUT}/{variant}'; os.makedirs(out, exist_ok=True)
    objs = []
    srcs = files + ['driver.c']
    for src in srcs:
        c = cmd_for('source/math/real_math.c') if src == 'driver.c' else cmd_for(src)
        path = os.path.join(HERE, 'driver.c') if src == 'driver.c' else src
        name = os.path.basename(src)[:-2]
        if variant == 'sse2':
            a = flags(c, '-march=x86-64', ['-c', path, '-o', f'{out}/{name}.o'])
            run(a)
        else:
            cw = '0x007F' if variant == 'hybrid' else '0x037F'
            base = flags(c, '-march=pentium2', ['-DX87', f'-DX87_START_CW={cw}'])
            if variant == 'hybrid' and src in game:
                run(base + ['-S', '-emit-llvm', path, '-o', f'{out}/{name}.ll'])
                r = subprocess.run([sys.executable, os.path.join(PRECISION, 'rewrite.py'), f'{out}/{name}.ll', f'{out}/{name}.x87.ll'], capture_output=True, text=True)
                sys.stderr.write(r.stderr)
                if r.returncode: raise SystemExit(r.stderr)
                run(['clang', '--target=i686-linux-gnu', '-m32', '-march=pentium2', '-O2', '-Xclang', '-disable-llvm-optzns', '-c', f'{out}/{name}.x87.ll', '-o', f'{out}/{name}.o'])
            else:
                extra = []
                if variant == 'hybrid' and 'musl-math' in src:
                    # hybrid precision runs the maths library in double precision: method 0
                    extra = ['-ffp-eval-method=source']
                run(base + extra + ['-c', path, '-o', f'{out}/{name}.o'])
        objs.append(f'{out}/{name}.o')
    run(['clang', '--target=i686-linux-gnu', '-m32', '-O1', '-ffreestanding', '-fno-builtin', '-fno-pic', '-march=x86-64' if variant == 'sse2' else '-march=pentium2', '-c', os.path.join(HERE, 'stubs.c'), '-o', f'{out}/stubs.o'])
    objs.append(f'{out}/stubs.o')
    if variant != 'sse2':
        run(['clang', '--target=i686-linux-gnu', '-m32', '-c', os.path.join(PRECISION, 'x87_helpers.S'), '-o', f'{out}/x87_helpers.o'])
        objs.append(f'{out}/x87_helpers.o')
    # stubs for what is referenced but not exercised
    link = ['clang', '--target=i686-linux-gnu', '-m32', '-nostdlib', '-static', '-fuse-ld=lld', '-Wl,--unresolved-symbols=ignore-all', '-Wl,-e,_start'] + objs + ['-o', f'{out}/test']
    run(link)
for v in sys.argv[1:]:
    build(v)
    print('built', v)
