/*
RETRO_SHIM.C

What the platform layer (port/linux/src) calls of the OpenGL 4.5 renderer's
files that port/retro_gl replaces (port/win98/build.py RETRO_GL_REPLACED),
besides the Direct3D device:
- gl_functions_load and the two OpenGL entry points the platform layer
  calls itself (sdl_platform.c's glGetString, menu_files.c's
  glDeleteTextures), through port/linux/src/gl.h's pointers;
- the high-res HUD and text (hud_hires.c, text_hires.c), which are off: they
  draw from textures of 512 to 2048 texels, past what these cards hold, so
  the game's own HUD bitmaps and fonts are drawn instead;
- Custom Edition HUD meters' channel order (xbox_textures.c), which only the
  high-res HUD reads.
*/

#include "platform.h"
#define GL_FUNCTIONS_DEFINE
#include "gl.h"

#include <SDL3/SDL.h>

/* ---------- OpenGL entry points */

__typeof__(&glGetString) halo_glGetString;
__typeof__(&glDeleteTextures) halo_glDeleteTextures;

int gl_functions_load(void)
{
	halo_glGetString = (__typeof__(halo_glGetString))SDL_GL_GetProcAddress("glGetString");
	halo_glDeleteTextures = (__typeof__(halo_glDeleteTextures))SDL_GL_GetProcAddress("glDeleteTextures");
	return halo_glGetString && halo_glDeleteTextures;
}

void xgpu_gl_texture_deleted(unsigned int texture)
{
	(void)texture;
}

/* ---------- the high-res HUD (off) */

long hud_hires_asset_count(void)
{
	return 0;
}

char const *hud_hires_asset_tag(long asset)
{
	(void)asset;
	return "";
}

long hud_hires_asset_bitmap(long asset)
{
	(void)asset;
	return 0;
}

long hud_hires_asset_fits(long asset, long width, long height)
{
	(void)asset;
	(void)width;
	(void)height;
	return 0;
}

unsigned long hud_hires_asset_sprites(long asset)
{
	(void)asset;
	return 0;
}

int hud_hires_sprites_drawable(long asset, unsigned long address, unsigned long level0_size)
{
	(void)asset;
	(void)address;
	(void)level0_size;
	return 0;
}

void hud_hires_register_placeholder(long asset, unsigned long const *texture)
{
	(void)asset;
	(void)texture;
}

int hud_hires_asset_custom_edition(long asset)
{
	(void)asset;
	return 0;
}

int hud_hires_asset_ce_only(long asset)
{
	(void)asset;
	return 0;
}

long hud_hires_ce_stock_count_get(void)
{
	return 0;
}

char const *hud_hires_ce_stock_tag(long index)
{
	(void)index;
	return "";
}

long hud_hires_ce_stock_bitmap(long index)
{
	(void)index;
	return 0;
}

int hud_hires_ce_stock_matches(long index, unsigned long address, long width, long height, unsigned long size)
{
	(void)index;
	(void)address;
	(void)width;
	(void)height;
	(void)size;
	return 0;
}

void hud_hires_map_loaded(const long *assets, long count)
{
	(void)assets;
	(void)count;
}

void hud_hires_map_unloaded(void)
{
}

/* ---------- the high-res text (off) */

struct text_hires_glyph;

int text_hires_enabled(void)
{
	return 0;
}

int text_hires_covers(long font, unsigned long code)
{
	(void)font;
	(void)code;
	return 0;
}

long text_hires_font(char const *tag_name, float cap_height, short const *letter_widths, float oversample)
{
	(void)tag_name;
	(void)cap_height;
	(void)letter_widths;
	(void)oversample;
	return -1;
}

int text_hires_glyph(long font, unsigned long code, struct text_hires_glyph *glyph)
{
	(void)font;
	(void)code;
	(void)glyph;
	return 0;
}

void text_hires_register_atlas(const unsigned long *texture, unsigned long width, unsigned long height)
{
	(void)texture;
	(void)width;
	(void)height;
}

void text_hires_set_reset_hook(void (*hook)(void))
{
	(void)hook;
}

/* ---------- Custom Edition meters' channels (the high-res HUD's only) */

void halo_custom_edition_hud_meter(int active)
{
	(void)active;
}

void halo_custom_edition_texels_channels(const void *texels, unsigned char channel_order)
{
	(void)texels;
	(void)channel_order;
}

void halo_custom_edition_texels_forget(void)
{
}
