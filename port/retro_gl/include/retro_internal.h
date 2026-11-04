/*
RETRO_INTERNAL.H

What the parts of port/retro_gl share: the Xbox Direct3D 8 device on
OpenGL 1.1 (retro_device.c), its vertex programs run on the processor
(retro_vertex.c), its pixel shaders mapped onto the texture units
(retro_pixel.c) and its textures (retro_textures.c).

The game draws through the Xbox's Direct3D 8 (the XDK's inline functions,
which keep simple state in D3D__RenderState and call the device for the
rest), as on every other build; port/linux/src/d3d8_gl.c is that device on
OpenGL 4.5, and its comments explain the Xbox's conventions this keeps.
*/

#ifndef __RETRO_INTERNAL_H
#define __RETRO_INTERNAL_H

#include "platform.h"
#include "retro_gl.h"

/* ---------- vertex programs (retro_vertex.c) */

#define RETRO_ATTRIBUTE_COUNT 16
#define RETRO_CONSTANT_COUNT 192
/* Direct3D's constant register -96 is the hardware's 0 */
#define RETRO_CONSTANT_BIAS 96

struct retro_vertex_element
{
	unsigned char reg;
	unsigned char stream;
	unsigned char type;
	unsigned char bytes;
	unsigned short offset;
};

/* an instruction decoded once (retro_vertex_program_decode) */
struct retro_vertex_operand
{
	unsigned char mux;
	unsigned char index;
	unsigned char negate;
	unsigned char swizzle[4];
};

struct retro_vertex_instruction
{
	unsigned char mac, ilu;
	unsigned char mac_mask, ilu_mask, output_mask;
	unsigned char temporary, ilu_temporary;
	unsigned char output_register, output_address, output_from_ilu;
	unsigned char relative;
	/* the screen-space conversion's reciprocal of the clip-space w: the
	position before it is kept (retro_vertex_run) */
	unsigned char captures_clip;
	unsigned short constant;
	unsigned short input;
	struct retro_vertex_operand a, b, c;
};

struct retro_vertex_program
{
	unsigned long signature;
	unsigned long id;
	struct retro_vertex_instruction *instructions;
	unsigned long instruction_count;
	struct retro_vertex_element elements[RETRO_ATTRIBUTE_COUNT];
	unsigned long element_count;
};

/* a vertex as the program leaves it, ready for OpenGL 1.1: the position
in OpenGL's clip space, the colors clamped and as bytes (RGBA), four
texture coordinates (s, t, r, q) and the fog factor */
struct retro_vertex
{
	float position[4];
	unsigned char diffuse[4];
	unsigned char specular[4];
	float texture[4][4];
	float fog;
};

/* the state a vertex program reads besides its inputs */
struct retro_vertex_state
{
	const float (*constants)[4];
	float viewport_scale[4];
	float viewport_offset[4];
	/* the columns the menus shift by to center on a wide screen */
	float screen_offset;
};

void retro_vertex_program_decode(struct retro_vertex_program *program, const DWORD *instructions,
	unsigned long instruction_count);
/* runs the program on inputs (16 registers of 4 floats), writing out */
void retro_vertex_run(const struct retro_vertex_program *program, const struct retro_vertex_state *state,
	const float (*inputs)[4], struct retro_vertex *out);
/* one attribute of the stream's format as 4 floats */
void retro_vertex_fetch(const struct retro_vertex_element *element, const unsigned char *source, float *value);
void retro_vertex_declaration_parse(struct retro_vertex_program *program, const DWORD *declaration);

/* ---------- textures (retro_textures.c) */

struct retro_texture
{
	RGLuint name;
	/* texture coordinates times these reach the uploaded texture's: an
	Xbox linear texture is addressed in texels, a swizzled one from 0 to 1 */
	float coordinate_scale[2];
	/* it has transparent or partly transparent texels */
	int has_alpha;
};

/* the OpenGL texture for a Direct3D texture header (and the palette of a
P8 one); FALSE where there is none to be had (a cube or volume texture,
a render target, out of memory): the stage is then left out */
int retro_texture_get(const D3DBaseTexture *texture, const D3DPalette *palette, struct retro_texture *result);
void retro_textures_begin_frame(unsigned long frame);
/* whether a texture's texels are a render target's (the device knows) */
int retro_device_is_render_target(DWORD data);

/* ---------- the device (retro_device.c) */

extern DWORD D3D__RenderState[D3DRS_MAX];
extern DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX];

/* the device's textures and their palettes, as SetTexture left them */
const D3DBaseTexture *retro_device_texture(int stage);
const D3DPalette *retro_device_palette(int stage);

/* ---------- pixel shaders (retro_pixel.c) */

/* the texture units' setup for the draw about to be made: which stage's
texture and coordinates each unit takes (-1 for none), and whether the
vertices' diffuse color is used at all; the units' state is set here */
struct retro_pixel_setup
{
	int unit_stage[4];
	int units;
	/* each unit's texture coordinates times these (retro_texture's) */
	float texture_scale[4][2];
	/* the vertices' diffuse color is drawn with (else white) */
	int diffuse;
	/* passes the draw needs (1 for now) */
	int passes;
};

/* the shader's combiners, as a number (each logged once by it) */
unsigned long retro_pixel_hash(void);
int retro_pixel_prepare(struct retro_pixel_setup *setup, const struct retro_vertex *first_vertex);
/* each vertex's color as the pixel shader makes it with its textures white
(retro_pixel.c): what the texture units then multiply the textures into */
void retro_pixel_vertex_colors(struct retro_vertex *vertices, unsigned long count);

float retro_dword_to_float(DWORD value);
void retro_color_to_floats(D3DCOLOR color, float *rgba);

#endif
