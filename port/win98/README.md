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
| Compiling and linking (`build.py`, `include/`, `src/`, `runtime/`) | All 900 units compile for i686-w64-windows-gnu (i586, no CMOV, no SSE) on mingw-w64's headers, and link into build/win98/halo.exe: the game through hybrid precision, the Windows build's platform layer, the third-party libraries, and what Windows 9x lacks (below). Runs on Windows 98 SE (86Box: VIA Cyrix III, 128 MB) without drawing: loads ui.map and the main menu, plays its music to no device, and quits on time (first runs, below). |
| SDL 3 | `port/retro_sdl`: the SDL 3 functions the platform layer calls, on Win32 (Windows 95 and later). Its time, threads, files and events run; its window, OpenGL and sound not yet. |
| Setup window | `launcher/`: sliders and toggles (user32 only) before the game opens; below. Its choices are tested apart from the window (`launcher/test/launcher_test.py`); the window has been seen opening on Windows 98 SE, but its layout has not been checked. |
| OpenGL 1.1 renderer | `port/retro_gl`: Direct3D 8 on OpenGL 1.1 in place of the OpenGL 4.5 renderer (its README). First version, not yet run: the vertex programs on the processor, textures fitted to the card, the pixel shaders approximate. |

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

## The setup window

`launcher/win9x_launcher.c` shows a window before the game starts, in the
style of the setup programs of the time: a blue banner, group boxes (Picture,
Detail, Sound), sliders and toggles, and a line of help at the bottom for the
control under the pointer or with the focus. It is 616 by about 440 pixels, to
fit a 640 by 480 screen.

- Picture: display mode, picture size, frame limit, brightness (the display's
  gamma ramp, `render.gamma`), V-Sync, smooth motion.
- Detail: object detail, extras drawn, textures (`render.texture_detail`),
  Keep up (`render.adaptive_fps`: lowers the detail by itself to hold a frame
  rate), stored room sets (PVS).
- Sound: quality (a stop of `audio.sample_rate`, `audio.resampler`,
  `audio.sinc_voices` and `audio.buffer_frames` together), sound, room reverb.

It uses only user32's classes (the trackbar is comctl32's, which an early
Windows 95 lacks, so the sliders are scroll bars). "Detect this PC" times the
processor (`rdtsc` over 60 ms) and reads the memory, and suggests one of four
presets, Fastest, Fast, Balanced, Best, marking it "(suggested)" in the list.
The graphics card is not tested. "Benchmark..." plays the main menu for 30
seconds with the chosen settings (`debug.exit_after`, `debug.frame_budget`,
`launcher.benchmark`), and as the program ends says how fast it ran and which
preset suits (`launcher_advice`); the next start takes those settings back and
shows the window. The main menu's scene is lighter than a game's: it is a
guide, not a measure of a map in play.

It comes up:

- on the first run (`launcher.configured` false);
- when Shift is held as `halo.exe` starts, or with `--setup`: `SETUP.BAT`
  (`tools/SETUP.BAT`) is that, for a shortcut;
- when `launcher.show_next_start` or `launcher.always` is true;
- after a run that did not end normally: a run leaves `halo_running.flag`
  beside `halo.exe` and removes it on exit, so a crash or a hang leaves it,
  and the banner says so. This is how a setting that stops the game from
  starting can be taken back.

`HALO_NO_LAUNCHER` and `--no-setup` skip it, as does a dedicated server.
Its sliders write the same settings, in the same steps, as Video Setup,
Performance and Audio Setup, Sound Processing in the game
(`tools/port_settings.py`); `launcher_test.py` checks that the lists of
passes agree. `BUDGET.BAT` runs the game with `debug.frame_budget`, which
writes where each frame's time went to `HALO.log`.

None of the window has run on a Windows 9x machine except through the
emulated one's first runs: its layout, the help line, the banner, the
benchmark, the gamma ramp and the colour pointer are compile-checked only.

## Detail while playing

In a game, Ctrl+Minus and Ctrl+Equals step the detail level down and up by
hand (`render.hotkeys`), and with Keep up on the level follows the frame rate
by itself (`port/linux/game/render_policy.c`). Each change says so at the
top left of the screen for a moment ("Detail: level 3 of 6",
`port/linux/game/detail_notice.c`). The governor does not lower the detail
when the rate is under an eighth of the one asked for: that is a map
loading, or a machine too slow for the game itself, which no level helps.

## Pointer and icon

The menus' pointer is the port's own picture, 32 by 32 at most, drawn or not
by each pixel's alpha (Windows 95 and 98 have no alpha pointers;
`SDL_CreateColorCursor` in `port/retro_sdl/src/win32.c`). Its PNG is embedded
with stored (uncompressed) blocks (`tools/embed_assets.py --menus-only`) so
that `SDL_LoadPNG_IO` (`port/retro_sdl/src/common.c`) reads it with no
inflate. The window's icon is `halo.exe`'s own and is not embedded.

## Sound

