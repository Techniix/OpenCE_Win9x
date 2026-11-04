/*
RETRO_SDL_BACKEND.H

What port/retro_sdl/src/common.c needs of a backend (src/win32.c; Mac OS 9
and DOS to come), and what it gives them.
*/

#ifndef __RETRO_SDL_BACKEND_H
#define __RETRO_SDL_BACKEND_H

#include <SDL3/SDL.h>

/* ---------- hints of retro_sdl's own (SDL_SetHint) */

/* the depth and stencil bits an OpenGL window has at least, whatever
SDL_GL_SetAttribute asked */
#define RETRO_SDL_HINT_GL_MINIMUM_DEPTH "RETRO_SDL_GL_MINIMUM_DEPTH"
#define RETRO_SDL_HINT_GL_MINIMUM_STENCIL "RETRO_SDL_GL_MINIMUM_STENCIL"
/* "640x480": the window's OpenGL drawable that size and no other, read when
the context is made: no resizing or maximizing, and fullscreen the display
changed to that mode (the Voodoo3's ICD loses its context's extensions when
the window grows) */
#define RETRO_SDL_HINT_GL_FIXED_SIZE "RETRO_SDL_GL_FIXED_SIZE"

/* ---------- a backend's */

/* each name in a folder (not "." or ".."), passed to add; false if the
folder cannot be read */
bool retro_sdl_list_directory(const char *path, void (*add)(void *context, const char *name), void *context);

/* the display's gamma ramp (red, green, blue: 256 entries each, 0 to 65535),
which render.gamma sets (port/retro_gl/src/retro_gamma.c); the display's own is
put back as the program ends. False where the display or the system does not
take one (Windows 98 has the call; a card in fullscreen may ignore it) */
bool retro_sdl_set_gamma_ramp(const unsigned short ramp[3][256]);

/* ---------- common.c's */

/* a hint's value (SDL_SetHint), or NULL */
const char *retro_sdl_hint(const char *name);
bool retro_sdl_set_pointer_property(SDL_PropertiesID properties, const char *name, void *value);

/* (the program's, if it defines it: run first in each thread the backend
starts, as the Windows 9x build puts each thread's x87 in single
precision there, port/win98/src/win9x_precision.c) */
void retro_sdl_thread_started(void) __attribute__((weak));

#endif
