"""Compiles the game's units (source/ and port/linux/game) for Windows 9x.

The units are compiled as the Linux build compiles them (its flags, from
`ninja -t commands linux`), retargeted to i686-w64-windows-gnu for a Pentium
II without SSE, on mingw-w64's C runtime headers (port/win98/include adapts
them), and through hybrid precision (port/win98/precision): LLVM IR, then
rewrite.py, then x87 code.

Needs a configured Linux build (python configure.py, then
ninja build/linux/halo_msvc_semantics.h) and mingw-w64's headers:

    MINGW_SOURCE=/path/to/mingw-w64 python3 port/win98/build_game.py [--syntax-only]

Objects go to build/win98/obj. The platform layer, the link and the renderer
are not done yet.
"""

import collections
import concurrent.futures
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / "port" / "win98"
BUILD = ROOT / "build" / "win98"
OBJ = BUILD / "obj"
TARGET = ["--target=i686-w64-windows-gnu", "-march=pentium2"]


def mingw_headers() -> Path:
    """mingw's C runtime headers, with its configured _mingw.h (Windows 98,
    msvcrt.dll) written to build/win98/include"""
    mingw = Path(os.environ.get("MINGW_SOURCE", ""))
    crt = mingw / "mingw-w64-headers" / "crt"
    if not (crt / "_mingw.h.in").is_file():
        raise SystemExit("set MINGW_SOURCE to a mingw-w64 source tree (with mingw-w64-headers)")
    include = BUILD / "include"
    (include / "sdks").mkdir(parents=True, exist_ok=True)
    text = (crt / "_mingw.h.in").read_text()
    text = text.replace("@DEFAULT_MSVCRT_VERSION@", "0x600").replace("@DEFAULT_WIN32_WINNT@", "0x0410")
    (include / "_mingw.h").write_text(text)
    ddk = re.sub(r"@DEFAULT_\w+@", "0", (crt / "sdks" / "_mingw_ddk.h.in").read_text())
    (include / "sdks" / "_mingw_ddk.h").write_text(ddk)
    return crt


def resource_dir() -> str:
    return subprocess.run(["clang", "-print-resource-dir"], capture_output=True, text=True,
                          check=True).stdout.strip()


def linux_commands():
    """the Linux build's compile commands for the game's units"""
    text = subprocess.run(["ninja", "-C", str(ROOT), "-t", "commands", "linux"], capture_output=True,
                          text=True, check=True).stdout
    return [c for c in text.splitlines()
            if c.startswith("clang") and re.search(r" -c (source|port/linux/game)/\S+\.c ", c)]


def win9x_arguments(command: str, crt: Path, resources: str):
    """a Linux compile command's flags, retargeted; returns (source, flags)"""
    args = shlex.split(command)
    source = args[args.index("-c") + 1]
    drop = {"-flto=full", "-g", "-MMD", "-m32", "-fno-pic", "-fstack-protector-strong"}
    flags = []
    skip = 0
    for index, arg in enumerate(args[1:], 1):
        if skip:
            skip -= 1
            continue
        if arg in drop or arg.startswith(("-march", "--target")):
            continue
        if arg in ("-MF", "-o", "-c"):
            skip = 1
            continue
        if arg == "-include" and args[index + 1] == "port/linux/include/halo_linux_prefix.h":
            flags += ["-include", "port/win98/include/halo_win9x_prefix.h"]
            skip = 1
            continue
        if arg == "-Iport/linux/include":
            flags += ["-Iport/win98/include", "-Iport/linux/include"]
            continue
        flags.append(arg)
    # mingw's C runtime headers only: the game's Windows declarations are the
    # XDK's (port/include/xdk), never the Windows SDK's
    return source, TARGET + ["-g0", "-nostdinc", "-isystem", f"{resources}/include",
                             "-isystem", str(BUILD / "include"), "-isystem", str(crt)] + flags


def compile_unit(job):
    source, flags, syntax_only = job
    run = lambda a: subprocess.run(a, cwd=ROOT, capture_output=True, text=True)
    if syntax_only:
        r = run(["clang", *flags, "-fsyntax-only", source])
        return source, "ok" if r.returncode == 0 else "error", r.stderr
    name = source.replace("/", "__")[:-2]
    ir, rewritten, obj = OBJ / f"{name}.ll", OBJ / f"{name}.x87.ll", OBJ / f"{name}.o"
    r = run(["clang", *flags, "-S", "-emit-llvm", source, "-o", str(ir)])
    if r.returncode:
        return source, "error", r.stderr
    r = run([sys.executable, str(HERE / "precision" / "rewrite.py"), str(ir), str(rewritten)])
    if r.returncode:
        return source, "error", r.stderr
    counts = r.stderr
    # (the IR is optimised already: only code generation, which must not
    # move arithmetic across the precision helpers' calls)
    r = run(["clang", *TARGET, "-O2", "-Xclang", "-disable-llvm-optzns", "-c", str(rewritten), "-o", str(obj)])
    if r.returncode:
        return source, "error", r.stderr
    return source, "ok", counts


def main() -> None:
    syntax_only = "--syntax-only" in sys.argv
    crt = mingw_headers()
    resources = resource_dir()
    OBJ.mkdir(parents=True, exist_ok=True)
    jobs = [(*win9x_arguments(c, crt, resources), syntax_only) for c in linux_commands()]
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        results = list(pool.map(compile_unit, jobs))
    failed = [r for r in results if r[1] != "ok"]
    totals = collections.Counter()
    for _, status, text in results:
        match = re.search(r"\{.*\}", text) if status == "ok" else None
        if match:
            totals.update(eval(match.group(0)))
    print(f"{len(results) - len(failed)} of {len(results)} units compiled")
    if totals:
        print("hybrid precision:", dict(totals))
    for source, _, text in failed[:5]:
        print(f"--- {source}\n{text[-1500:]}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
