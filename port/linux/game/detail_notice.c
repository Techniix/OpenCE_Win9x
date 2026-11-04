/*
DETAIL_NOTICE.C

A line of text for a couple of seconds at the screen's top left (detail_notice.h),
drawn in the game's terminal font with the overlays. It reads the clock and
draws: nothing of the game's state.
*/

#include "cseries.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "interface/interface.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "detail_notice.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	NOTICE_MILLISECONDS = 2500,
	NOTICE_FADE_MILLISECONDS = 500,
	NOTICE_MARGIN = 10,
	/* draw_string.c's left-justified text (its enum is its own) */
	_notice_text_left = 0
};

/* the top, clear of the HUD's shields and the latency meter at the right */
#define NOTICE_TOP_FRACTION 0.06f

/* ---------- globals */

static char notice_text[96];
static unsigned long notice_until;

/* cseries_windows.c's */
unsigned long system_milliseconds(void);

/* ---------- public code */

void detail_notice_set(char const *text)
{
	snprintf(notice_text, sizeof(notice_text), "%s", text ? text : "");
	notice_until = system_milliseconds() + NOTICE_MILLISECONDS;
	if (!notice_until)
		notice_until = 1;
}

void detail_notice_draw(void)
{
	long font_index;
	struct font_header *font;
	short line_height;
	unsigned long now;
	real_argb_color color;
	rectangle2d bounds;

	if (!notice_until || !notice_text[0])
		return;
	now = system_milliseconds();
	if ((long)(notice_until - now) <= 0)
	{
		notice_until = 0;
		return;
	}
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
		return;
	color.alpha = notice_until - now < NOTICE_FADE_MILLISECONDS ?
		(real)(notice_until - now) / NOTICE_FADE_MILLISECONDS : 1.0f;
	color.red = 0.75f;
	color.green = 0.95f;
	color.blue = 1.0f;
	bounds = render.camera.window_bounds;
	offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
	bounds.y0 = (short)(bounds.y0 + (bounds.y1 - bounds.y0) * NOTICE_TOP_FRACTION);
	bounds.y1 = (short)(bounds.y0 + line_height);
	bounds.x0 = (short)(bounds.x0 + NOTICE_MARGIN);
	draw_string_set_draw_mode(font_index, NONE, _notice_text_left, 0, &color);
	rasterizer_draw_string(&bounds, NULL, NULL, 0, notice_text);
}
