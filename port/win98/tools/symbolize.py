#!/usr/bin/env python3
"""Names the addresses in halo_trace.txt (port/win98/runtime/win9x_watch.c)
from the linker's map of the same build (build/win98/halo.map):

    python3 port/win98/tools/symbolize.py halo_trace.txt [build/win98/halo.map]

Each address becomes function+offset (the object it came from in brackets).
The stack's words are those that point into the code: return addresses, and
some that only look like them (function pointers left on the stack).
"""

import bisect
import re
import sys
from pathlib import Path


def read_map(path: Path):
    symbols = []
    current_object = ""
    for line in path.read_text(errors="replace").splitlines():
        match = re.match(r"^([0-9a-f]{8}) ([0-9a-f]{8}) +(\d+) +(.*)$", line)
        if not match:
            continue
        address, size, _, rest = match.groups()
        name = rest.strip()
        if not rest.startswith(" ") and "(" in name and name.endswith(")"):
            current_object = Path(name.split(":(")[0]).name
            continue
        if size == "00000000" and name and not name.startswith((".", "/")) and ":(" not in name:
            symbols.append((int(address, 16), name, current_object))
    symbols.sort()
    return symbols


def name(symbols, keys, rva):
    index = bisect.bisect_right(keys, rva) - 1
    if index < 0:
        return "?"
    address, symbol, obj = symbols[index]
    return f"{symbol}+0x{rva - address:x} [{obj}]"


def main():
    trace = Path(sys.argv[1])
    map_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parents[3] / "build/win98/halo.map"
    symbols = read_map(map_path)
    keys = [address for address, _, _ in symbols]
    base = 0x400000
    code_end = None
    for line in trace.read_text(errors="replace").splitlines():
        match = re.match(r"halo\.exe at ([0-9a-f]{8}), code [0-9a-f]{8} to ([0-9a-f]{8})", line)
        if match:
            base = int(match.group(1), 16)
            code_end = int(match.group(2), 16)
        match = re.search(r"watch: eip ([0-9a-f]{8}) esp ([0-9a-f]{8}) stack(.*)$", line)
        if not match:
            print(line)
            continue
        print(line.split("watch:")[0] + "watch:")
        eip = int(match.group(1), 16)
        if eip >= base and (code_end is None or eip < code_end):
            print(f"    at   {name(symbols, keys, eip - base)}")
        else:
            print(f"    at   {eip:08x} (outside halo.exe: a system DLL)")
        for word in match.group(3).split():
            print(f"    from {name(symbols, keys, int(word, 16) - base)}")


if __name__ == "__main__":
    main()
