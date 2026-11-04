/*
RETRO_GL.C

OpenGL 1.1 and its extensions looked up (retro_gl.h).
*/

#include "retro_internal.h"

#include <SDL3/SDL.h>
#include <string.h>

union retro_gl_page retro_gl_page __attribute__((aligned(4096)));

/* the table's page read-only */
static void retro_gl_protect(void)
{
#ifdef _WIN32
	/* kernel32's own: VirtualProtect is the Xbox one in the game's names
	(halo_windows_api_names.h) */
	extern int (__stdcall *const retro_gl_virtual_protect)(void *, unsigned long, unsigned long,
		unsigned long *) __asm__("__imp__VirtualProtect@16");
	unsigned long previous;

	/* (PAGE_READONLY) */
	if (!retro_gl_virtual_protect(&retro_gl_page, sizeof(retro_gl_page), 0x02, &previous))
		platform_log("OpenGL: the function table cannot be made read-only");
#endif
}

int retro_gl_has_extension(const char *name)
{
	const char *extensions = RGL.rgl_glGetString ? (const char *)RGL.rgl_glGetString(RGL_EXTENSIONS) : NULL;
	size_t length = strlen(name);

	while (extensions && *extensions)
	{
		const char *end = strchr(extensions, ' ');
		size_t word = end ? (size_t)(end - extensions) : strlen(extensions);

		if (word == length && !memcmp(extensions, name, length))
			return TRUE;
		extensions += word;
		while (*extensions == ' ')
			extensions++;
	}
	return FALSE;
}

int retro_gl_load(void)
{
	int complete = TRUE;
	RGLint value = 0;

#define RETRO_GL_LOAD(type, name, parameters) \
	RGL.rgl_##name = (type (RETRO_GL_API *) parameters)SDL_GL_GetProcAddress(#name); \
	if (!RGL.rgl_##name) \
	{ \
		platform_log("OpenGL: %s is missing", #name); \
		complete = FALSE; \
	}
	RETRO_GL_FUNCTIONS(RETRO_GL_LOAD)
#undef RETRO_GL_LOAD
	if (!complete)
		return FALSE;

	memset(&retro_gl_capabilities, 0, sizeof(retro_gl_capabilities));
	RGL.rgl_glActiveTextureARB = NULL;
	RGL.rgl_glClientActiveTextureARB = NULL;
	retro_gl_capabilities.texture_units = 1;
	if (retro_gl_has_extension("GL_ARB_multitexture"))
	{
#define RETRO_GL_LOAD_OPTIONAL(type, name, parameters) \
		RGL.rgl_##name = (type (RETRO_GL_API *) parameters)SDL_GL_GetProcAddress(#name);
		RETRO_GL_MULTITEXTURE_FUNCTIONS(RETRO_GL_LOAD_OPTIONAL)
#undef RETRO_GL_LOAD_OPTIONAL
		if (RGL.rgl_glActiveTextureARB && RGL.rgl_glClientActiveTextureARB)
		{
			RGL.rgl_glGetIntegerv(RGL_MAX_TEXTURE_UNITS, &value);
			retro_gl_capabilities.texture_units = value < 1 ? 1 : value > 4 ? 4 : value;
		}
	}
	value = 0;
	RGL.rgl_glGetIntegerv(RGL_MAX_TEXTURE_SIZE, &value);
	retro_gl_capabilities.maximum_texture_size = value >= 64 ? value : 256;
	/* (2048 at most: more costs memory these cards do not have) */
	if (retro_gl_capabilities.maximum_texture_size > 2048)
		retro_gl_capabilities.maximum_texture_size = 2048;
	retro_gl_capabilities.env_combine = retro_gl_has_extension("GL_ARB_texture_env_combine") ||
		retro_gl_has_extension("GL_EXT_texture_env_combine");
	retro_gl_capabilities.env_add = retro_gl_has_extension("GL_EXT_texture_env_add") ||
		retro_gl_has_extension("GL_ARB_texture_env_add");
	retro_gl_capabilities.edge_clamp = retro_gl_has_extension("GL_SGIS_texture_edge_clamp") ||
		retro_gl_has_extension("GL_EXT_texture_edge_clamp");
	retro_gl_capabilities.stencil_wrap = retro_gl_has_extension("GL_EXT_stencil_wrap");
	RGL.rgl_glGetIntegerv(0x0D57 /* GL_STENCIL_BITS */, &retro_gl_capabilities.stencil_bits);
	RGL.rgl_glGetIntegerv(0x0D56 /* GL_DEPTH_BITS */, &retro_gl_capabilities.depth_bits);

	retro_gl_protect();
	platform_log("OpenGL: %s, %s, version %s", (const char *)RGL.rgl_glGetString(RGL_VENDOR),
		(const char *)RGL.rgl_glGetString(RGL_RENDERER), (const char *)RGL.rgl_glGetString(RGL_VERSION));
	platform_log("OpenGL: %d texture units, textures up to %d, depth %d bits, stencil %d bits%s%s",
		retro_gl_capabilities.texture_units, retro_gl_capabilities.maximum_texture_size,
		retro_gl_capabilities.depth_bits, retro_gl_capabilities.stencil_bits,
		retro_gl_capabilities.env_combine ? ", combine" : "", retro_gl_capabilities.env_add ? ", add" : "");
	return TRUE;
}
