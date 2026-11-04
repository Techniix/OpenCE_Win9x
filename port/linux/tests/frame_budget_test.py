"""Builds and runs frame_budget_test.c on the host: frame_budget.c on a
clock the test drives (the include of cseries.h, which the test has no need
of, is left out)."""
import pathlib, subprocess, sys, tempfile

HERE = pathlib.Path(__file__).resolve().parent
GAME = HERE.parent / "game"

with tempfile.TemporaryDirectory() as folder:
    source = pathlib.Path(folder) / "frame_budget_x.c"
    source.write_text((GAME / "frame_budget.c").read_text().replace('#include "cseries.h"\n', ""))
    exe = pathlib.Path(folder) / "test"
    subprocess.check_call(["gcc", "-Wall", "-D__stdcall=", f"-I{GAME}", "-o", str(exe),
                           str(HERE / "frame_budget_test.c"), str(source)])
    result = subprocess.run([str(exe)])
    sys.exit(result.returncode)
