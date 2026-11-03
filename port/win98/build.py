"""The Windows 9x build (port/win98/README.md), from Linux.

    python3 port/win98/build.py [--syntax-only] [--clean] [--jobs N]

What it compiles, and how:
- the game's units (source/, port/linux/game) as the Linux build compiles
  them (its flags, from `ninja -t commands linux`), retargeted to
  i686-w64-windows-gnu for an i586 without SSE, on mingw-w64's C runtime
  headers (port/win98/include adapts them), through hybrid precision
  (port/win98/precision: LLVM IR, rewrite.py, then x87 code);
- the platform layer the Windows build compiles (port/linux/src less what
  port/windows/port.json replaces, and port/windows/src), with the Windows
  build's flags on the GNU target and mingw-w64's headers
  (halo_win9x_platform_prefix.h), and the 16-bit wide functions the game's
  units call (port/linux/src/msvc_wide.c);
- the third-party libraries the Windows build links.

It needs a configured Linux build (python3 configure.py --window-base
0x20000000) for the game's flags; mingw-w64's headers and .def files and
SDL 3's headers are cloned into build/win98/third_party the first time
(git), or taken from MINGW_SOURCE and SDL_SOURCE.

Objects go to build/win98/obj. They are linked, with the build's own runtime
(port/win98/runtime) and import libraries made from mingw-w64's definitions
(build/win98/lib), to build/win98/halo.exe; while symbols are missing, the
link lists them in build/win98/undefined.txt.
"""

import argparse
import collections
import concurrent.futures
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.linux_build import (MINIUPNPC_DEFINES, MINIUPNPC_DIR, PLATFORM_FLAGS, musl_math_sources,  # noqa: E402
                               miniupnpc_sources, opus_sources, OPUS_DEFINES, OPUS_DIR)
from tools.windows_build import (EXPAT_DIR, KCP_DIR, MBEDTLS_DIR, MONOCYPHER_DIR, TOML_DIR,  # noqa: E402
                                 WIN32_FLAGS, WINDOWS_ABI_FLAGS, ZLIB_DEFINES, ZLIB_DIR, ZLIB_SOURCES)

HERE = ROOT / "port" / "win98"
BUILD = ROOT / "build" / "win98"
OBJ = BUILD / "obj"
THIRD_PARTY = BUILD / "third_party"
# the window where Windows 9x has room for it (port/linux/include/halo_port_capacity.h)
WINDOW_BASE = 0x20000000
TARGET = ["--target=i686-w64-windows-gnu", "-march=pentium-mmx"]
MINGW_URL = "https://github.com/mirror/mingw-w64"
LLVM_URL = "https://github.com/llvm/llvm-project"
# the compiler's 64-bit division, from compiler-rt (the build links no
# libgcc or compiler-rt library)
COMPILER_RT_BUILTINS = ("divdi3.c", "moddi3.c", "udivdi3.c", "umoddi3.c", "udivmoddi4.c", "int_util.c")
# the DLLs the program imports from: Windows 98's (port/win98/probe/results)
DLLS = ("msvcrt", "kernel32", "user32", "gdi32", "advapi32", "ws2_32", "wsock32", "winmm", "shell32", "comdlg32",
        "ole32", "opengl32", "iphlpapi", "wininet", "dsound", "dinput")
SDL_URL = "https://github.com/libsdl-org/SDL"
SDL_TAG = "release-3.4.16"
MUSL_MATH_DIR = ROOT / "port" / "third_party" / "musl-math"
EXPAT_SOURCES = ("xmlparse.c", "xmlrole.c", "xmltok.c", "random_rand_s.c")


# ---------- sources of headers

def sparse_clone(url: str, destination: Path, paths, branch=None) -> Path:
    if not destination.is_dir():
        command = ["git", "clone", "-q", "--depth", "1", "--filter=blob:none", "--sparse"]
        if branch:
            command += ["--branch", branch]
        subprocess.run(command + [url, str(destination)], check=True)
        subprocess.run(["git", "-C", str(destination), "sparse-checkout", "set", *paths], check=True)
    return destination


def mingw_source() -> Path:
    if os.environ.get("MINGW_SOURCE"):
        return Path(os.environ["MINGW_SOURCE"])
    return sparse_clone(MINGW_URL, THIRD_PARTY / "mingw-w64",
                        ["mingw-w64-headers/include", "mingw-w64-headers/crt", "mingw-w64-crt/lib32",
                         "mingw-w64-crt/lib-common", "mingw-w64-crt/def-include"])


