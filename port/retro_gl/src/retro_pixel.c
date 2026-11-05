/*
RETRO_PIXEL.C

The Xbox pixel shaders (the NV2A's texture stages and register combiners,
held in the pixel shader render states: port/linux/src/nv2a_psh.c reads
them in full, and this follows it) on the texture units of a card of the
time.

The register combiners compute far more than OpenGL 1.1's texture
environment can: up to eight stages of products, sums, dot products and
multiplexes of four textures, two colors, the fog and constants. Here the
combiners themselves run on the processor, once for each vertex
(retro_pixel_vertex_color), with each texture the draw samples standing in
as white: what comes out is everything the shader does with the vertex
colors, the constants and the fog, and becomes the vertex's color, which
the texture units then multiply by the textures (in stage order, up to the
card's units). A texture the result does not change with (its stage white
or black gives the same color) is left off. That is exact for the many
shaders that are a texture times a color, a tint or a fade (the menus, the
HUD, the text), and close for the others, which mix their textures in ways
the units cannot; each new shader is logged once with what it reads.
*/

#include "retro_internal.h"
#include "retro_affine.h"

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

unsigned long retro_pixel_hash(void)
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
	unsigned long hash = retro_pixel_hash(), index, count;
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
	/* (each stage: color inputs and outputs, alpha inputs and outputs) */
	for (index = 0; index < count && index < 8; index++)
		platform_log("pixel shader %08lx stage %lu: %08lx %08lx, alpha %08lx %08lx", hash, index,
			(unsigned long)D3D__RenderState[D3DRS_PSRGBINPUTS0 + index],
			(unsigned long)D3D__RenderState[D3DRS_PSRGBOUTPUTS0 + index],
			(unsigned long)D3D__RenderState[D3DRS_PSALPHAINPUTS0 + index],
			(unsigned long)D3D__RenderState[D3DRS_PSALPHAOUTPUTS0 + index]);
}

/* ---------- the combiners on the processor (as nv2a_psh.c) */

/* an input register's value as a combiner reads it: a byte of the input
word (register, alpha bit, mapping) */
static void combiner_input(const float (*registers)[4], unsigned long input, int alpha_portion, float *out)
{
	const float *source = registers[input & 0x0f];
	int alpha_channel = (input & 0x10) != 0;
	int component;

	for (component = 0; component < 3; component++)
	{
		float value = alpha_portion ? source[alpha_channel ? 3 : 2] : alpha_channel ? source[3] : source[component];
		float positive = value > 0.0f ? value : 0.0f;

		switch (input & 0xe0)
		{
		case 0x00: value = positive; break;
		case 0x20: value = 1.0f - (value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value); break;
		case 0x40: value = 2.0f * positive - 1.0f; break;
		case 0x60: value = 1.0f - 2.0f * positive; break;
		case 0x80: value = positive - 0.5f; break;
		case 0xa0: value = 0.5f - positive; break;
		case 0xc0: break;
		default: value = -value; break;
		}
		out[component] = value;
	}
}

static float output_mapped(float value, unsigned long flags)
{
	switch (flags & 0x38)
	{
	case 0x08: value = value - 0.5f; break;
	case 0x10: value = value * 2.0f; break;
	case 0x18: value = (value - 0.5f) * 2.0f; break;
	case 0x20: value = value * 4.0f; break;
	case 0x30: value = value * 0.5f; break;
	default: break;
	}
	return value < -1.0f ? -1.0f : value > 1.0f ? 1.0f : value;
}

