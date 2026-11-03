# Windows 9x (work in progress)

A build of OpenCE for Windows 95, 98 and Me on the hardware of their time:
a Pentium II or later without SSE, and a fixed-function OpenGL 1.1 card
(Voodoo 3, TNT, Rage 128, GeForce 2). The fixed-function renderer is meant to
be shared with later Mac OS 9 and DOS (Mesa on Glide) builds.

## Status

| Part | State |
| --- | --- |
| `probe/` | Done: a small program that reports what a Windows 9x machine offers (memory window, CPU, OpenGL, system libraries). Not yet run on Windows 98. |
| `precision/` | Prototype works: x87 maths that match the SSE2 builds bit for bit, for network play with them (below). |
| Memory window at 0x80000000 | Waiting on the probe's results. |
| Toolchain (i686, x87, msvcrt.dll) | Not started. |
| Fixed-function OpenGL 1.1 renderer | Not started. |
| Win32 replacement for SDL3 | Not started. |

## Maths: hybrid precision

Every networked build computes with SSE2 (or ARM, or WebAssembly), which
round each single-precision operation to single precision. The x87 FPU of a
processor without SSE2 computes in extended precision, so a plain x87 build
gets different results and cannot play with them. In a test of 20 of the
game's maths functions, a plain x87 build matched SSE2 in 2.

The game's simulation is almost all single precision: 24,759 `float`
operations against 388 `double` ones, which are in 91 functions, and 447
calls of the maths library (measured on the LLVM IR of `source/` and
`port/linux/game`). So the Win9x build:

1. runs the x87 in **single precision** (control word 0x007F), so each
   `float` operation rounds as SSE does;
2. rewrites each `double` operation, `sqrt` and maths-library call, in the
   LLVM IR (`precision/rewrite.py`), into a call of `precision/x87_helpers.S`,
   which switches to double precision for that operation only;
3. rounds integer-to-`float` conversions to single at once (`fild` is exact,
   SSE's `cvtsi2ss` rounds);
4. compiles the maths library (musl) with `-ffp-eval-method=source`: it runs
   in double precision, and `__rem_pio2` must not take its x87
   extended-precision path.

With all four, the 20 functions match SSE2 bit for bit over 300,000 random
inputs each (`precision/test`). It costs about 70% over a plain x87 build in
that test, which is mostly trigonometry; inlining the helpers will reduce it.

Remaining risks: values held in x87 registers keep the extended exponent
range, so results that overflow or become denormal in single precision can
still differ (the test's inputs do not reach that range); the thread start
code and the game's `_control87` calls must keep the control word at 0x007F.
`tools/net_compare.py` with real maps is the full test.