def compiler_rt_builtins() -> Path:
    if os.environ.get("COMPILER_RT_SOURCE"):
        return Path(os.environ["COMPILER_RT_SOURCE"]) / "lib" / "builtins"
    clone = THIRD_PARTY / "llvm-project"
    if not clone.is_dir():
        subprocess.run(["git", "clone", "-q", "--depth", "1", "--filter=blob:none", "--sparse", LLVM_URL, str(clone)],
                       check=True)
        subprocess.run(["git", "-C", str(clone), "sparse-checkout", "set", "--no-cone", "/compiler-rt/lib/builtins/",
                        "/compiler-rt/LICENSE.TXT"], check=True)
    return clone / "compiler-rt" / "lib" / "builtins"


def sdl_include() -> Path:
    if os.environ.get("SDL_SOURCE"):
        return Path(os.environ["SDL_SOURCE"]) / "include"
    return sparse_clone(SDL_URL, THIRD_PARTY / "SDL3", ["include"], SDL_TAG) / "include"


def mingw_headers(mingw: Path):
    """mingw's C runtime and Windows headers, with its configured _mingw.h
    (Windows 98, msvcrt.dll) written to build/win98/include"""
    crt = mingw / "mingw-w64-headers" / "crt"
    include = BUILD / "include"
    (include / "sdks").mkdir(parents=True, exist_ok=True)
    text = (crt / "_mingw.h.in").read_text()
    text = text.replace("@DEFAULT_MSVCRT_VERSION@", "0x600").replace("@DEFAULT_WIN32_WINNT@", "0x0410")
    (include / "_mingw.h").write_text(text)
    ddk = re.sub(r"@DEFAULT_\w+@", "0", (crt / "sdks" / "_mingw_ddk.h.in").read_text())
    (include / "sdks" / "_mingw_ddk.h").write_text(ddk)
    return crt, mingw / "mingw-w64-headers" / "include"


def resource_dir() -> str:
    return subprocess.run(["clang", "-print-resource-dir"], capture_output=True, text=True,
                          check=True).stdout.strip()


# ---------- the units and their flags

def linux_commands():
    """the configured Linux build's compile commands for the game's units"""
    text = subprocess.run(["ninja", "-C", str(ROOT), "-t", "commands", "linux"], capture_output=True,
                          text=True, check=True).stdout
    commands = []
    for command in text.splitlines():
        if not command.startswith("clang"):
            continue
        args = shlex.split(command)
        if "-c" in args and re.match(r"(source|port/linux/game)/.+\.c$", args[args.index("-c") + 1]):
            commands.append(command)
    if not commands:
        raise SystemExit("configure the Linux build first: python3 configure.py --window-base 0x20000000")
    if not any(f"-DHALO_PORT_WINDOW_BASE=0x{WINDOW_BASE:08X}UL" in c for c in commands):
        raise SystemExit("the Linux build is not configured with --window-base 0x20000000")
    return commands


def game_units(system):
    """(source, flags, hybrid precision) for each of the game's units"""
    units = []
    for command in linux_commands():
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
        # (the C runtime's headers only: the game's Windows declarations are
        # the XDK's, port/include/xdk)
        units.append((source, TARGET + ["-g0"] + system["crt"] + flags, True))
    # the Custom Edition maps' Ogg Vorbis decoder, as the game's units are
    units.append(("port/third_party/stb_vorbis/stb_vorbis.c",
                  units[0][1], True))
    return units


