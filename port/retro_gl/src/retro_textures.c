/*
RETRO_TEXTURES.C

The Xbox's textures for OpenGL 1.1. An Xbox texture is a Direct3D header
(Common, Data, Lock, Format, Size) over texels in guest memory, swizzled
unless it is linear (port/linux/src/xbox_textures.c, from which the format
table, the description and the texel decoding are taken). Here each is
decoded to RGBA and made what a card of the time takes:
- a power of two each way (OpenGL 1.1 has nothing else), resampled;
- no larger than the card's largest (256 on a Voodoo3), halved until it fits;
- 16 bits a texel, the driver's choice of RGB5, RGB5_A1 or RGBA4 by what the
  texture's alpha holds;
- its mip levels made here, from the resampled texture, when it has them.
A texture stays as uploaded until a page it was read from is written
(memory_watch, as the OpenGL 4.5 renderer does). Cube maps, volume
textures and render targets are left out: a stage that would sample one is
dropped (retro_pixel.c).

The menus' bitmaps drawn from PNGs (menu_files.c, menu_art_texture) are
decoded here too (hud_hires_png_texture, as port/linux/src/hud_hires.c does).
*/

#include "retro_internal.h"
#include "xgpu.h"
#include "menu_files.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* ---------- formats (as xbox_textures.c) */

enum texel_kind
{
	_texel_unknown,
	_texel_a8r8g8b8, _texel_x8r8g8b8, _texel_r5g6b5, _texel_a1r5g5b5, _texel_x1r5g5b5, _texel_a4r4g4b4,
	_texel_l8, _texel_al8, _texel_a8, _texel_a8l8, _texel_p8, _texel_g8b8, _texel_r8b8, _texel_r6g5b5,
	_texel_l16, _texel_v16u16, _texel_a8b8g8r8, _texel_b8g8r8a8, _texel_r8g8b8a8, _texel_r5g5b5a1,
	_texel_r4g4b4a4, _texel_yuy2, _texel_uyvy, _texel_d24s8, _texel_d16,
	_texel_dxt1, _texel_dxt3, _texel_dxt5,
};

struct format_information
{
	unsigned char kind;
	unsigned char bytes; /* per texel; per 4x4 block for DXT */
	unsigned char linear;
};

static struct format_information format_information(DWORD format)
{
	static const struct format_information table[0x42] =
	{
		[0x00] = { _texel_l8, 1, 0 },
		[0x01] = { _texel_al8, 1, 0 },
		[0x02] = { _texel_a1r5g5b5, 2, 0 },
		[0x03] = { _texel_x1r5g5b5, 2, 0 },
		[0x04] = { _texel_a4r4g4b4, 2, 0 },
		[0x05] = { _texel_r5g6b5, 2, 0 },
		[0x06] = { _texel_a8r8g8b8, 4, 0 },
		[0x07] = { _texel_x8r8g8b8, 4, 0 },
		[0x0b] = { _texel_p8, 1, 0 },
		[0x0c] = { _texel_dxt1, 8, 0 },
		[0x0e] = { _texel_dxt3, 16, 0 },
		[0x0f] = { _texel_dxt5, 16, 0 },
		[0x10] = { _texel_a1r5g5b5, 2, 1 },
		[0x11] = { _texel_r5g6b5, 2, 1 },
		[0x12] = { _texel_a8r8g8b8, 4, 1 },
		[0x13] = { _texel_l8, 1, 1 },
		[0x16] = { _texel_r8b8, 2, 1 },
		[0x17] = { _texel_g8b8, 2, 1 },
		[0x19] = { _texel_a8, 1, 0 },
		[0x1a] = { _texel_a8l8, 2, 0 },
		[0x1b] = { _texel_al8, 1, 1 },
		[0x1c] = { _texel_x1r5g5b5, 2, 1 },
		[0x1d] = { _texel_a4r4g4b4, 2, 1 },
		[0x1e] = { _texel_x8r8g8b8, 4, 1 },
		[0x1f] = { _texel_a8, 1, 1 },
		[0x20] = { _texel_a8l8, 2, 1 },
		[0x24] = { _texel_yuy2, 2, 1 },
		[0x25] = { _texel_uyvy, 2, 1 },
		[0x27] = { _texel_r6g5b5, 2, 0 },
		[0x28] = { _texel_g8b8, 2, 0 },
		[0x29] = { _texel_r8b8, 2, 0 },
		[0x2a] = { _texel_d24s8, 4, 0 },
		[0x2b] = { _texel_d24s8, 4, 0 },
		[0x2c] = { _texel_d16, 2, 0 },
		[0x2d] = { _texel_d16, 2, 0 },
		[0x2e] = { _texel_d24s8, 4, 1 },
		[0x2f] = { _texel_d24s8, 4, 1 },
		[0x30] = { _texel_d16, 2, 1 },
		[0x31] = { _texel_d16, 2, 1 },
		[0x32] = { _texel_l16, 2, 0 },
		[0x33] = { _texel_v16u16, 4, 0 },
		[0x35] = { _texel_l16, 2, 1 },
		[0x36] = { _texel_v16u16, 4, 1 },
		[0x37] = { _texel_r6g5b5, 2, 1 },
		[0x38] = { _texel_r5g5b5a1, 2, 0 },
		[0x39] = { _texel_r4g4b4a4, 2, 0 },
		[0x3a] = { _texel_a8b8g8r8, 4, 0 },
		[0x3b] = { _texel_b8g8r8a8, 4, 0 },
		[0x3c] = { _texel_r8g8b8a8, 4, 0 },
		[0x3d] = { _texel_r5g5b5a1, 2, 1 },
		[0x3e] = { _texel_r4g4b4a4, 2, 1 },
		[0x3f] = { _texel_a8b8g8r8, 4, 1 },
		[0x40] = { _texel_b8g8r8a8, 4, 1 },
		[0x41] = { _texel_r8g8b8a8, 4, 1 },
	};
	struct format_information unknown = { _texel_a8r8g8b8, 4, 0 };

