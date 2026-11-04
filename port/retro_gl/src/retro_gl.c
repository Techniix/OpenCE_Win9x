/*
RETRO_GL.C

OpenGL 1.1 and its extensions looked up (retro_gl.h).
*/

#include "retro_internal.h"

#include <SDL3/SDL.h>
#include <string.h>

union retro_gl_page retro_gl_page __attribute__((aligned(4096)));

#ifdef _WIN32
/* kernel32's and opengl32's own: VirtualProtect is the Xbox one in the
game's names (halo_windows_api_names.h) */
extern int (__stdcall *const retro_gl_virtual_protect)(void *, unsigned long, unsigned long,
	unsigned long *) __asm__("__imp__VirtualProtect@16");
extern void *(__stdcall *const retro_gl_current_dc)(void) __asm__("__imp__wglGetCurrentDC@0");
extern void *(__stdcall *const retro_gl_current_context)(void) __asm__("__imp__wglGetCurrentContext@0");
extern int (__stdcall *const retro_gl_make_current)(void *, void *) __asm__("__imp__wglMakeCurrent@8");
#endif

/* the table's page read-only, or writable again for a moment */
static void retro_gl_protect(int read_only)
{
#ifdef _WIN32
	unsigned long previous;

	/* (PAGE_READONLY, PAGE_READWRITE) */
	if (!retro_gl_virtual_protect(&retro_gl_page, sizeof(retro_gl_page), read_only ? 0x02 : 0x04, &previous))
		platform_log("OpenGL: the function table's protection cannot be changed");
#else
	(void)read_only;
#endif
}

/* where an extension's entry goes: the ICDs hand out stubs that jump through
the current context's table (jmp [table]), and the Voodoo3's has been seen to
go to 7 or 11 once its window was resized; 0 when the stub is of some other
kind */
static unsigned long entry_target(const void *function)
{
	const unsigned char *code = (const unsigned char *)function;

	if (!code)
		return 0;
	if (code[0] == 0xff && code[1] == 0x25)
		return **(const unsigned long *const *)(code + 2);
	if (code[0] == 0xe9)
		return (unsigned long)code + 5 + *(const long *)(code + 1);
	return 0;
}

static void entry_describe(const char *when)
{
	const unsigned char *code = (const unsigned char *)RGL.rgl_glActiveTextureARB;

	if (!code)
		return;
	platform_log("OpenGL: %s, glActiveTextureARB at %p: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x, going to %08lx",
		when, (const void *)code, code[0], code[1], code[2], code[3], code[4], code[5], code[6], code[7],
		code[8], code[9], code[10], code[11], entry_target(code));
}

static void multitexture_load(void)
{
#define RETRO_GL_LOAD_OPTIONAL(type, name, parameters) \
	RGL.rgl_##name = (type (RETRO_GL_API *) parameters)SDL_GL_GetProcAddress(#name);
	RETRO_GL_MULTITEXTURE_FUNCTIONS(RETRO_GL_LOAD_OPTIONAL)
#undef RETRO_GL_LOAD_OPTIONAL
}

static int entry_broken(void)
{
	unsigned long target = entry_target((const void *)RGL.rgl_glActiveTextureARB);

	return RGL.rgl_glActiveTextureARB && target && target < 0x10000;
}

/* the multitexture functions found again (the window changed size), or given
up when they still go nowhere: one texture a pass then */
void retro_gl_window_changed(void)
{
	if (!RGL.rgl_glActiveTextureARB)
		return;
	entry_describe("the window changed");
	retro_gl_protect(FALSE);
#ifdef _WIN32
	{
		void *dc = retro_gl_current_dc(), *context = retro_gl_current_context();

		if (dc && context)
			retro_gl_make_current(dc, context);
	}
#endif
	multitexture_load();
	if (entry_broken())
	{
		platform_log("OpenGL: the multitexture functions go nowhere since the window changed; one texture unit from now on");
		RGL.rgl_glActiveTextureARB = NULL;
		RGL.rgl_glClientActiveTextureARB = NULL;
		retro_gl_capabilities.texture_units = 1;
	}
	retro_gl_protect(TRUE);
	entry_describe("found again");
}

/* before each draw: the window may have changed since the last */
void retro_gl_check(void)
{
	if (entry_broken())
		retro_gl_window_changed();
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
		multitexture_load();
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

	retro_gl_protect(TRUE);
	entry_describe("loaded");
	platform_log("OpenGL: %s, %s, version %s", (const char *)RGL.rgl_glGetString(RGL_VENDOR),
		(const char *)RGL.rgl_glGetString(RGL_RENDERER), (const char *)RGL.rgl_glGetString(RGL_VERSION));
	platform_log("OpenGL: %d texture units, textures up to %d, depth %d bits, stencil %d bits%s%s",
		retro_gl_capabilities.texture_units, retro_gl_capabilities.maximum_texture_size,
		retro_gl_capabilities.depth_bits, retro_gl_capabilities.stencil_bits,
		retro_gl_capabilities.env_combine ? ", combine" : "", retro_gl_capabilities.env_add ? ", add" : "");
	return TRUE;
}