def platform_units(system, sdl: Path):
    windows = ROOT / "port" / "windows"
    abi = TARGET + ["-g0", f"-DHALO_PORT_WINDOW_BASE=0x{WINDOW_BASE:08X}UL"] + [
        flag for flag in WINDOWS_ABI_FLAGS if not flag.startswith("--target") and flag not in ("-g", "-gcodeview")]
    platform = abi + PLATFORM_FLAGS + [
        "-include", "port/win98/include/halo_win9x_platform_prefix.h",
        # (the XDK's inline functions weak, as MSVC's COMDATs merge them: the
        # Linux build's generated list, tools/linux_msvc_semantics.py)
        "-include", "build/linux/platform_msvc_semantics.h",
        "-Iport/windows/include/posix", "-Iport/windows/include/crt", "-Iport/linux/src", "-Iport/windows/include",
        f"-I{TOML_DIR}", f"-I{EXPAT_DIR}", f"-I{KCP_DIR}", f"-I{MONOCYPHER_DIR}", f"-I{ZLIB_DIR}",
        "-iquote", "port/linux/include", "-Isource", "-Isource/cseries", f"-I{sdl}", "-Iport/include/xdk",
        "-w"] + system["all"]
    win32 = abi + [flag for flag in WIN32_FLAGS if not flag.startswith("-D_WIN32_WINNT")] + [
        "-D_WIN32_WINNT=0x0410", "-include", "port/win98/include/win9x_compat.h",
        "-Iport/windows/include/posix", "-Iport/linux/src", f"-I{sdl}",
        f"-I{MBEDTLS_DIR / 'include'}", f"-I{MINIUPNPC_DIR / 'include'}", "-DMINIUPNP_STATICLIB", "-w"] + system["all"]
    import json
    replaced = set(json.loads((windows / "port.json").read_text())["replaced_platform_sources"])
    # (the game's units call the Linux build's 16-bit wide functions:
    # port/linux/include/wchar.h)
    replaced.discard("msvc_wide.c")
    units = []
    for source in sorted((ROOT / "port" / "linux" / "src").glob("*.c")):
        if source.name in replaced:
            continue
        flags = win32 if source.name == "posix_dtls.c" else platform
        if source.name == "msvc_wide.c":
            # (localtime_r: win9x_compat.c)
            flags = flags + ["-include", "port/win98/include/win9x_crt.h"]
        units.append((str(source.relative_to(ROOT)), flags, False))
    for source in sorted((windows / "src").glob("*.c")):
        units.append((str(source.relative_to(ROOT)), win32 if source.name.startswith("win32_") else platform, False))
    # what Windows 9x lacks (port/win98/include/win9x_compat.h), and the
    # x87's control word under hybrid precision
    units.append(("port/win98/src/win9x_compat.c", win32, False))
    # what Windows 9x has only on NT: the wide-character functions, security
    units.append(("port/win98/src/win9x_unicode.c", win32, False))
    units.append(("port/win98/src/win9x_precision.c", win32, False))
    # the C runtime under the Linux build's names, which the game's units
    # call; what Windows 98's msvcrt.dll and ws2_32.dll lack
    units.append(("port/win98/src/win9x_linux_crt.c", win32, False))
    units.append(("port/win98/src/win9x_crt.c", win32, False))
    units.append(("port/win98/src/win9x_net.c", win32, False))
    # SDL 3's functions the platform layer calls (port/retro_sdl)
    retro_sdl = abi + ["-std=gnu11", "-D_WIN32_WINNT=0x0410", "-include", "port/win98/include/win9x_compat.h",
                       f"-I{sdl}", "-Iport/retro_sdl/include", "-Wall", "-Wno-unused-function"] + system["all"]
    for name in ("common.c", "win32.c"):
        units.append((f"port/retro_sdl/src/{name}", retro_sdl, False))
    # the assets the Linux build embeds (tools/embed_assets.py, tools/server_ui_embed.py)
    generated = ["build/linux/generated/hud_hires_assets.c", "build/linux/generated/server_ui_assets.c",
                 "build/linux/platform_msvc_semantics.h"]
    subprocess.run(["ninja", "-C", str(ROOT), *generated], check=True, capture_output=True)
    for source in generated[:2]:
        units.append((source, platform, False))
    return units


def library_units(system):
    abi = TARGET + ["-g0", "-O2", "-std=gnu11", "-w"]
    units = []
    units.append((str(TOML_DIR / "tomlc17.c"), abi + system["crt"], False))
    for name in EXPAT_SOURCES:
        units.append((str(EXPAT_DIR / name), abi + [f"-I{EXPAT_DIR}"] + system["all"], False))
    units.append((str(KCP_DIR / "ikcp.c"), abi + system["crt"], False))
    for name in ("monocypher.c", "monocypher-ed25519.c"):
        units.append((str(MONOCYPHER_DIR / name), abi + system["crt"], False))
    for name in ZLIB_SOURCES:
        units.append((str(ZLIB_DIR / name), abi + list(ZLIB_DEFINES) + system["crt"], False))
    for source in sorted((MBEDTLS_DIR / "library").glob("*.c")):
        units.append((str(source), abi + [f"-I{MBEDTLS_DIR / 'include'}", f"-I{MBEDTLS_DIR / 'library'}",
                                          "-D_WIN32_WINNT=0x0410"] + system["all"], False))
    for source in opus_sources():
        units.append((str(source), abi + list(OPUS_DEFINES) +
                      [f"-I{OPUS_DIR / name}" for name in ("include", "celt", "silk", "silk/float")] + system["crt"],
                      False))
    for source in miniupnpc_sources():
        units.append((str(source), abi + MINIUPNPC_DEFINES + [f"-I{MINIUPNPC_DIR / 'include'}",
                                                              f"-I{MINIUPNPC_DIR / 'src'}", "-D_WIN32_WINNT=0x0410"] +
                      system["all"], False))
    # the game's sin, pow and the rest: in double precision under hybrid
    # precision (port/win98/precision), so with FLT_EVAL_METHOD 0
    for source in musl_math_sources():
        units.append((str(source), abi + ["-ffp-eval-method=source", f"-I{MUSL_MATH_DIR}/include",
                                          "-include", f"{MUSL_MATH_DIR}/include/libm.h"] + system["crt"], False))
    # hybrid precision's helpers
    units.append((str(HERE / "precision" / "x87_helpers.S"), TARGET, False))
    return units


