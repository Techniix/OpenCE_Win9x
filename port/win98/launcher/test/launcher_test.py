#!/usr/bin/env python3
"""Builds and runs launcher_test.c (the setup window's choices, apart from its
window) and checks that its lists of passes are Video Setup, Performance's.

    python3 port/win98/launcher/test/launcher_test.py
"""

import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import port_settings  # noqa: E402


def main():
    with tempfile.TemporaryDirectory() as folder:
        exe = Path(folder) / "launcher_test"
        subprocess.run(["clang", "-std=gnu99", "-Wall", "-Werror", "-O1", str(HERE / "launcher_test.c"),
                        str(HERE.parent / "win9x_launcher_logic.c"), "-o", str(exe)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
    print(result.stdout, end="")
    if result.returncode:
        return 1
    menu = {0: "", 1: port_settings._SKIP_SHINY, 2: port_settings._SKIP_SOME, 3: port_settings._SKIP_MOST}
    for line in result.stdout.splitlines():
        if line.startswith("extras "):
            _, index, *rest = line.split(" ")
            if " ".join(rest) != menu[int(index)]:
                print(f"FAIL: extras step {index} differs from the menu's")
                return 1
    print("the lists agree with the menu's")
    return 0


if __name__ == "__main__":
    sys.exit(main())
