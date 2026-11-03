#!/bin/sh
# Builds win98_probe.exe and import_check.exe with clang and lld from Linux, without a C runtime.
#
# Needs: clang, lld, llvm-dlltool, and mingw-w64's headers and .def files
# (a sparse clone of https://github.com/mirror/mingw-w64 with
# mingw-w64-headers and mingw-w64-crt/lib32), named by MINGW_SOURCE.
#
#   MINGW_SOURCE=/path/to/mingw-w64 port/win98/probe/build.sh
#
# The executable says it needs Windows 4.0 (95/98/NT 4), not the 6.0 lld
# writes by default, which Windows 98 refuses as "not a valid Windows
# application".
set -eu

here=$(cd "$(dirname "$0")" && pwd)
mingw=${MINGW_SOURCE:?name the mingw-w64 source tree in MINGW_SOURCE}
out=${OUT:-$here/build}
mkdir -p "$out/include"

# mingw's configured header, for Windows 98 (0x0410) and msvcrt.dll
sed -e 's/@DEFAULT_MSVCRT_VERSION@/0x600/' -e 's/@DEFAULT_WIN32_WINNT@/0x0410/' \
	"$mingw/mingw-w64-headers/crt/_mingw.h.in" > "$out/include/_mingw.h"
mkdir -p "$out/include/sdks"
sed -e 's/@DEFAULT_.*@/0/' "$mingw/mingw-w64-headers/crt/sdks/_mingw_ddk.h.in" > "$out/include/sdks/_mingw_ddk.h"

for library in kernel32 user32 gdi32; do
	llvm-dlltool -m i386 -k -d "$mingw/mingw-w64-crt/lib32/$library.def" -l "$out/lib$library.a"
done

clang --target=i686-w64-windows-gnu -march=pentium -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -Wno-pragma-pack -Wno-ignored-attributes -fno-asynchronous-unwind-tables -mno-stack-arg-probe \
	-I "$out/include" -I "$mingw/mingw-w64-headers/include" -I "$mingw/mingw-w64-headers/crt" \
	-c "$here/win98_probe.c" -o "$out/win98_probe.o"

clang --target=i686-w64-windows-gnu -fuse-ld=lld -nostdlib \
	-Wl,--entry,_probe_entry@0 -Wl,--subsystem,windows:4.0 \
	-Wl,--major-os-version,4 -Wl,--minor-os-version,0 \
	-Wl,--build-id=none -Wl,--disable-dynamicbase -Wl,--disable-nxcompat \
	"$out/win98_probe.o" -L "$out" -lkernel32 -luser32 -lgdi32 \
	-o "$out/win98_probe.exe"

echo "built $out/win98_probe.exe"

# import_check.exe: which of halo.exe's imports this Windows lacks
clang --target=i686-w64-windows-gnu -march=pentium -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -Wno-pragma-pack -Wno-ignored-attributes -fno-asynchronous-unwind-tables -mno-stack-arg-probe \
	-I "$out/include" -I "$mingw/mingw-w64-headers/include" -I "$mingw/mingw-w64-headers/crt" \
	-c "$here/import_check.c" -o "$out/import_check.o"

clang --target=i686-w64-windows-gnu -fuse-ld=lld -nostdlib \
	-Wl,--entry,_check_entry@0 -Wl,--subsystem,windows:4.0 \
	-Wl,--major-os-version,4 -Wl,--minor-os-version,0 \
	-Wl,--build-id=none -Wl,--disable-dynamicbase -Wl,--disable-nxcompat \
	"$out/import_check.o" -L "$out" -lkernel32 -luser32 \
	-o "$out/import_check.exe"

echo "built $out/import_check.exe"