def runtime_units(system, builtins: Path):
    """the build's own start-up, thread-local storage and stack probe
    (port/win98/runtime), and the compiler's 64-bit division"""
    flags = TARGET + ["-g0", "-O2", "-std=gnu11", "-D_WIN32_WINNT=0x0410", "-DWIN32_LEAN_AND_MEAN"] + system["all"]
    units = [(f"port/win98/runtime/{name}", flags, False) for name in ("win9x_start.c", "win9x_tls.c", "win9x_watch.c")]
    units.append(("port/win98/runtime/win9x_chkstk.S", TARGET, False))
    for name in COMPILER_RT_BUILTINS:
        units.append((str(builtins / name), TARGET + ["-g0", "-O2", "-w"] + system["crt"], False))
    return units


# ---------- compiling

def object_path(source: str) -> Path:
    if os.path.isabs(source):
        source = "external/" + Path(source).name
    name = source.replace("/", "__").replace(" ", "_")
    return OBJ / (name.rsplit(".", 1)[0] + ".o")


def compile_unit(job):
    source, flags, hybrid, syntax_only = job
    run = lambda a: subprocess.run(a, cwd=ROOT, capture_output=True, text=True)
    if syntax_only:
        if source.endswith(".S"):
            return source, "ok", ""
        r = run(["clang", *flags, "-fsyntax-only", source])
        return source, "ok" if r.returncode == 0 else "error", r.stderr
    obj = object_path(source)
    if obj.is_file() and obj.stat().st_mtime >= (ROOT / source).stat().st_mtime:
        return source, "ok", ""
    if not hybrid:
        r = run(["clang", *flags, "-O2", "-c", source, "-o", str(obj)])
        return source, "ok" if r.returncode == 0 else "error", r.stderr
    ir, rewritten = obj.with_suffix(".ll"), obj.with_suffix(".x87.ll")
    r = run(["clang", *flags, "-S", "-emit-llvm", source, "-o", str(ir)])
    if r.returncode:
        return source, "error", r.stderr
    r = run([sys.executable, str(HERE / "precision" / "rewrite.py"), str(ir), str(rewritten)])
    if r.returncode:
        return source, "error", r.stderr
    # (the IR is optimised already: only code generation, which must not
    # move arithmetic across the precision helpers' calls)
    r = run(["clang", *TARGET, "-O2", "-Xclang", "-disable-llvm-optzns", "-c", str(rewritten), "-o", str(obj)])
    ir.unlink(missing_ok=True)
    rewritten.unlink(missing_ok=True)
    return source, "ok" if r.returncode == 0 else "error", r.stderr


