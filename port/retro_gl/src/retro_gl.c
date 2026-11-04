/*
RETRO_GL.C

OpenGL 1.1 and its extensions looked up (retro_gl.h).
*/

#include "retro_internal.h"

#include <SDL3/SDL.h>
#include <string.h>

#define RETRO_GL_DEFINE(type, name, parameters) type (RETRO_GL_API *rgl_##name) parameters;
RETRO_GL_FUNCTIONS(RETRO_GL_DEFINE)
RETRO_GL_MULTITEXTURE_FUNCTIONS(RETRO_GL_DEFINE)
#undef RETRO_GL_DEFINE

struct retro_gl_capabilities retro_gl_capabilities;

int retro_gl_has_extension(const char *name)
{
	const char *extensions = rgl_glGetString ? (const char *)rgl_glGetString(RGL_EXTENSIONS) : NULL;
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
	rgl_##name = (type (RETRO_GL_API *) parameters)SDL_GL_GetProcAddress(#name); \
	if (!rgl_##name) \
	{ \
		platform_log("OpenGL: %s is missing", #name); \
		complete = FALSE; \
	}
	RETRO_GL_FUNCTIONS(RETRO_GL_LOAD)
#undef RETRO_GL_LOAD
	if (!complete)
		return FALSE;

	memset(&retro_gl_capabilities, 0, sizeof(retro_gl_capabilities));
	retro_gl_capabilities.texture_units = 1;
	if (retro_gl_has_extension("GL_ARB_multitexture"))
	{
#define RETRO_GL_LOAD_OPTIONAL(type, name, parameters) \
		rgl_##name = (type (RETRO_GL_API *) parameters)SDL_GL_GetProcAddress(#name);
		RETRO_GL_MULTITEXTURE_FUNCTIONS(RETRO_GL_LOAD_OPTIONAL)
#undef RETRO_GL_LOAD_OPTIONAL
		if (rgl_glActiveTextureARB && rgl_glClientActiveTextureARB)
		{
			rgl_glGetIntegerv(RGL_MAX_TEXTURE_UNITS, &value);
			retro_gl_capabilities.texture_units = value < 1 ? 1 : value > 4 ? 4 : value;
		}
	}
	value = 0;
	rgl_glGetIntegerv(RGL_MAX_TEXTURE_SIZE, &value);
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
	rgl_glGetIntegerv(0x0D57 /* GL_STENCIL_BITS */, &retro_gl_capabilities.stencil_bits);
	rgl_glGetIntegerv(0x0D56 /* GL_DEPTH_BITS */, &retro_gl_capabilities.depth_bits);

	platform_log("OpenGL: %s, %s, version %s", (const char *)rgl_glGetString(RGL_VENDOR),
		(const char *)rgl_glGetString(RGL_RENDERER), (const char *)rgl_glGetString(RGL_VERSION));
	platform_log("OpenGL: %d texture units, textures up to %d, depth %d bits, stencil %d bits%s%s",
		retro_gl_capabilities.texture_units, retro_gl_capabilities.maximum_texture_size,
		retro_gl_capabilities.depth_bits, retro_gl_capabilities.stencil_bits,
		retro_gl_capabilities.env_combine ? ", combine" : "", retro_gl_capabilities.env_add ? ", add" : "");
	return TRUE;
}