	if (format < sizeof(table) / sizeof(table[0]) && table[format].kind != _texel_unknown)
		return table[format];
	return unknown;
}

static BOOL kind_compressed(unsigned char kind)
{
	return kind == _texel_dxt1 || kind == _texel_dxt3 || kind == _texel_dxt5;
}

/* ---------- geometry of a texture in memory (as xbox_textures.c; these
four are what d3d8_resources.c calls) */

static unsigned long level_dimension(unsigned long base, unsigned long level)
{
	unsigned long value = base >> level;

	return value ? value : 1;
}

void xgpu_texture_describe(DWORD format_word, DWORD size_word, struct xgpu_texture_description *description)
{
	struct format_information information;

	memset(description, 0, sizeof(*description));
	description->format = (format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT;
	information = format_information(description->format);
	description->cube_map = (format_word & D3DFORMAT_CUBEMAP) != 0;
	description->compressed = kind_compressed(information.kind);
	if (size_word)
	{
		description->width = (size_word & D3DSIZE_WIDTH_MASK) + 1;
		description->height = ((size_word & D3DSIZE_HEIGHT_MASK) >> D3DSIZE_HEIGHT_SHIFT) + 1;
		description->depth = 1;
		description->levels = 1;
		description->pitch = (((size_word & D3DSIZE_PITCH_MASK) >> D3DSIZE_PITCH_SHIFT) + 1) * D3DTEXTURE_PITCH_ALIGNMENT;
		description->linear = TRUE;
	}
	else
	{
		description->width = 1UL << ((format_word & D3DFORMAT_USIZE_MASK) >> D3DFORMAT_USIZE_SHIFT);
		description->height = 1UL << ((format_word & D3DFORMAT_VSIZE_MASK) >> D3DFORMAT_VSIZE_SHIFT);
		description->depth = 1UL << ((format_word & D3DFORMAT_PSIZE_MASK) >> D3DFORMAT_PSIZE_SHIFT);
		description->levels = (format_word & D3DFORMAT_MIPMAP_MASK) >> D3DFORMAT_MIPMAP_SHIFT;
		if (!description->levels)
			description->levels = 1;
		description->linear = information.linear;
		description->pitch = description->width * information.bytes;
	}
	if ((format_word & D3DFORMAT_DIMENSION_MASK) >> D3DFORMAT_DIMENSION_SHIFT != 3)
		description->depth = 1;
}

static unsigned long level_bytes(const struct xgpu_texture_description *description, unsigned long level)
{
	struct format_information information = format_information(description->format);
	unsigned long width = level_dimension(description->width, level);
	unsigned long height = level_dimension(description->height, level);
	unsigned long depth = level_dimension(description->depth, level);

	if (description->compressed)
		return ((width + 3) / 4) * ((height + 3) / 4) * information.bytes * depth;
	if (description->linear)
		return description->pitch * height;
	return width * height * depth * information.bytes;
}

unsigned long xgpu_texture_level_offset(const struct xgpu_texture_description *description, unsigned long level)
{
	unsigned long offset = 0;
	unsigned long index;

	for (index = 0; index < level && index < description->levels; index++)
		offset += level_bytes(description, index);
	return offset;
}

unsigned long xgpu_texture_face_size(const struct xgpu_texture_description *description)
{
	unsigned long size = xgpu_texture_level_offset(description, description->levels);

	if (description->cube_map)
		size = (size + D3DTEXTURE_CUBEFACE_ALIGNMENT - 1) & ~(unsigned long)(D3DTEXTURE_CUBEFACE_ALIGNMENT - 1);
	return size;
}

unsigned long xgpu_texture_level_pitch(const struct xgpu_texture_description *description, unsigned long level)
{
	struct format_information information = format_information(description->format);

	if (description->linear)
		return description->pitch;
	if (description->compressed)
		return ((level_dimension(description->width, level) + 3) / 4) * information.bytes;
	return level_dimension(description->width, level) * information.bytes;
}

/* ---------- decoding into ARGB words (as xbox_textures.c) */

struct swizzle_masks
{
	unsigned long x, y, z;
};

static struct swizzle_masks swizzle_masks(unsigned long width, unsigned long height, unsigned long depth)
{
	struct swizzle_masks masks = { 0, 0, 0 };
	unsigned long bit = 1, mask_bit = 1;
	BOOL done;

	do
	{
		done = TRUE;
		if (bit < width)
		{
			masks.x |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		if (bit < height)
		{
			masks.y |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		if (bit < depth)
		{
			masks.z |= mask_bit;
			mask_bit <<= 1;
			done = FALSE;
		}
		bit <<= 1;
	} while (!done);
	return masks;
}

static unsigned long spread(unsigned long mask, unsigned long value)
{
	unsigned long result = 0, bit = 1;

	while (value && bit)
	{
		if (mask & bit)
		{
			if (value & 1)
				result |= bit;
			value >>= 1;
		}
		bit <<= 1;
	}
	return result;
}

static unsigned long expand5(unsigned long v) { return (v << 3) | (v >> 2); }
static unsigned long expand6(unsigned long v) { return (v << 2) | (v >> 4); }
static unsigned long expand4(unsigned long v) { return v * 0x11; }

static unsigned long argb(unsigned long a, unsigned long r, unsigned long g, unsigned long b)
{
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static unsigned char clamp_byte(long value)
{
	return (unsigned char)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static unsigned long yuv_to_argb(long y, long u, long v)
{
	long c = y - 16, d = u - 128, e = v - 128;

	return argb(255, clamp_byte((298 * c + 409 * e + 128) >> 8),
		clamp_byte((298 * c - 100 * d - 208 * e + 128) >> 8),
		clamp_byte((298 * c + 516 * d + 128) >> 8));
}

#define TEXEL16(source) ((unsigned long)(source)[0] | ((unsigned long)(source)[1] << 8))
#define TEXEL32(source) (TEXEL16(source) | ((unsigned long)(source)[2] << 16) | ((unsigned long)(source)[3] << 24))

static unsigned long convert_texel(unsigned char kind, const unsigned char *source, const D3DCOLOR *palette,
	unsigned long x, const unsigned char *row)
{
	switch (kind)
	{
	case _texel_a8r8g8b8: return TEXEL32(source);
	case _texel_x8r8g8b8: return TEXEL32(source) | 0xff000000UL;
	case _texel_r5g6b5: return argb(255, expand5(TEXEL16(source) >> 11), expand6((TEXEL16(source) >> 5) & 0x3f), expand5(TEXEL16(source) & 0x1f));
	case _texel_a1r5g5b5: return argb((TEXEL16(source) & 0x8000) ? 255 : 0, expand5((TEXEL16(source) >> 10) & 0x1f), expand5((TEXEL16(source) >> 5) & 0x1f), expand5(TEXEL16(source) & 0x1f));
	case _texel_x1r5g5b5: return argb(255, expand5((TEXEL16(source) >> 10) & 0x1f), expand5((TEXEL16(source) >> 5) & 0x1f), expand5(TEXEL16(source) & 0x1f));
	case _texel_a4r4g4b4: return argb(expand4(TEXEL16(source) >> 12), expand4((TEXEL16(source) >> 8) & 0xf), expand4((TEXEL16(source) >> 4) & 0xf), expand4(TEXEL16(source) & 0xf));
	case _texel_l8: return argb(255, source[0], source[0], source[0]);
	case _texel_al8: return argb(source[0], source[0], source[0], source[0]);
	case _texel_a8: return argb(source[0], 255, 255, 255);
	case _texel_a8l8: return argb(source[1], source[0], source[0], source[0]);
	case _texel_p8: return palette ? palette[source[0]] : argb(255, source[0], source[0], source[0]);
	case _texel_g8b8: return argb(255, source[0], source[1], 0);
	case _texel_r8b8: return argb(255, source[1], 0, source[0]);
	case _texel_r6g5b5: return argb(255, expand6(TEXEL16(source) >> 10), expand5((TEXEL16(source) >> 5) & 0x1f), expand5(TEXEL16(source) & 0x1f));
	case _texel_l16: return argb(255, source[1], source[1], source[1]);
	case _texel_v16u16: return argb(255, source[1], source[3], 0);
	case _texel_a8b8g8r8: return argb(source[3], source[0], source[1], source[2]);
	case _texel_b8g8r8a8: return argb(source[0], source[1], source[2], source[3]);
	case _texel_r8g8b8a8: return argb(source[0], source[3], source[2], source[1]);
	case _texel_r5g5b5a1: return argb((TEXEL16(source) & 1) ? 255 : 0, expand5(TEXEL16(source) >> 11), expand5((TEXEL16(source) >> 6) & 0x1f), expand5((TEXEL16(source) >> 1) & 0x1f));
	case _texel_r4g4b4a4: return argb(expand4(TEXEL16(source) & 0xf), expand4(TEXEL16(source) >> 12), expand4((TEXEL16(source) >> 8) & 0xf), expand4((TEXEL16(source) >> 4) & 0xf));
	case _texel_yuy2:
	{
		const unsigned char *pair = row + (x & ~1UL) * 2;

		return yuv_to_argb(pair[(x & 1) ? 2 : 0], pair[1], pair[3]);
	}
	case _texel_uyvy:
	{
		const unsigned char *pair = row + (x & ~1UL) * 2;

		return yuv_to_argb(pair[(x & 1) ? 3 : 1], pair[0], pair[2]);
	}
	case _texel_d24s8: return argb(255, source[3], source[3], source[3]);
	case _texel_d16: return argb(255, source[1], source[1], source[1]);
	default: return TEXEL32(source);
	}
}

/* level 0 of a 2D texture, uncompressed, into ARGB words */
static BOOL decode_level(const struct xgpu_texture_description *description, const unsigned char *source,
	const D3DCOLOR *palette, unsigned long *destination)
{
	struct format_information information = format_information(description->format);
	unsigned long width = description->width, height = description->height;
	unsigned long x, y;

	if (description->linear)
	{
		unsigned long row_texels = information.bytes ? description->pitch / information.bytes : 0;

		if (information.kind == _texel_yuy2 || information.kind == _texel_uyvy)
			row_texels &= ~1UL;
		for (y = 0; y < height; y++)
		{
			const unsigned char *row = source + y * description->pitch;

			for (x = 0; x < width; x++)
				destination[y * width + x] = x < row_texels ?
					convert_texel(information.kind, row + x * information.bytes, palette, x, row) : 0;
		}
		return TRUE;
	}
	{
		struct swizzle_masks masks = swizzle_masks(width, height, 1);
		unsigned long *x_offsets = malloc(width * sizeof(unsigned long));

		if (!x_offsets)
			return FALSE;
		for (x = 0; x < width; x++)
			x_offsets[x] = spread(masks.x, x);
		for (y = 0; y < height; y++)
		{
			unsigned long y_offset = spread(masks.y, y);

			for (x = 0; x < width; x++)
			{
				const unsigned char *texel = source + (x_offsets[x] | y_offset) * information.bytes;

				destination[y * width + x] = convert_texel(information.kind, texel, palette, x, texel);
			}
		}
		free(x_offsets);
	}
	return TRUE;
}

static unsigned long color565(unsigned long value)
{
	return argb(255, expand5(value >> 11), expand6((value >> 5) & 0x3f), expand5(value & 0x1f));
}

static unsigned long mix(unsigned long a, unsigned long b, unsigned long weight_a, unsigned long weight_b,
	unsigned long divisor)
{
	unsigned long result = 0;
	int shift;

	for (shift = 0; shift < 24; shift += 8)
	{
		unsigned long channel = (((a >> shift) & 0xff) * weight_a + ((b >> shift) & 0xff) * weight_b) / divisor;

		result |= channel << shift;
	}
	return result | 0xff000000UL;
}

static void dxt_color_block(const unsigned char *block, BOOL dxt1, unsigned long colors[16])
{
	unsigned long c0 = block[0] | (block[1] << 8);
	unsigned long c1 = block[2] | (block[3] << 8);
	unsigned long palette[4];
	unsigned long bits = block[4] | (block[5] << 8) | ((unsigned long)block[6] << 16) | ((unsigned long)block[7] << 24);
	int index;

	palette[0] = color565(c0);
	palette[1] = color565(c1);
	if (c0 > c1 || !dxt1)
	{
		palette[2] = mix(palette[0], palette[1], 2, 1, 3);
		palette[3] = mix(palette[0], palette[1], 1, 2, 3);
	}
	else
	{
		palette[2] = mix(palette[0], palette[1], 1, 1, 2);
		palette[3] = 0;
	}
	for (index = 0; index < 16; index++)
		colors[index] = palette[(bits >> (index * 2)) & 3];
}

static void dxt_decode_level(unsigned char kind, const unsigned char *source, unsigned long width, unsigned long height,
	unsigned long *destination)
{
	unsigned long blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
	unsigned long block_bytes = kind == _texel_dxt1 ? 8 : 16;
	unsigned long bx, by, x, y;

	for (by = 0; by < blocks_y; by++)
	{
		for (bx = 0; bx < blocks_x; bx++)
		{
			const unsigned char *block = source + (by * blocks_x + bx) * block_bytes;
			unsigned long colors[16];
			unsigned long alpha[16];
			int index;

			if (kind == _texel_dxt1)
			{
				dxt_color_block(block, TRUE, colors);
				for (index = 0; index < 16; index++)
					alpha[index] = colors[index] >> 24;
			}
			else
			{
				dxt_color_block(block + 8, FALSE, colors);
				if (kind == _texel_dxt3)
				{
					for (index = 0; index < 16; index++)
						alpha[index] = expand4((block[index / 2] >> ((index & 1) * 4)) & 0xf);
				}
				else
				{
					unsigned long a0 = block[0], a1 = block[1], values[8];
					unsigned long long bits = 0;
					int bit;

					for (bit = 0; bit < 6; bit++)
						bits |= (unsigned long long)block[2 + bit] << (bit * 8);
					values[0] = a0;
					values[1] = a1;
					if (a0 > a1)
					{
						for (index = 2; index < 8; index++)
							values[index] = ((8 - index) * a0 + (index - 1) * a1) / 7;
					}
					else
					{
						for (index = 2; index < 6; index++)
							values[index] = ((6 - index) * a0 + (index - 1) * a1) / 5;
						values[6] = 0;
						values[7] = 255;
					}
					for (index = 0; index < 16; index++)
						alpha[index] = values[(bits >> (index * 3)) & 7];
				}
			}
			for (y = 0; y < 4; y++)
			{
				for (x = 0; x < 4; x++)
				{
					unsigned long px = bx * 4 + x, py = by * 4 + y;

					if (px < width && py < height)
						destination[py * width + px] = (colors[y * 4 + x] & 0x00ffffffUL) | (alpha[y * 4 + x] << 24);
				}
			}
		}
	}
}

/* ---------- what the card takes */

static unsigned long power_of_two_at_most(unsigned long value)
{
	unsigned long result = 1;

	while (result * 2 <= value)
		result *= 2;
	return result;
}

/* the texture as RGBA bytes, a power of two each way no larger than the
card's largest: halved by averaging while it is twice that or more, else
resampled (point sampled: a linear texture's odd sizes, the menus' art) */
static unsigned char *texels_fit(const unsigned long *argb_texels, unsigned long width, unsigned long height,
	unsigned long *fit_width, unsigned long *fit_height)
{
	unsigned long maximum = (unsigned long)retro_gl_capabilities.maximum_texture_size;
	unsigned long target_width = power_of_two_at_most(width), target_height = power_of_two_at_most(height);
	unsigned long x, y;
	unsigned char *rgba;

	if (target_width < width && target_width * 2 <= maximum)
		target_width *= 2;
	if (target_height < height && target_height * 2 <= maximum)
		target_height *= 2;
	while (target_width > maximum)
		target_width /= 2;
	while (target_height > maximum)
		target_height /= 2;
	rgba = malloc(target_width * target_height * 4);
	if (!rgba)
		return NULL;
	/* (each texel the average of the source texels it covers, at most
	4x4 of them: enough for a texture four times the card's largest) */
	for (y = 0; y < target_height; y++)
	{
		unsigned long y0 = y * height / target_height, y1 = (y + 1) * height / target_height;

		if (y1 <= y0)
			y1 = y0 + 1;
		if (y1 - y0 > 4)
			y0 = y1 - 4;
		for (x = 0; x < target_width; x++)
		{
			unsigned long x0 = x * width / target_width, x1 = (x + 1) * width / target_width;
			unsigned long sum[4] = { 0, 0, 0, 0 }, count = 0, sx, sy;
			unsigned char *out = rgba + (y * target_width + x) * 4;

			if (x1 <= x0)
				x1 = x0 + 1;
			if (x1 - x0 > 4)
				x0 = x1 - 4;
			for (sy = y0; sy < y1; sy++)
			{
				for (sx = x0; sx < x1; sx++)
				{
					unsigned long texel = argb_texels[sy * width + sx];

					sum[0] += (texel >> 16) & 0xff;
					sum[1] += (texel >> 8) & 0xff;
					sum[2] += texel & 0xff;
					sum[3] += texel >> 24;
					count++;
				}
			}
			out[0] = (unsigned char)(sum[0] / count);
			out[1] = (unsigned char)(sum[1] / count);
			out[2] = (unsigned char)(sum[2] / count);
			out[3] = (unsigned char)(sum[3] / count);
		}
	}
	*fit_width = target_width;
	*fit_height = target_height;
	return rgba;
}

/* 0 all opaque, 1 opaque or clear, 2 partly transparent too */
static int alpha_kind(const unsigned char *rgba, unsigned long count)
{
	int kind = 0;
	unsigned long index;

	for (index = 0; index < count; index++)
	{
		unsigned char alpha = rgba[index * 4 + 3];

		if (alpha != 255)
		{
			if (alpha != 0)
				return 2;
			kind = 1;
		}
	}
	return kind;
}

/* uploads the RGBA texels (and, with mipmaps, levels made from them by
averaging, which reuses the buffer) into the bound texture */
static void upload_rgba(unsigned char *rgba, unsigned long width, unsigned long height, int mipmaps, int alpha)
{
	RGLint internal = alpha == 0 ? RGL_RGB5 : alpha == 1 ? RGL_RGB5_A1 : RGL_RGBA4;
	RGLint level = 0;

	RGL.rgl_glPixelStorei(RGL_UNPACK_ALIGNMENT, 1);
	RGL.rgl_glTexImage2D(RGL_TEXTURE_2D, 0, internal, (RGLsizei)width, (RGLsizei)height, 0, RGL_RGBA,
		RGL_UNSIGNED_BYTE, rgba);
	while (mipmaps && (width > 1 || height > 1))
	{
		unsigned long next_width = width > 1 ? width / 2 : 1, next_height = height > 1 ? height / 2 : 1;
		unsigned long x, y, channel;

		for (y = 0; y < next_height; y++)
		{
			for (x = 0; x < next_width; x++)
			{
				unsigned long x0 = x * width / next_width, y0 = y * height / next_height;
				unsigned long x1 = width > 1 ? x0 + 1 : x0, y1 = height > 1 ? y0 + 1 : y0;

				for (channel = 0; channel < 4; channel++)
				{
					rgba[(y * next_width + x) * 4 + channel] = (unsigned char)((
						rgba[(y0 * width + x0) * 4 + channel] + rgba[(y0 * width + x1) * 4 + channel] +
						rgba[(y1 * width + x0) * 4 + channel] + rgba[(y1 * width + x1) * 4 + channel] + 2) / 4);
				}
			}
		}
		width = next_width;
		height = next_height;
		RGL.rgl_glTexImage2D(RGL_TEXTURE_2D, ++level, internal, (RGLsizei)width, (RGLsizei)height, 0, RGL_RGBA,
			RGL_UNSIGNED_BYTE, rgba);
	}
}

/* ---------- the cache */

#define CACHE_BUCKET_COUNT 1024

struct texture_entry
{
	struct texture_entry *next;
	DWORD data, format, size;
	const void *palette;
	unsigned long palette_hash;
	unsigned long address, bytes;
	unsigned long generation;
	unsigned long last_used;
	struct retro_texture texture;
	/* nothing could be made of it (an unsupported kind): not tried again */
	BOOL failed;
};

static struct texture_entry *buckets[CACHE_BUCKET_COUNT];
static unsigned long current_frame;
static struct
{
	unsigned long uploads, upload_bytes, entries;
} statistics;

static unsigned long bucket_index(DWORD data, DWORD format, DWORD size)
{
	return ((data >> 8) ^ (data >> 16) ^ format ^ size) % CACHE_BUCKET_COUNT;
}

static unsigned long palette_hash(const D3DPalette *palette)
{
	const D3DCOLOR *colors;
	unsigned long hash = 2166136261UL, index;

	if (!palette || !palette->Data)
		return 0;
	colors = PLATFORM_PHYSICAL_TO_VIRTUAL(palette->Data);
	for (index = 0; index < 256; index++)
		hash = (hash ^ colors[index]) * 16777619UL;
	return hash;
}

static void entry_upload(struct texture_entry *entry, const struct xgpu_texture_description *description,
	const D3DPalette *palette)
{
	struct format_information information = format_information(description->format);
	const unsigned char *source = (const unsigned char *)entry->address;
	const D3DCOLOR *colors = palette && palette->Data ? PLATFORM_PHYSICAL_TO_VIRTUAL(palette->Data) : NULL;
	unsigned long *argb_texels = malloc(description->width * description->height * sizeof(unsigned long));
	unsigned long fit_width, fit_height;
	unsigned char *rgba;
	int alpha;

	if (!argb_texels)
	{
		entry->failed = TRUE;
		return;
	}
	if (kind_compressed(information.kind))
		dxt_decode_level(information.kind, source, description->width, description->height, argb_texels);
	else if (!decode_level(description, source, colors, argb_texels))
	{
		free(argb_texels);
		entry->failed = TRUE;
		return;
	}
	rgba = texels_fit(argb_texels, description->width, description->height, &fit_width, &fit_height);
	free(argb_texels);
	if (!rgba)
	{
		entry->failed = TRUE;
		return;
	}
	alpha = alpha_kind(rgba, fit_width * fit_height);
	entry->texture.has_alpha = alpha != 0;
	if (!entry->texture.name)
		RGL.rgl_glGenTextures(1, &entry->texture.name);
	RGL.rgl_glBindTexture(RGL_TEXTURE_2D, entry->texture.name);
	upload_rgba(rgba, fit_width, fit_height, description->levels > 1, alpha);
	free(rgba);
	statistics.uploads++;
	statistics.upload_bytes += fit_width * fit_height * 2;
}

int retro_texture_get(const D3DBaseTexture *texture, const D3DPalette *palette, struct retro_texture *result)
{
	struct xgpu_texture_description description;
	struct texture_entry *entry, **bucket;
	unsigned long hash;

	if (!texture || !texture->Data)
		return FALSE;
	xgpu_texture_describe(texture->Format, texture->Size, &description);
	if (description.cube_map || description.depth > 1 || description.width > 4096 || description.height > 4096)
		return FALSE;
	/* (a menu's bitmap: menu_files.h) */
	{
		unsigned long levels;
		unsigned int art = menu_art_texture(texture->Data, &levels);

		if (art)
		{
			result->name = art;
			result->coordinate_scale[0] = description.linear ? 1.0f / (float)description.width : 1.0f;
			result->coordinate_scale[1] = description.linear ? 1.0f / (float)description.height : 1.0f;
			result->has_alpha = TRUE;
			return TRUE;
		}
	}
	if (retro_device_is_render_target(texture->Data))
		return FALSE;
	hash = format_information(description.format).kind == _texel_p8 ? palette_hash(palette) : 0;
	bucket = &buckets[bucket_index(texture->Data, texture->Format, texture->Size)];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->data == texture->Data && entry->format == texture->Format && entry->size == texture->Size &&
			entry->palette_hash == hash)
		{
			break;
		}
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return FALSE;
		entry->data = texture->Data;
		entry->format = texture->Format;
		entry->size = texture->Size;
		entry->palette_hash = hash;
		entry->address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(texture->Data);
		entry->bytes = level_bytes(&description, 0);
		entry->next = *bucket;
		*bucket = entry;
		statistics.entries++;
	}
	if (entry->failed)
		return FALSE;
	/* made, or written since it was */
	if (!entry->texture.name || memory_watch_generation(entry->address, entry->bytes) > entry->generation)
	{
		entry_upload(entry, &description, palette);
		if (entry->failed)
			return FALSE;
		memory_watch_protect(entry->address, entry->bytes);
		entry->generation = memory_watch_generation(entry->address, entry->bytes);
	}
	entry->last_used = current_frame;
	*result = entry->texture;
	result->coordinate_scale[0] = description.linear ? 1.0f / (float)description.width : 1.0f;
	result->coordinate_scale[1] = description.linear ? 1.0f / (float)description.height : 1.0f;
	return TRUE;
}

void retro_textures_begin_frame(unsigned long frame)
{
	static unsigned long logged_at;

	current_frame = frame;
	if (statistics.uploads && frame - logged_at >= 600)
	{
		platform_log("textures: %lu kept, %lu uploads (%lu KB) in the last %lu frames", statistics.entries,
			statistics.uploads, statistics.upload_bytes / 1024, frame - logged_at);
		statistics.uploads = statistics.upload_bytes = 0;
		logged_at = frame;
	}
}

/* ---------- the menus' PNGs (as port/linux/src/hud_hires.c) */

#define MAXIMUM_DECODED_SIZE (64UL << 20)

static unsigned long big_endian_long(const unsigned char *bytes)
{
	return ((unsigned long)bytes[0] << 24) | ((unsigned long)bytes[1] << 16) |
		((unsigned long)bytes[2] << 8) | bytes[3];
}

static unsigned char paeth(unsigned char left, unsigned char up, unsigned char up_left)
{
	int estimate = (int)left + up - up_left;
	int to_left = abs(estimate - left), to_up = abs(estimate - up), to_up_left = abs(estimate - up_left);

	if (to_left <= to_up && to_left <= to_up_left)
		return left;
	return to_up <= to_up_left ? up : up_left;
}

/* the PNG's texels, RGBA in rows top first; NULL unless 8-bit RGBA, not
interlaced (as tools/hud_assets.py and the menus' art are) */
static unsigned char *png_decode(const unsigned char *data, unsigned long size, unsigned long *png_width,
	unsigned long *png_height)
{
	unsigned long position = 8;
	unsigned long width = size >= 33 ? big_endian_long(data + 16) : 0;
	unsigned long height = size >= 33 ? big_endian_long(data + 20) : 0;
	unsigned long stride = width * 4, filtered_size = height * (stride + 1);
	unsigned char *compressed = NULL, *filtered = NULL, *pixels = NULL;
	unsigned long compressed_size = 0, row, column;
	uLongf inflated_size = filtered_size;
	int result;

	if (size < 33 || memcmp(data, "\x89PNG\r\n\x1a\n", 8) || memcmp(data + 12, "IHDR", 4) ||
		!width || !height || width > 8192 || height > 8192 ||
		data[24] != 8 || data[25] != 6 || data[28] != 0)
		return NULL;
	if (filtered_size + stride * height > MAXIMUM_DECODED_SIZE)
		return NULL;
	*png_width = width;
	*png_height = height;
	compressed = malloc(size);
	while (compressed && position + 12 <= size)
	{
		unsigned long length = big_endian_long(data + position);

		if (length > size - position - 12)
			break;
		if (!memcmp(data + position + 4, "IDAT", 4))
		{
			memcpy(compressed + compressed_size, data + position + 8, length);
			compressed_size += length;
		}
		else if (!memcmp(data + position + 4, "IEND", 4))
		{
			break;
		}
		position += 12 + length;
	}
	filtered = compressed ? malloc(filtered_size) : NULL;
	pixels = filtered ? malloc(stride * height) : NULL;
	if (!pixels)
		goto failed;
	result = uncompress(filtered, &inflated_size, compressed, compressed_size);
	if ((result != Z_OK && result != Z_BUF_ERROR) || inflated_size != filtered_size)
		goto failed;
	for (row = 0; row < height; row++)
	{
		const unsigned char *line = filtered + row * (stride + 1) + 1;
		unsigned char filter = line[-1];
		unsigned char *out = pixels + row * stride;
		const unsigned char *above = row ? out - stride : NULL;

		if (filter > 4)
			goto failed;
		if (!above && filter == 2)
			filter = 0;
		else if (!above && filter == 4)
			filter = 1;
		switch (filter)
		{
		case 0:
			memcpy(out, line, stride);
			break;
		case 1:
			memcpy(out, line, 4);
			for (column = 4; column < stride; column++)
				out[column] = (unsigned char)(line[column] + out[column - 4]);
			break;
		case 2:
			for (column = 0; column < stride; column++)
				out[column] = (unsigned char)(line[column] + above[column]);
			break;
		case 3:
			for (column = 0; column < 4; column++)
				out[column] = (unsigned char)(line[column] + (above ? above[column] : 0) / 2);
			for (column = 4; column < stride; column++)
				out[column] = (unsigned char)(line[column] +
					((unsigned)out[column - 4] + (above ? above[column] : 0)) / 2);
			break;
		default:
			for (column = 0; column < 4; column++)
				out[column] = (unsigned char)(line[column] + above[column]);
			for (column = 4; column < stride; column++)
				out[column] = (unsigned char)(line[column] + paeth(out[column - 4], above[column], above[column - 4]));
			break;
		}
	}
	free(compressed);
	free(filtered);
	return pixels;

failed:
	free(compressed);
	free(filtered);
	free(pixels);
	return NULL;
}

int retro_device_gl_ready(void);

unsigned int hud_hires_png_texture(const void *png, unsigned long size, unsigned long *levels)
{
	unsigned long width = 0, height = 0, index, fit_width, fit_height;
	unsigned char *pixels = png_decode(png, size, &width, &height);
	unsigned long *argb_texels;
	unsigned char *rgba;
	RGLuint name;

	if (!pixels || !retro_device_gl_ready())
	{
		free(pixels);
		return 0;
	}
	argb_texels = malloc(width * height * sizeof(unsigned long));
	if (!argb_texels)
	{
		free(pixels);
		return 0;
	}
	for (index = 0; index < width * height; index++)
	{
		argb_texels[index] = argb(pixels[index * 4 + 3], pixels[index * 4], pixels[index * 4 + 1],
			pixels[index * 4 + 2]);
	}
	free(pixels);
	rgba = texels_fit(argb_texels, width, height, &fit_width, &fit_height);
	free(argb_texels);
	if (!rgba)
		return 0;
	RGL.rgl_glGenTextures(1, &name);
	RGL.rgl_glBindTexture(RGL_TEXTURE_2D, name);
	upload_rgba(rgba, fit_width, fit_height, TRUE, alpha_kind(rgba, fit_width * fit_height));
	free(rgba);
	*levels = 1;
	for (; fit_width > 1 || fit_height > 1; fit_width /= 2, fit_height /= 2)
		(*levels)++;
	return name;
}
