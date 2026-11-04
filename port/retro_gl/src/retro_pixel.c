/*
RETRO_PIXEL.C

The Xbox pixel shaders (the NV2A's texture stages and register combiners,
held in the pixel shader render states: port/linux/src/nv2a_psh.c reads
them in full) on the texture units of a card of the time.

The register combiners compute far more than OpenGL 1.1's texture
environment can: up to eight stages of products, sums and multiplexes of
four textures, two colors and constants. This first mapping takes what a
shader reads, not how it combines it: the textures it samples (up to the
card's units, in stage order) are multiplied together, and by the vertices'
diffuse color where the shader reads that. That draws most of the game's
surfaces as a base texture lit by its vertex color or by a lightmap, which
is what they mostly are; the exact combinations come shader by shader,
which is why each new one is logged (retro_pixel_log) with what it reads.
*/

#include "retro_internal.h"

#include <string.h>

enum
{
	_register_zero = 0,
	_register_c0 = 1,
	_register_c1 = 2,
	_register_fog = 3,
	_register_v0 = 4,
	_register_v1 = 5,
	_register_t0 = 8,
	_register_t1 = 9,
	_register_t2 = 10,
	_register_t3 = 11,
	_register_r0 = 12,
	_register_r1 = 13,
	_register_v1r0_sum = 14,
	_register_ef_product = 15,
};

enum
{
	_mode_none = 0x00,
	_mode_project2d = 0x01,
	_mode_passthru = 0x04,
};

/* the registers a combiner input word's four bytes read, a bit each */
static unsigned long input_registers(DWORD inputs)
{
	unsigned long registers = 0;
	int byte;

	for (byte = 0; byte < 4; byte++)
		registers |= 1UL << ((inputs >> (byte * 8)) & 0x0f);
	return registers;
}

/* every register the shader reads in its stages and final combiner */
static unsigned long shader_registers(void)
{
	unsigned long count = D3D__RenderState[D3DRS_PSCOMBINERCOUNT] & 0xff;
	unsigned long registers = 0, stage;
	DWORD final_efg = D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSEFG];

	if (count > 8)
		count = 8;
	for (stage = 0; stage < count; stage++)
	{
		registers |= input_registers(D3D__RenderState[D3DRS_PSRGBINPUTS0 + stage]);
		registers |= input_registers(D3D__RenderState[D3DRS_PSALPHAINPUTS0 + stage]);
	}
	registers |= input_registers(D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSABCD]);
	/* G, the alpha (EFG's second byte; the first holds flags); E and F only
	where A to D read their product */
	registers |= 1UL << ((final_efg >> 8) & 0x0f);
	if (registers & (1UL << _register_ef_product))
		registers |= (1UL << ((final_efg >> 24) & 0x0f)) | (1UL << ((final_efg >> 16) & 0x0f));
	return registers & ~1UL;
}

/* ---------- the shaders seen, logged once each */

#define LOGGED_SHADER_COUNT 512

static unsigned long logged_hashes[LOGGED_SHADER_COUNT];
static unsigned long logged_count;

static unsigned long combiner_hash(void)
{
	unsigned long hash = 2166136261UL, index;

	for (index = 0; index < 8; index++)
	{
		hash = (hash ^ D3D__RenderState[D3DRS_PSRGBINPUTS0 + index]) * 16777619UL;
		hash = (hash ^ D3D__RenderState[D3DRS_PSALPHAINPUTS0 + index]) * 16777619UL;
		hash = (hash ^ D3D__RenderState[D3DRS_PSRGBOUTPUTS0 + index]) * 16777619UL;
		hash = (hash ^ D3D__RenderState[D3DRS_PSALPHAOUTPUTS0 + index]) * 16777619UL;
	}
	hash = (hash ^ D3D__RenderState[D3DRS_PSCOMBINERCOUNT]) * 16777619UL;
	hash = (hash ^ D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSABCD]) * 16777619UL;
	hash = (hash ^ D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSEFG]) * 16777619UL;
	hash = (hash ^ D3D__RenderState[D3DRS_PSTEXTUREMODES]) * 16777619UL;
	return hash;
}

static void shader_log(unsigned long registers)
{
	unsigned long hash = combiner_hash(), index, count;
	char reads[64];
	static const char *names[16] =
	{
		"", "c0", "c1", "fog", "v0", "v1", "?6", "?7", "t0", "t1", "t2", "t3", "r0", "r1", "v1r0", "ef",
	};

	for (index = 0; index < logged_count; index++)
	{
		if (logged_hashes[index] == hash)
			return;
	}
	if (logged_count == LOGGED_SHADER_COUNT)
		return;
	logged_hashes[logged_count++] = hash;
	reads[0] = '\0';
	for (index = 1; index < 16; index++)
	{
		if (registers & (1UL << index))
		{
			strcat(reads, " ");
			strcat(reads, names[index]);
		}
	}
	count = D3D__RenderState[D3DRS_PSCOMBINERCOUNT] & 0xff;
	platform_log("pixel shader %08lx: %lu stages, modes %05lx, reads%s; final %08lx %08lx; stage 0 %08lx %08lx",
		hash, count, (unsigned long)D3D__RenderState[D3DRS_PSTEXTUREMODES] & 0xfffff, reads,
		(unsigned long)D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSABCD],
		(unsigned long)D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSEFG],
		(unsigned long)D3D__RenderState[D3DRS_PSRGBINPUTS0], (unsigned long)D3D__RenderState[D3DRS_PSRGBOUTPUTS0]);
}