def undefined_symbols(objects):
    """the symbols the objects need that none of them defines, by name"""
    defined, needed = set(), collections.defaultdict(set)
    for obj in objects:
        out = subprocess.run(["llvm-nm", "--no-sort", str(obj)], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[0] == "U":
                needed[parts[1]].add(obj.name)
            elif len(parts) == 3 and parts[1] not in ("U", "u", "w", "v"):
                defined.add(parts[2])
            elif len(parts) == 3 and parts[1] in ("w", "v"):
                defined.add(parts[2])
    return {name: users for name, users in needed.items() if name not in defined}


# ---------- linking

def defined_symbols(objects):
    """the names (undecorated) the objects define"""
    names = set()
    for obj in objects:
        out = subprocess.run(["llvm-nm", "--defined-only", "--no-sort", str(obj)], capture_output=True,
                             text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[1] in "TtDdBbRr":
                # (C's names have one leading underscore on 32-bit Windows)
                name = parts[2][1:] if parts[2].startswith("_") else parts[2]
                names.add(re.sub(r"@\d+$", "", name))
    return names


def import_libraries(mingw: Path, defined):
    """an import library for each DLL, from mingw-w64's definitions, less
    what the program defines itself (port/win98/src/win9x_compat.c: what
    Windows 9x lacks), so that the linker takes those"""
    lib = BUILD / "lib"
    lib.mkdir(parents=True, exist_ok=True)
    crt = mingw / "mingw-w64-crt"
    for name in DLLS:
        definition = crt / "lib32" / f"{name}.def"
        if name == "msvcrt":
            text = subprocess.run(["clang", "-E", "-P", "-x", "c", "-DDEF_I386", f"-I{crt / 'def-include'}",
                                   str(crt / "lib-common" / "msvcrt.def.in")], capture_output=True, text=True,
                                  check=True).stdout
        elif definition.is_file():
            text = definition.read_text()
        elif (crt / "lib-common" / f"{name}.def").is_file():
            text = (crt / "lib-common" / f"{name}.def").read_text()
        else:
            continue
        kept = []
        for line in text.splitlines():
            symbol = line.strip().split()[0] if line.strip() else ""
            bare = re.sub(r"@\d+$", "", symbol)
            if symbol and not symbol.startswith((";", "LIBRARY", "EXPORTS")) and bare in defined:
                continue
            kept.append(line)
        (lib / f"{name}.def").write_text("\n".join(kept) + "\n")
        subprocess.run(["llvm-dlltool", "-m", "i386", "-k", "-d", str(lib / f"{name}.def"),
                        "-l", str(lib / f"lib{name}.a")], check=True)
    return lib


def link(objects, lib: Path):
    """halo.exe for Windows 4.0 (95, 98, Me; NT 4), without a console (the
    log goes to halo.log, runtime/win9x_start.c), and its map (halo.map,
    for tools/symbolize.py); the linker's undefined symbols listed"""
    output = BUILD / "halo.exe"
    response = BUILD / "link.rsp"
    response.write_text("\n".join(str(obj) for obj in objects) + "\n")
    command = ["clang", *TARGET, "-fuse-ld=lld", "-nostdlib", "-o", str(output), f"@{response}", f"-L{lib}",
               *[f"-l{name}" for name in DLLS],
               "-Wl,--entry,_mainCRTStartup", "-Wl,--subsystem,windows:4.0", f"-Wl,-Map={BUILD / 'halo.map'}",
               "-Wl,--major-os-version,4", "-Wl,--minor-os-version,0",
               "-Wl,--disable-dynamicbase", "-Wl,--disable-nxcompat", "-Wl,--build-id=none",
               "-Wl,--stack,0x800000", "-Wl,--error-limit=0"]
    result = subprocess.run(command, capture_output=True, text=True)
    undefined = sorted(set(re.findall(r"undefined symbol: (?:__declspec\(dllimport\) )?(\S+)", result.stderr)))
    with open(BUILD / "undefined.txt", "w") as out:
        out.write("\n".join(undefined) + ("\n" if undefined else ""))
    if result.returncode:
        other = [line for line in result.stderr.splitlines()
                 if "error:" in line and "undefined symbol" not in line]
        print(f"link: {len(undefined)} symbols undefined (build/win98/undefined.txt)")
        for line in other[:10]:
            print(line)
        return False
    print(f"linked {output}")
    return True


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--syntax-only", action="store_true")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--jobs", type=int, default=os.cpu_count())
    args = parser.parse_args()

    mingw = mingw_source()
    crt, windows_include = mingw_headers(mingw)
    sdl = sdl_include()
    resources = resource_dir()
    # (msvcrt.dll's printf and strtod, not mingw's own: there is no mingw
    # runtime library in this build)
    base = ["-nostdinc", "-isystem", f"{resources}/include", "-isystem", str(BUILD / "include"), "-isystem", str(crt),
            "-D__USE_MINGW_ANSI_STDIO=0"]
    system = {"crt": base, "all": base + ["-isystem", str(windows_include)]}
    OBJ.mkdir(parents=True, exist_ok=True)
    if args.clean:
        for obj in OBJ.glob("*.o"):
            obj.unlink()

    units = (game_units(system) + platform_units(system, sdl) + library_units(system) +
             runtime_units(system, compiler_rt_builtins()))
    jobs = [(source, flags, hybrid, args.syntax_only) for source, flags, hybrid in units]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(compile_unit, jobs))
    failed = [r for r in results if r[1] != "ok"]
    print(f"{len(results) - len(failed)} of {len(results)} units compiled")
    for source, _, text in failed[:8]:
        errors = [line for line in text.splitlines() if "error:" in line]
        print(f"--- {source}\n" + "\n".join(errors[:6]))
    if failed or args.syntax_only:
        sys.exit(1 if failed else 0)

    objects = [object_path(source) for source, _, _ in units]
    lib = import_libraries(mingw, defined_symbols(objects))
    sys.exit(0 if link(objects, lib) else 1)


if __name__ == "__main__":
    main()
