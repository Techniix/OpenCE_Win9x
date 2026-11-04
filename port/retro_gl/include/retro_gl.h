/*
RETRO_GL.H

The OpenGL that port/retro_gl draws with: version 1.1, and the extensions
cards of 1998 to 2001 have (ARB_multitexture first), each function looked up
through SDL_GL_GetProcAddress (port/retro_sdl), so that the same code runs
on Windows 9x's opengl32.dll, Mac OS 9's OpenGL and DOS's Mesa. Only the
calls retro_gl makes are here, under names of their own (rgl_*), so that
nothing here meets the platform's headers or the OpenGL 4.5 renderer's
(port/linux/src/gl.h).
*/

#ifndef __RETRO_GL_H
#define __RETRO_GL_H

#if defined(_WIN32)
#define RETRO_GL_API __stdcall
#else
#define RETRO_GL_API
#endif

typedef unsigned int RGLenum;
typedef unsigned int RGLbitfield;
typedef unsigned int RGLuint;
typedef int RGLint;
typedef int RGLsizei;
typedef unsigned char RGLboolean;
typedef float RGLfloat;
typedef double RGLdouble;
typedef unsigned char RGLubyte;
typedef void RGLvoid;

/* ---------- constants (OpenGL 1.1's values) */

#define RGL_FALSE 0
#define RGL_TRUE 1

#define RGL_POINTS 0x0000
#define RGL_LINES 0x0001
#define RGL_LINE_LOOP 0x0002
#define RGL_LINE_STRIP 0x0003
#define RGL_TRIANGLES 0x0004
#define RGL_TRIANGLE_STRIP 0x0005
#define RGL_TRIANGLE_FAN 0x0006
#define RGL_QUADS 0x0007
#define RGL_QUAD_STRIP 0x0008
#define RGL_POLYGON 0x0009

#define RGL_NEVER 0x0200
#define RGL_LESS 0x0201
#define RGL_EQUAL 0x0202
#define RGL_LEQUAL 0x0203
#define RGL_GREATER 0x0204
#define RGL_NOTEQUAL 0x0205
#define RGL_GEQUAL 0x0206
#define RGL_ALWAYS 0x0207

#define RGL_ZERO 0
#define RGL_ONE 1
#define RGL_SRC_COLOR 0x0300
#define RGL_ONE_MINUS_SRC_COLOR 0x0301
#define RGL_SRC_ALPHA 0x0302
#define RGL_ONE_MINUS_SRC_ALPHA 0x0303
#define RGL_DST_ALPHA 0x0304
#define RGL_ONE_MINUS_DST_ALPHA 0x0305
#define RGL_DST_COLOR 0x0306
#define RGL_ONE_MINUS_DST_COLOR 0x0307
#define RGL_SRC_ALPHA_SATURATE 0x0308

#define RGL_FRONT 0x0404
#define RGL_BACK 0x0405
#define RGL_FRONT_AND_BACK 0x0408
#define RGL_CW 0x0900
#define RGL_CCW 0x0901

#define RGL_CULL_FACE 0x0B44
#define RGL_DEPTH_TEST 0x0B71
#define RGL_STENCIL_TEST 0x0B90
#define RGL_ALPHA_TEST 0x0BC0
#define RGL_BLEND 0x0BE2
#define RGL_SCISSOR_TEST 0x0C11
#define RGL_TEXTURE_2D 0x0DE1
#define RGL_FOG 0x0B60
#define RGL_POLYGON_OFFSET_FILL 0x8037
#define RGL_DITHER 0x0BD0
#define RGL_LIGHTING 0x0B50

#define RGL_KEEP 0x1E00
#define RGL_REPLACE 0x1E01
#define RGL_INCR 0x1E02
#define RGL_DECR 0x1E03
#define RGL_INVERT 0x150A
/* (EXT_stencil_wrap) */
#define RGL_INCR_WRAP 0x8507
#define RGL_DECR_WRAP 0x8508

#define RGL_COLOR_BUFFER_BIT 0x00004000
#define RGL_DEPTH_BUFFER_BIT 0x00000100
#define RGL_STENCIL_BUFFER_BIT 0x00000400

#define RGL_VENDOR 0x1F00
#define RGL_RENDERER 0x1F01
#define RGL_VERSION 0x1F02
#define RGL_EXTENSIONS 0x1F03
#define RGL_MAX_TEXTURE_SIZE 0x0D33

#define RGL_UNSIGNED_BYTE 0x1401
#define RGL_UNSIGNED_SHORT 0x1403
#define RGL_FLOAT 0x1406

#define RGL_RGB 0x1907
#define RGL_RGBA 0x1908
#define RGL_LUMINANCE 0x1909
#define RGL_ALPHA 0x1906
#define RGL_RGB5 0x8050
#define RGL_RGBA4 0x8056
#define RGL_RGB5_A1 0x8057
#define RGL_RGB8 0x8051
#define RGL_RGBA8 0x8058

