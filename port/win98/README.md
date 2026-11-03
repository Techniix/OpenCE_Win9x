# Windows 9x (work in progress)

A build of OpenCE for Windows 95, 98 and Me on the hardware of their time:
a Pentium MMX-class processor or later (i586: no CMOV, no SSE, so that the
K6-2, K6-III, Cyrix and VIA processors run it too), and a fixed-function
OpenGL 1.1 card (Voodoo 3, TNT, Rage 128, GeForce 2). The fixed-function renderer is meant to
be shared with later Mac OS 9 and DOS (Mesa on Glide) builds.

## Status

| Part | State |
| --- | --- |
| `probe/` | Done: a small program that reports what a Windows 9x machine offers (memory window, CPU, OpenGL, system libraries). `probe/results/` has its reports. |
| `precision/` | Prototype works: x87 maths that match the SSE2 builds bit for bit, for network play with them (below). |
| Memory window | Cannot be at 0x80000000 (below), so it moves: `configure.py --window-base` puts it elsewhere and the game relocates each map's tags and bsps as they load (`port/linux/game/tag_relocate.c`). Checked on all 24 retail maps (`map_validate --relocation`) and in play against a default build (`net_compare.py`), below. |
| Game units (`build_game.py`, `include/`) | All 511 compile for i686-w64-windows-gnu (i586, no CMOV, no SSE) on mingw-w64's C runtime headers, through hybrid precision. Not linked yet. |
| Platform layer, link against msvcrt.dll | Not started. |
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

## What the first probe found

`probe/results/86box-win98se-cyrix3-voodoo3.txt`: Windows 98 SE in 86Box,
a VIA Cyrix III, 128 MB, a Voodoo 3.

- **The Xbox memory window cannot be at 0x80000000.** On Windows 9x that is
  the shared arena, and the system's own allocations fill its first 38 MB
  (0x80000000-0x825AA000); neither VirtualAlloc nor a file mapping placed with
  MapViewOfFileEx can take it (error 8). The private arena is free (2 GB; the
  Custom Edition window at 0x40440000 reserves). So the window moves into the
  private arena, and a map's tag data, which is linked to 0x803A6000, is
  relocated as it loads. The tag validator (port/linux/game/tag_validate.c)
  already walks every block and data pointer of every tag through the tag
  schemas, so relocation can follow the same walk; it can be developed and
  checked on Linux, against the unrelocated tags.
- **No SSE at all**, as expected: hybrid precision is needed. The build
  targets i586 since the Cyrix III, the VIA C3 and the K6 family lack CMOV.
- **OpenGL 1.1 with two texture units**, `GL_ARB_multitexture` and
  `GL_EXT_texture_env_add` but not `GL_ARB_texture_env_combine`; textures of
  256x256 at most, 16-bit colour and depth, no S3TC but
  `GL_EXT_paletted_texture`. The fixed-function renderer works with
  modulate and add per texture unit and multi-pass blending, and converts
  DXT textures to 16-bit or paletted ones of at most 256x256.
- **Missing system functions** (each to be replaced): `getaddrinfo`,
  `AddVectoredExceptionHandler`, condition variables, `GetTickCount64`,
  WinHTTP (WinINet is present), DbgHelp, BCrypt (CryptoAPI is present),
  XInput, DirectInput 8 (DirectInput is present), `DirectSoundCreate8`
  (`DirectSoundCreate` is present), `_aligned_malloc` in msvcrt.dll. No
  Direct3D 8 or 9.

## Relocation, checked on the retail maps

`tools/map_validate --relocation <maps folder>` reads each Xbox map where it
is linked and again at 0x503A6000 with its tags relocated, validates both, and
compares them. On the 24 maps of the retail disc (the campaign, the
multiplayer maps and ui.map) every map passes: about 790,000 pointers moved
in all, the tags and every structure bsp the same but for them, and no
relocated map needs a correction the linked one does not. That last part is
the strongest evidence: the validator checks every pointer the game follows,
and one left unmoved would point outside the relocated tag cache.

The notes it prints are words in tags' structures that still look like
addresses in the old tag cache, and none is a pointer the game follows:

- pointers to what the schemas leave out because the game never reads it:
  a scenario's editor data, comments, child scenarios and decals, a particle
  system's own physics constants, the globals' playlist;
- the names of tag references, which the game resolves by index;
- data that looks like an address: colours (`hudg` +0xd0), packed values in
  scenarios, animations and structure bsps.

## A moved window in play

A Linux build with `--window-base 0x20000000` and a default build played
`tools/net_compare.py`'s network games against each other on bloodgulch,
beavercreek, sidewinder and damnation, each build hosting in turn: 8 of 8
games in step, positions apart by 0.04-0.06 world units on average and the
mismatched seconds (grenades, rounds) within the netcode's round trip, as
between identical builds (port/linux/NETCODE.md, "Comparing two builds").