`audio.sample_rate` makes the software mixer (`port/linux/src/dsound_sdl.c`)
run at a lower rate: the cost of the mix is proportional to it. The resampler
`"auto"` gives the sinc low pass (most of a voice's cost) to the loudest
`audio.sinc_voices` voices only. The voice chat's 48 kHz voices are resampled
to the mix's rate (`voice_audio_mix_at`). The defaults (48000, `"sinc"`) mix as
before.

## Building

    python3 configure.py --window-base 0x20000000   # the game's flags, its window moved
    python3 port/win98/build.py                      # build/win98/obj, build/win98/undefined.txt

The game's units are compiled as the Linux build's (`halo_win9x_prefix.h`),
the platform layer as the Windows build's (`halo_win9x_platform_prefix.h`),
both on mingw-w64's headers, which `build.py` clones into
build/win98/third_party with SDL 3's headers the first time.
What Windows 98 lacks that the platform layer calls is defined under the
same names in `src/win9x_compat.c` (condition variables on a semaphore,
`rand_s` on CryptoAPI, `localtime_r`), so the shared sources stay as they
are; `src/win9x_precision.c` keeps the x87 in single precision when the game
resets it; `src/win9x_linux_crt.c` gives the game's units the C runtime under
the Linux build's names.

## What Windows 9x lacks

`build.py` links build/win98/halo.exe with the build's own runtime
(`runtime/`: start-up, static constructors, thread-local storage, the stack
probe; compiler-rt's 64-bit division) and import libraries made from
mingw-w64's definitions, less every function the build defines itself, so
that the linker takes those. What Windows 98 lacks is in `src/`:

| File | What |
| --- | --- |
| `win9x_unicode.c` | What Windows 9x has in name only, or not at all, and NT has: the wide-character functions the platform layer calls (on the ANSI ones, names in the system's code page), NT's security (nothing to do on a FAT disk: a token of one user, access lists that change nothing), reads and writes at an offset (a seek and a plain transfer: Windows 9x has no overlapped I/O on disk files), `SwitchToThread`, `TryEnterCriticalSection`, `GetProcessTimes`, and iphlpapi.dll's functions (looked up, as Windows 95 has none). On Windows NT each calls the system's own. |
| `win9x_compat.c` | Condition variables (on a semaphore), vectored exception handlers (on `SetUnhandledExceptionFilter`, for the texture watch), `GetFileSizeEx`, `SetFilePointerEx`, `GlobalMemoryStatusEx`, `BCryptGenRandom` and `rand_s` (CryptoAPI), the newer C runtime's `*_s` functions and stdio, and failing stubs for what Windows 9x does without: WinHTTP (the updater and crash uploads), DbgHelp's minidumps, the firmware table, `AttachConsole`. |
| `win9x_crt.c` | C99's float maths and 64-bit conversions, and what mingw-w64's headers expect its runtime library to define. |
| `win9x_net.c` | `getaddrinfo`, `freeaddrinfo`, `getnameinfo` on `gethostbyname` (IPv4); `WSASocketW` on `socket`. |
| `win9x_precision.c` | `_control87`, which keeps the x87 in single precision, and the same for each thread port/retro_sdl starts. |
| `win9x_linux_crt.c` | The C runtime under the Linux build's names, for the game's units. |

SDL 3's functions are port/retro_sdl's (its README). The program imports
309 functions from msvcrt, kernel32, user32, gdi32, advapi32, ws2_32,
winmm, shell32, comdlg32 and opengl32, all of which Windows 98 should have;
`probe/import_check.exe` says which a machine lacks.

## First runs

On Linux (WSL):

    python3 port/win98/build.py
    MINGW_SOURCE=build/win98/third_party/mingw-w64 OUT=build/win98/probe port/win98/probe/build.sh
    python3 port/win98/tools/make_iso.py build/win98/opence.iso build/win98/halo.exe \
        build/win98/probe/import_check.exe port/win98/tools/first_boot.toml /mnt/c/Games/OpenCE/maps

make_iso.py writes a CD image (with long names) for the emulator's CD
drive. On Windows 98, copy its files into a folder on the disk, rename
first_boot.toml to config.toml and clear its read-only attribute
(`attrib -r config.toml`), then:

1. Run import_check.exe: it lists in import_check.txt whatever halo.exe
   imports that this Windows lacks (none, if all is well).
2. Run halo.exe: first_boot.toml runs it without drawing or sound, and
   quits after a minute. halo.log beside it is the game's log (there is no
   console). halo_trace.txt is the start-up's: its stages, and every five
   seconds where the main thread is (`runtime/win9x_watch.c`), which

       python3 port/win98/tools/symbolize.py halo_trace.txt

   names from build/win98/halo.map, the same build's.
3. Run retro_sdl_test.exe (port/retro_sdl/test, built with halo.exe): a
   window, its keyboard and mouse, fullscreen, OpenGL and a tone, through
   the same SDL layer as the game; retro_sdl_test.log beside it has what it
   found, the OpenGL driver's version and extensions among it.
4. With sound_test.toml as config.toml, halo.exe plays the main menu's
   music, still without drawing (heard on 86Box, with stutters at a third
   of the machine's speed; the mixer's reverb and sinc resampling off there
   leave it more time). Windows 9x's sound drivers take one program at a
   time: another playing leaves the game silent, as halo.log says.
5. With draw_test.toml as config.toml, halo.exe draws (port/retro_gl) in a
   640x480 window, with sound; halo.log lists the OpenGL driver and each
   pixel shader the game uses.