static float unit_clamp(float value)
{
	return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

/* the final combiner's inputs: the unsigned identity and invert only */
static void final_input(const float (*registers)[4], unsigned long input, int alpha_portion, float *out)
{
	const float *source = registers[input & 0x0f];
	int alpha_channel = (input & 0x10) != 0;
	int component;

	for (component = 0; component < 3; component++)
	{
		float value = alpha_portion ? source[alpha_channel ? 3 : 2] : alpha_channel ? source[3] : source[component];

		value = unit_clamp(value);
		out[component] = (input & 0x20) ? 1.0f - value : value;
	}
}

/* what the pixel shader makes of these inputs (each RGBA, 0 to 1): the
diffuse and specular colors, the fog factor and the four textures */
static void combiners_run(const float *diffuse, const float *specular, float fog_factor, const float (*textures)[4],
	float *result)
{
	/* (indexed as the registers are: zero, c0, c1, fog, v0, v1, -, -, t0 to
	t3, r0, r1, v1r0 sum, ef product) */
	float registers[16][4];
	DWORD *state = D3D__RenderState;
	unsigned long count = state[D3DRS_PSCOMBINERCOUNT] & 0xff;
	BOOL unique_c0 = (state[D3DRS_PSCOMBINERCOUNT] & 0x1000) != 0;
	BOOL unique_c1 = (state[D3DRS_PSCOMBINERCOUNT] & 0x10000) != 0;
	BOOL mux_msb = (state[D3DRS_PSCOMBINERCOUNT] & 0x100) != 0;
	DWORD final_abcd = state[D3DRS_PSFINALCOMBINERINPUTSABCD];
	DWORD final_efg = state[D3DRS_PSFINALCOMBINERINPUTSEFG];
	unsigned long stage;
	int component;

	memset(registers, 0, sizeof(registers));
	retro_color_to_floats(state[D3DRS_FOGCOLOR], registers[_register_fog]);
	registers[_register_fog][3] = unit_clamp(fog_factor);
	memcpy(registers[_register_v0], diffuse, sizeof(registers[0]));
	memcpy(registers[_register_v1], specular, sizeof(registers[0]));
	for (stage = 0; stage < 4; stage++)
		memcpy(registers[_register_t0 + stage], textures[stage], sizeof(registers[0]));
	registers[_register_r0][3] = textures[0][3];
	if (count > 8)
		count = 8;
	for (stage = 0; stage < count; stage++)
	{
		float products[2][3][3]; /* [portion][ab, cd, sum][component] */
		int portion;

		retro_color_to_floats(state[D3DRS_PSCONSTANT0_0 + (unique_c0 ? stage : 0)], registers[_register_c0]);
		retro_color_to_floats(state[D3DRS_PSCONSTANT1_0 + (unique_c1 ? stage : 0)], registers[_register_c1]);
		for (portion = 0; portion < 2; portion++)
		{
			DWORD inputs = portion ? state[D3DRS_PSALPHAINPUTS0 + stage] : state[D3DRS_PSRGBINPUTS0 + stage];
			DWORD outputs = portion ? state[D3DRS_PSALPHAOUTPUTS0 + stage] : state[D3DRS_PSRGBOUTPUTS0 + stage];
			unsigned long flags = outputs >> 12;
			float a[3], b[3], c[3], d[3];

			combiner_input((const float (*)[4])registers, (inputs >> 24) & 0xff, portion, a);
			combiner_input((const float (*)[4])registers, (inputs >> 16) & 0xff, portion, b);
			combiner_input((const float (*)[4])registers, (inputs >> 8) & 0xff, portion, c);
			combiner_input((const float (*)[4])registers, inputs & 0xff, portion, d);
			for (component = 0; component < 3; component++)
			{
				float ab = !portion && (flags & 0x02) ? a[0] * b[0] + a[1] * b[1] + a[2] * b[2] : a[component] * b[component];
				float cd = !portion && (flags & 0x01) ? c[0] * d[0] + c[1] * d[1] + c[2] * d[2] : c[component] * d[component];
				float sum;

				if (flags & 0x04)
					sum = (mux_msb ? registers[_register_r0][3] >= 0.5f :
						((int)(registers[_register_r0][3] * 255.0f + 0.5f) & 1) != 0) ? cd : ab;
				else
					sum = ab + cd;
				products[portion][0][component] = output_mapped(ab, flags);
				products[portion][1][component] = output_mapped(cd, flags);
				products[portion][2][component] = output_mapped(sum, flags);
			}
		}
		/* written back only after both portions have read */
		for (portion = 0; portion < 2; portion++)
		{
			DWORD outputs = portion ? state[D3DRS_PSALPHAOUTPUTS0 + stage] : state[D3DRS_PSRGBOUTPUTS0 + stage];
			unsigned long flags = outputs >> 12;
			unsigned long destinations[3];
			int which;

			destinations[0] = (outputs >> 4) & 0x0f;
			destinations[1] = outputs & 0x0f;
			destinations[2] = (outputs >> 8) & 0x0f;
			for (which = 0; which < 3; which++)
			{
				unsigned long reg = destinations[which];

				/* (only v0, v1, t0 to t3, r0 and r1 take results) */
				if (reg != _register_v0 && reg != _register_v1 && (reg < _register_t0 || reg > _register_r1))
					continue;
				if (portion)
				{
					registers[reg][3] = products[1][which][0];
				}
				else
				{
					for (component = 0; component < 3; component++)
						registers[reg][component] = products[0][which][component];
					if ((which == 0 && (flags & 0x80)) || (which == 1 && (flags & 0x40)))
						registers[reg][3] = products[0][which][2];
				}
			}
		}
	}
	if (final_abcd == 0 && final_efg == 0)
	{
		for (component = 0; component < 4; component++)
			result[component] = unit_clamp(registers[_register_r0][component]);
		return;
	}
	retro_color_to_floats(state[D3DRS_PSFINALCOMBINERCONSTANT0], registers[_register_c0]);
	retro_color_to_floats(state[D3DRS_PSFINALCOMBINERCONSTANT1], registers[_register_c1]);
	{
		unsigned long settings = final_efg & 0xff;
		float e[3], f[3], fa[3], fb[3], fc[3], fd[3], fg[3];

		final_input((const float (*)[4])registers, (final_efg >> 24) & 0xff, FALSE, e);
		final_input((const float (*)[4])registers, (final_efg >> 16) & 0xff, FALSE, f);
		for (component = 0; component < 3; component++)
		{
			float v1 = unit_clamp(registers[_register_v1][component]);
			float r0 = unit_clamp(registers[_register_r0][component]);
			float sum = ((settings & 0x40) ? 1.0f - v1 : v1) + ((settings & 0x20) ? 1.0f - r0 : r0);

			registers[_register_ef_product][component] = e[component] * f[component];
			registers[_register_v1r0_sum][component] = (settings & 0x80) ? unit_clamp(sum) : sum;
		}
		registers[_register_ef_product][3] = registers[_register_v1r0_sum][3] = 0.0f;
		final_input((const float (*)[4])registers, (final_abcd >> 24) & 0xff, FALSE, fa);
		final_input((const float (*)[4])registers, (final_abcd >> 16) & 0xff, FALSE, fb);
		final_input((const float (*)[4])registers, (final_abcd >> 8) & 0xff, FALSE, fc);
		final_input((const float (*)[4])registers, final_abcd & 0xff, FALSE, fd);
		final_input((const float (*)[4])registers, (final_efg >> 8) & 0xff, TRUE, fg);
		for (component = 0; component < 3; component++)
			result[component] = unit_clamp(fa[component] * fb[component] + (1.0f - fa[component]) * fc[component] + fd[component]);
		result[3] = unit_clamp(fg[0]);
	}
}

/* the inputs of a vertex, its textures given */
static void vertex_inputs(const struct retro_vertex *vertex, float *diffuse, float *specular)
{
	int component;

	for (component = 0; component < 4; component++)
	{
		diffuse[component] = vertex->diffuse[component] * (1.0f / 255.0f);
		specular[component] = vertex->specular[component] * (1.0f / 255.0f);
	}
}

/* the textures as the evaluation takes them: white where a unit samples
the stage (it multiplies its texture in), the stage's passthru coordinate
or white for a stage the units leave out (a cube map, a fifth texture) */
static void textures_white(float (*textures)[4])
{
	int stage, component;

	for (stage = 0; stage < 4; stage++)
		for (component = 0; component < 4; component++)
			textures[stage][component] = 1.0f;
}

static float color_difference(const float *a, const float *b)
{
	float difference = 0.0f;
	int component;

	for (component = 0; component < 4; component++)
		difference += a[component] > b[component] ? a[component] - b[component] : b[component] - a[component];
	return difference;
}

/* how a unit can give the stage's texture, judged at the draw's first
vertex: multiplied in, when the result changes with it while the other
textures are white (t0 * t1 * v0, the most); added, when it changes with it
only while the others are black (t0 + t1 + t2: the menu's stars, which
white textures saturate whatever one of them is); or not at all */
enum
{
	_stage_unused,
	_stage_modulate,
	_stage_add
};

/* the textures' values in the per-vertex evaluation (retro_pixel_vertex_colors):
white for a texture multiplied in (or one no unit gives), black for one added */
static float vertex_textures[4][4];

static int stage_kind(const struct retro_vertex *vertex, int stage)
{
	float diffuse[4], specular[4], textures[4][4], with[4], without[4];
	int other, component;

	vertex_inputs(vertex, diffuse, specular);
	textures_white(textures);
	combiners_run(diffuse, specular, vertex->fog, (const float (*)[4])textures, with);
	for (component = 0; component < 4; component++)
		textures[stage][component] = 0.0f;
	combiners_run(diffuse, specular, vertex->fog, (const float (*)[4])textures, without);
	if (color_difference(with, without) > 0.004f)
		return _stage_modulate;
	for (other = 0; other < 4; other++)
		for (component = 0; component < 4; component++)
			textures[other][component] = 0.0f;
	combiners_run(diffuse, specular, vertex->fog, (const float (*)[4])textures, without);
	for (component = 0; component < 4; component++)
		textures[stage][component] = 1.0f;
	combiners_run(diffuse, specular, vertex->fog, (const float (*)[4])textures, with);
	return color_difference(with, without) > 0.004f ? _stage_add : _stage_unused;
}

static void retro_pixel_vertex_colors_by_combiners(struct retro_vertex *vertices, unsigned long count);

/* ---------- the draws whose combiners are a line (retro_affine.c) */

/* the lines found for the shaders seen, by everything the combiners read (the shader's words, the
constants, the fog color and the textures' stand-ins), so that a draw with the same state again does not
look again; the oldest is replaced */
#define AFFINE_CACHE 32
/* (a draw with fewer vertices is run on the combiners: finding the line costs about this many vertices) */
#define AFFINE_MINIMUM_VERTICES 64

struct affine_entry
{
	DWORD words[54];
	float textures[4][4];
	struct retro_affine line;
};

static struct affine_entry affine_entries[AFFINE_CACHE];
static unsigned long affine_known, affine_next, affine_logged;

static void affine_words(DWORD *words)
{
	DWORD *state = D3D__RenderState;
	unsigned long index, at = 0;

	for (index = 0; index < 8; index++)
	{
		words[at++] = state[D3DRS_PSRGBINPUTS0 + index];
		words[at++] = state[D3DRS_PSALPHAINPUTS0 + index];
		words[at++] = state[D3DRS_PSRGBOUTPUTS0 + index];
		words[at++] = state[D3DRS_PSALPHAOUTPUTS0 + index];
		words[at++] = state[D3DRS_PSCONSTANT0_0 + index];
		words[at++] = state[D3DRS_PSCONSTANT1_0 + index];
	}
	words[at++] = state[D3DRS_PSCOMBINERCOUNT];
	words[at++] = state[D3DRS_PSFINALCOMBINERINPUTSABCD];
	words[at++] = state[D3DRS_PSFINALCOMBINERINPUTSEFG];
	words[at++] = state[D3DRS_PSFINALCOMBINERCONSTANT0];
	words[at++] = state[D3DRS_PSFINALCOMBINERCONSTANT1];
	words[at++] = state[D3DRS_FOGCOLOR];
}

static void affine_run(const float *diffuse, const float *specular, float fog, float *result, void *context)
{
	(void)context;
	combiners_run(diffuse, specular, fog, (const float (*)[4])vertex_textures, result);
}

/* the line for this draw's shader, or NULL when it is not one */
static const struct retro_affine *affine_for_draw(void)
{
	struct affine_entry *entry;
	DWORD words[54];
	unsigned long index;

	affine_words(words);
	for (index = 0; index < affine_known; index++)
	{
		if (!memcmp(affine_entries[index].words, words, sizeof(words)) &&
			!memcmp(affine_entries[index].textures, vertex_textures, sizeof(vertex_textures)))
			return affine_entries[index].line.valid ? &affine_entries[index].line : NULL;
	}
	index = affine_next;
	affine_next = (affine_next + 1) % AFFINE_CACHE;
	if (affine_known < AFFINE_CACHE)
		affine_known++;
	entry = &affine_entries[index];
	memcpy(entry->words, words, sizeof(words));
	memcpy(entry->textures, vertex_textures, sizeof(vertex_textures));
	retro_affine_fit(&entry->line, affine_run, NULL);
	if (affine_logged < 64)
	{
		affine_logged++;
		platform_log("pixel shader %08lx: %s", retro_pixel_hash(),
			entry->line.valid ? "vertex colors by a scale and an offset (no combiners per vertex)" :
			"vertex colors by the combiners");
	}
	return entry->line.valid ? &entry->line : NULL;
}

void retro_pixel_vertex_colors(struct retro_vertex *vertices, unsigned long count)
{
	if (count >= AFFINE_MINIMUM_VERTICES)
	{
		const struct retro_affine *line = affine_for_draw();

		if (line)
		{
			unsigned long vertex;

			for (vertex = 0; vertex < count; vertex++)
			{
				unsigned char *color = vertices[vertex].diffuse;
				int component;

				for (component = 0; component < 4; component++)
				{
					float value = line->scale[component] * (color[component] * (1.0f / 255.0f)) + line->offset[component];

					value = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
					color[component] = (unsigned char)(value * 255.0f + 0.5f);
				}
			}
			return;
		}
	}
	retro_pixel_vertex_colors_by_combiners(vertices, count);
}

static void retro_pixel_vertex_colors_by_combiners(struct retro_vertex *vertices, unsigned long count)
{
	/* (the combiners' result for the last few inputs: within a draw the
	constants and textures are the same, and most of its vertices share a
	color, which saves running the combiners, slow on the processor, for
	each) */
	enum { REMEMBERED = 8 };
	struct
	{
		unsigned char diffuse[4], specular[4];
		float fog;
		unsigned char result[4];
	} remembered[REMEMBERED];
	unsigned long vertex, known = 0, next = 0;

	for (vertex = 0; vertex < count; vertex++)
	{
		struct retro_vertex *at = &vertices[vertex];
		float diffuse[4], specular[4], result[4];
		unsigned long index;
		int component;

		for (index = 0; index < known; index++)
		{
			if (!memcmp(remembered[index].diffuse, at->diffuse, 4) &&
				!memcmp(remembered[index].specular, at->specular, 4) && remembered[index].fog == at->fog)
				break;
		}
		if (index < known)
		{
			memcpy(at->diffuse, remembered[index].result, 4);
			continue;
		}
		index = next;
		next = (next + 1) % REMEMBERED;
		if (known < REMEMBERED)
			known++;
		memcpy(remembered[index].diffuse, at->diffuse, 4);
		memcpy(remembered[index].specular, at->specular, 4);
		remembered[index].fog = at->fog;
		vertex_inputs(at, diffuse, specular);
		combiners_run(diffuse, specular, at->fog, (const float (*)[4])vertex_textures, result);
		for (component = 0; component < 4; component++)
			at->diffuse[component] = (unsigned char)(result[component] * 255.0f + 0.5f);
		memcpy(remembered[index].result, at->diffuse, 4);
	}
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
	if (RGL.rgl_glActiveTextureARB)
		RGL.rgl_glActiveTextureARB(RGL_TEXTURE0 + unit);
}

static void unit_texture(int unit, int stage, const struct retro_texture *texture, RGLint environment)
{
	const DWORD *state = D3D__TextureState[stage];
	BOOL linear = state[D3DTSS_MAGFILTER] != D3DTEXF_POINT;
	BOOL mip = state[D3DTSS_MIPFILTER] != D3DTEXF_NONE;

	unit_select(unit);
	RGL.rgl_glEnable(RGL_TEXTURE_2D);
	RGL.rgl_glBindTexture(RGL_TEXTURE_2D, texture->name);
	RGL.rgl_glTexParameteri(RGL_TEXTURE_2D, RGL_TEXTURE_MAG_FILTER, linear ? RGL_LINEAR : RGL_NEAREST);
	RGL.rgl_glTexParameteri(RGL_TEXTURE_2D, RGL_TEXTURE_MIN_FILTER,
		!mip ? (linear ? RGL_LINEAR : RGL_NEAREST) :
		state[D3DTSS_MIPFILTER] == D3DTEXF_POINT ? (linear ? RGL_LINEAR_MIPMAP_NEAREST : RGL_NEAREST_MIPMAP_NEAREST) :
		(linear ? RGL_LINEAR_MIPMAP_LINEAR : RGL_NEAREST_MIPMAP_LINEAR));
	RGL.rgl_glTexParameteri(RGL_TEXTURE_2D, RGL_TEXTURE_WRAP_S, wrap_mode(state[D3DTSS_ADDRESSU]));
	RGL.rgl_glTexParameteri(RGL_TEXTURE_2D, RGL_TEXTURE_WRAP_T, wrap_mode(state[D3DTSS_ADDRESSV]));
	RGL.rgl_glTexEnvi(RGL_TEXTURE_ENV, RGL_TEXTURE_ENV_MODE, environment);
}

int retro_pixel_prepare(struct retro_pixel_setup *setup, const struct retro_vertex *first_vertex)
{
	unsigned long registers = shader_registers();
	DWORD modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	int kinds[D3DTSS_MAXSTAGES];
	int stage, unit = 0, pass;

	shader_log(registers);
	memset(setup, 0, sizeof(*setup));
	setup->unit_stage[0] = setup->unit_stage[1] = setup->unit_stage[2] = setup->unit_stage[3] = -1;
	textures_white(vertex_textures);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		DWORD mode = (modes >> (stage * 5)) & 0x1f;

		kinds[stage] = _stage_unused;
		if (!(registers & (1UL << (_register_t0 + stage))) || mode != _mode_project2d)
			continue;
		kinds[stage] = first_vertex ? stage_kind(first_vertex, stage) : _stage_modulate;
		if (kinds[stage] == _stage_add && !retro_gl_capabilities.env_add)
			kinds[stage] = _stage_unused;
		/* (an added texture is black in the vertices' color, given by a unit
		or, when none is left, left out) */
		if (kinds[stage] == _stage_add || (first_vertex && kinds[stage] == _stage_unused))
		{
			int component;

			for (component = 0; component < 4; component++)
				vertex_textures[stage][component] = 0.0f;
		}
	}
	/* (the textures multiplied in first, into the vertices' color, then the
	added ones: base * t0 + t1 + t2) */
	for (pass = _stage_modulate; pass <= _stage_add; pass++)
	{
		for (stage = 0; stage < D3DTSS_MAXSTAGES && unit < retro_gl_capabilities.texture_units; stage++)
		{
			struct retro_texture texture;

			if (kinds[stage] != pass)
				continue;
			if (!retro_texture_get(retro_device_texture(stage), retro_device_palette(stage), &texture))
				continue;
			unit_texture(unit, stage, &texture, pass == _stage_add ? RGL_ADD : RGL_MODULATE);
			setup->unit_stage[unit] = stage;
			setup->texture_scale[unit][0] = texture.coordinate_scale[0];
			setup->texture_scale[unit][1] = texture.coordinate_scale[1];
			unit++;
		}
	}
	setup->units = unit;
	/* (the units the draw leaves out, off) */
	for (stage = unit; stage < retro_gl_capabilities.texture_units; stage++)
	{
		unit_select(stage);
		RGL.rgl_glDisable(RGL_TEXTURE_2D);
	}
	unit_select(0);
	setup->diffuse = TRUE;
	setup->passes = 1;
	return TRUE;
}
