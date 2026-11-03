/*
RETRO_SDL_BACKEND.H

What port/retro_sdl/src/common.c needs of a backend (src/win32.c; Mac OS 9
and DOS to come), and what it gives them.
*/

#ifndef __RETRO_SDL_BACKEND_H
#define __RETRO_SDL_BACKEND_H

#include <SDL3/SDL.h>

/* ---------- a backend's */

/* each name in a folder (not "." or ".."), passed to add; false if the
folder cannot be read */
bool retro_sdl_list_directory(const char *path, void (*add)(void *context, const char *name), void *context);

/* ---------- common.c's */

/* a hint's value (SDL_SetHint), or NULL */
const char *retro_sdl_hint(const char *name);
bool retro_sdl_set_pointer_property(SDL_PropertiesID properties, const char *name, void *value);

/* (the program's, if it defines it: run first in each thread the backend
starts, as the Windows 9x build puts each thread's x87 in single
precision there, port/win98/src/win9x_precision.c) */
void retro_sdl_thread_started(void) __attribute__((weak));

#endif