/* ---------- the units */

static RGLint wrap_mode(DWORD address)
{
	switch (address)
	{
	case D3DTADDRESS_CLAMP:
	case D3DTADDRESS_BORDER:
	case D3DTADDRESS_CLAMPTOEDGE:
		return retro_gl_capabilities.edge_clamp ? RGL_CLAMP_TO_EDGE : RGL_CLAMP;
	default:
		return RGL_REPEAT;
	}
}

static void unit_select(int unit)
{
	if (rgl_glActiveTextureARB)
		rgl_glActiveTextureARB(RGL_TEXTURE0 + unit);
}

static void unit_texture(int unit, int stage, const struct retro_texture *texture, int modulate)
{
	const DWORD *state = D3D__TextureState[stage];
	BOOL linear = state[D3DTSS_MAGFILTER] != D3DTEXF_POINT;
	BOOL mip = state[D3DTSS_MIPFILTER] != D3DTEXF_NONE;

	unit_select(unit);
	rgl_glEnable(RGL_TEXTURE_2D);
	rgl_glBindTexture(RGL_TEXTURE_2D, texture->name);
	rgl_glTexParameteri(RGL_TEXTURE_2D, RGL_TEXTURE_MAG_FILTER, linear ? RGL_LINEAR : RGL_NEAREST);
	rgl_glTexParameteri(RGL_TEXTURE_2D, RGL_TEXTURE_MIN_FILTER,
		!mip ? (linear ? RGL_LINEAR : RGL_NEAREST) :
		state[D3DTSS_MIPFILTER] == D3DTEXF_POINT ? (linear ? RGL_LINEAR_MIPMAP_NEAREST : RGL_NEAREST_MIPMAP_NEAREST) :
		(linear ? RGL_LINEAR_MIPMAP_LINEAR : RGL_NEAREST_MIPMAP_LINEAR));
	rgl_glTexParameteri(RGL_TEXTURE_2D, RGL_TEXTURE_WRAP_S, wrap_mode(state[D3DTSS_ADDRESSU]));
	rgl_glTexParameteri(RGL_TEXTURE_2D, RGL_TEXTURE_WRAP_T, wrap_mode(state[D3DTSS_ADDRESSV]));
	rgl_glTexEnvi(RGL_TEXTURE_ENV, RGL_TEXTURE_ENV_MODE, modulate ? RGL_MODULATE : RGL_REPLACE);
}

int retro_pixel_prepare(struct retro_pixel_setup *setup)
{
	unsigned long registers = shader_registers();
	DWORD modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	int stage, unit = 0;
	/* the vertices' color: read by the shader, or there is nothing else */
	int diffuse = (registers & (1UL << _register_v0)) != 0;

	shader_log(registers);
	memset(setup, 0, sizeof(*setup));
	setup->unit_stage[0] = setup->unit_stage[1] = setup->unit_stage[2] = setup->unit_stage[3] = -1;
	for (stage = 0; stage < D3DTSS_MAXSTAGES && unit < retro_gl_capabilities.texture_units; stage++)
	{
		DWORD mode = (modes >> (stage * 5)) & 0x1f;
		struct retro_texture texture;

		if (!(registers & (1UL << (_register_t0 + stage))) || (mode != _mode_project2d && mode != _mode_passthru))
			continue;
		if (mode == _mode_passthru || !retro_texture_get(retro_device_texture(stage), retro_device_palette(stage),
			&texture))
		{
			continue;
		}
		/* the first unit takes the diffuse color, if the shader reads it;
		each after takes what the one before made */
		unit_texture(unit, stage, &texture, unit > 0 || diffuse);
		setup->unit_stage[unit] = stage;
		setup->texture_scale[unit][0] = texture.coordinate_scale[0];
		setup->texture_scale[unit][1] = texture.coordinate_scale[1];
		unit++;
	}
	setup->units = unit;
	/* (the units the draw leaves out, off) */
	for (stage = unit; stage < retro_gl_capabilities.texture_units; stage++)
	{
		unit_select(stage);
		rgl_glDisable(RGL_TEXTURE_2D);
	}
	unit_select(0);
	setup->diffuse = diffuse || !unit;
	setup->passes = 1;
	return TRUE;
}
