# retro_sdl

The SDL 3 functions OpenCE's platform layer (port/linux/src) calls, for
systems SDL 3 does not run on: Windows 95, 98 and Me now, and later Mac OS 9
and DOS. It is built against SDL 3's own headers (3.4.16), so the platform
layer compiles unchanged; it is a subset, the 146 functions the platform
layer calls, not a port of SDL.

| File | What |
| --- | --- |
| `src/common.c` | What any system can share: errors, hints, properties, atomics, I/O streams on stdio, whole files, 32-bit surfaces, dates, directory globbing, key and scancode names. |
| `src/win32.c` | Windows (ANSI functions only, so Windows 95 and later): time, threads and locks, files, displays and fullscreen modes, the window and its events, the mouse, OpenGL through WGL, the clipboard, message boxes, the file dialog, sound through waveOut. |
| `include/retro_sdl_backend.h` | What `common.c` needs of a backend, and gives it. |

A backend defines `retro_sdl_list_directory` and the system's functions.
A program may define `retro_sdl_thread_started`, which each thread the
backend starts runs first (the Windows 9x build sets the x87's precision
there, port/win98/src/win9x_precision.c).

What the Win32 backend does without, as SDL does on a machine without the
driver: gamepads (none are listed yet), recording (no microphone), the 2D
renderer (the updater's window), PNG pictures (screenshots), colour
pointers, the window's icon (the executable's is used). Only one window.
Sound is 16-bit through waveOut, at the stream's rate if the device takes
it, else 44.1, 22.05 or 11.025 kHz, resampled linearly, and down-mixed to
stereo or mono. Fullscreen changes the display's mode, as Windows 9x
programs did.

`test/retro_sdl_test.c` checks a backend on the machine itself, apart from
the game: a window with an OpenGL context (and the driver's version and
extensions), its keyboard and mouse, fullscreen, the pointer's relative
mode, a thread and a condition, and a tone. The Windows 9x build makes it
as build/win98/retro_sdl_test.exe.