#define RGL_TEXTURE_MAG_FILTER 0x2800
#define RGL_TEXTURE_MIN_FILTER 0x2801
#define RGL_TEXTURE_WRAP_S 0x2802
#define RGL_TEXTURE_WRAP_T 0x2803
#define RGL_NEAREST 0x2600
#define RGL_LINEAR 0x2601
#define RGL_NEAREST_MIPMAP_NEAREST 0x2700
#define RGL_LINEAR_MIPMAP_NEAREST 0x2701
#define RGL_NEAREST_MIPMAP_LINEAR 0x2702
#define RGL_LINEAR_MIPMAP_LINEAR 0x2703
#define RGL_REPEAT 0x2901
#define RGL_CLAMP 0x2900
/* (SGIS_texture_edge_clamp, OpenGL 1.2) */
#define RGL_CLAMP_TO_EDGE 0x812F

#define RGL_TEXTURE_ENV 0x2300
#define RGL_TEXTURE_ENV_MODE 0x2200
#define RGL_TEXTURE_ENV_COLOR 0x2201
#define RGL_MODULATE 0x2100
#define RGL_DECAL 0x2101
#define RGL_ADD 0x0104
/* (EXT_texture_env_combine / ARB_texture_env_combine) */
#define RGL_COMBINE 0x8570
#define RGL_COMBINE_RGB 0x8571
#define RGL_COMBINE_ALPHA 0x8572
#define RGL_SOURCE0_RGB 0x8580
#define RGL_SOURCE1_RGB 0x8581
#define RGL_SOURCE2_RGB 0x8582
#define RGL_SOURCE0_ALPHA 0x8588
#define RGL_SOURCE1_ALPHA 0x8589
#define RGL_SOURCE2_ALPHA 0x858A
#define RGL_OPERAND0_RGB 0x8590
#define RGL_OPERAND1_RGB 0x8591
#define RGL_OPERAND2_RGB 0x8592
#define RGL_OPERAND0_ALPHA 0x8598
#define RGL_OPERAND1_ALPHA 0x8599
#define RGL_OPERAND2_ALPHA 0x859A
#define RGL_RGB_SCALE 0x8573
#define RGL_ADD_SIGNED 0x8574
#define RGL_INTERPOLATE 0x8575
#define RGL_CONSTANT 0x8576
#define RGL_PRIMARY_COLOR 0x8577
#define RGL_PREVIOUS 0x8578
#define RGL_TEXTURE 0x1702
#define RGL_ALPHA_SCALE 0x0D1C

#define RGL_MODELVIEW 0x1700
#define RGL_PROJECTION 0x1701

#define RGL_VERTEX_ARRAY 0x8074
#define RGL_COLOR_ARRAY 0x8076
#define RGL_TEXTURE_COORD_ARRAY 0x8078

#define RGL_FOG_MODE 0x0B65
#define RGL_FOG_COLOR 0x0B66
#define RGL_FOG_START 0x0B63
#define RGL_FOG_END 0x0B64
#define RGL_FOG_HINT 0x0C54
#define RGL_UNPACK_ALIGNMENT 0x0CF5
#define RGL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define RGL_NICEST 0x1102
#define RGL_FASTEST 0x1101

/* (ARB_multitexture) */
#define RGL_TEXTURE0 0x84C0
#define RGL_MAX_TEXTURE_UNITS 0x84E2

/* ---------- functions */

#define RETRO_GL_FUNCTIONS(F) \
	F(void, glAlphaFunc, (RGLenum func, RGLfloat ref)) \
	F(void, glBegin, (RGLenum mode)) \
	F(void, glBindTexture, (RGLenum target, RGLuint texture)) \
	F(void, glBlendFunc, (RGLenum sfactor, RGLenum dfactor)) \
	F(void, glClear, (RGLbitfield mask)) \
	F(void, glClearColor, (RGLfloat red, RGLfloat green, RGLfloat blue, RGLfloat alpha)) \
	F(void, glClearDepth, (RGLdouble depth)) \
	F(void, glClearStencil, (RGLint s)) \
	F(void, glColor4ub, (RGLubyte red, RGLubyte green, RGLubyte blue, RGLubyte alpha)) \
	F(void, glColorMask, (RGLboolean red, RGLboolean green, RGLboolean blue, RGLboolean alpha)) \
	F(void, glColorPointer, (RGLint size, RGLenum type, RGLsizei stride, const RGLvoid *pointer)) \
	F(void, glCopyTexSubImage2D, (RGLenum target, RGLint level, RGLint xoffset, RGLint yoffset, RGLint x, RGLint y, \
		RGLsizei width, RGLsizei height)) \
	F(void, glCullFace, (RGLenum mode)) \
	F(void, glDeleteTextures, (RGLsizei n, const RGLuint *textures)) \
	F(void, glDepthFunc, (RGLenum func)) \
	F(void, glDepthMask, (RGLboolean flag)) \
	F(void, glDepthRange, (RGLdouble near_value, RGLdouble far_value)) \
	F(void, glDisable, (RGLenum cap)) \
	F(void, glDisableClientState, (RGLenum array)) \
	F(void, glDrawArrays, (RGLenum mode, RGLint first, RGLsizei count)) \
	F(void, glDrawElements, (RGLenum mode, RGLsizei count, RGLenum type, const RGLvoid *indices)) \
	F(void, glEnable, (RGLenum cap)) \
	F(void, glEnableClientState, (RGLenum array)) \
	F(void, glEnd, (void)) \
	F(void, glFinish, (void)) \
	F(void, glFlush, (void)) \
	F(void, glFogf, (RGLenum pname, RGLfloat param)) \
	F(void, glFogfv, (RGLenum pname, const RGLfloat *params)) \
	F(void, glFogi, (RGLenum pname, RGLint param)) \
	F(void, glFrontFace, (RGLenum mode)) \
	F(void, glGenTextures, (RGLsizei n, RGLuint *textures)) \
	F(RGLenum, glGetError, (void)) \
	F(void, glGetIntegerv, (RGLenum pname, RGLint *params)) \
	F(const RGLubyte *, glGetString, (RGLenum name)) \
	F(void, glHint, (RGLenum target, RGLenum mode)) \
	F(void, glLoadIdentity, (void)) \
	F(void, glMatrixMode, (RGLenum mode)) \
	F(void, glPixelStorei, (RGLenum pname, RGLint param)) \
	F(void, glPolygonOffset, (RGLfloat factor, RGLfloat units)) \
	F(void, glScissor, (RGLint x, RGLint y, RGLsizei width, RGLsizei height)) \
	F(void, glStencilFunc, (RGLenum func, RGLint ref, RGLuint mask)) \
	F(void, glStencilMask, (RGLuint mask)) \
	F(void, glStencilOp, (RGLenum fail, RGLenum zfail, RGLenum zpass)) \
	F(void, glTexCoordPointer, (RGLint size, RGLenum type, RGLsizei stride, const RGLvoid *pointer)) \
	F(void, glTexEnvfv, (RGLenum target, RGLenum pname, const RGLfloat *params)) \
	F(void, glTexEnvi, (RGLenum target, RGLenum pname, RGLint param)) \
	F(void, glTexImage2D, (RGLenum target, RGLint level, RGLint internalformat, RGLsizei width, RGLsizei height, \
		RGLint border, RGLenum format, RGLenum type, const RGLvoid *pixels)) \
	F(void, glTexParameteri, (RGLenum target, RGLenum pname, RGLint param)) \
	F(void, glTexSubImage2D, (RGLenum target, RGLint level, RGLint xoffset, RGLint yoffset, RGLsizei width, \
		RGLsizei height, RGLenum format, RGLenum type, const RGLvoid *pixels)) \
	F(void, glVertexPointer, (RGLint size, RGLenum type, RGLsizei stride, const RGLvoid *pointer)) \
	F(void, glViewport, (RGLint x, RGLint y, RGLsizei width, RGLsizei height))

/* (ARB_multitexture: NULL without it, when one texture unit is all there is) */
#define RETRO_GL_MULTITEXTURE_FUNCTIONS(F) \
	F(void, glActiveTextureARB, (RGLenum texture)) \
	F(void, glClientActiveTextureARB, (RGLenum texture))

/* what the card offers (retro_gl_load) */
struct retro_gl_capabilities
{
	/* texture units (1 without ARB_multitexture) */
	int texture_units;
	/* the largest texture's side (256 on a Voodoo3) */
	int maximum_texture_size;
	/* (ARB_texture_env_combine or EXT_texture_env_combine) */
	int env_combine;
	/* (EXT_texture_env_add) */
	int env_add;
	int edge_clamp;
	int stencil_wrap;
	int stencil_bits;
	int depth_bits;
};

/* the functions and the capabilities, on a page of their own that is made
read-only once they are found (retro_gl_load): a stray write there (one
overwrote a function pointer while a level loaded) then stops at the
writer, which the crash report names, instead of a call going astray */
struct retro_gl_table
{
#define RETRO_GL_MEMBER(type, name, parameters) type (RETRO_GL_API *rgl_##name) parameters;
	RETRO_GL_FUNCTIONS(RETRO_GL_MEMBER)
	RETRO_GL_MULTITEXTURE_FUNCTIONS(RETRO_GL_MEMBER)
#undef RETRO_GL_MEMBER
	struct retro_gl_capabilities capabilities;
};

union retro_gl_page
{
	struct retro_gl_table table;
	unsigned char page[4096];
};

extern union retro_gl_page retro_gl_page;

#define RGL retro_gl_page.table
#define retro_gl_capabilities retro_gl_page.table.capabilities

/* looks every function up and reads the capabilities, the context being
current; FALSE if OpenGL 1.1's own functions are missing */
int retro_gl_load(void);
int retro_gl_has_extension(const char *name);

#endif
