/*
RENDER_POLICY.C

What the picture may leave out or draw cheaper, for the machines that cannot
draw all of it (render.skip_passes, render.lod_scale, render.visibility in
port_config.c). It is read by the render code alone (source/render/render.c,
render_objects.c), once for each pass or object, and nothing it does reaches
the game's state or the network: the passes it skips only draw. With its
defaults (nothing skipped, a scale of 1, no adaptive rate, no hot key
pressed) the frame is the one without it.

The performance level (0 to 6) is the one dial under them: 0 draws what the
settings say, and each level above multiplies the objects' detail scale down
and, from level 2, leaves out the next list of passes (halo_render_stages.h).
It is the sum of two:
- the player's, by the hot keys (Ctrl+Minus, Ctrl+Equals: Quake's +/- for the
  size of the view, on a machine whose menus are too slow to use);
- the governor's, if render.adaptive_fps is not 0: once a second it compares
  the frames' average time with the rate asked for, and goes a level up if
  they are slower than that, a level down after three seconds of being well
  within it. Only the drawing is cheaper: if the processor is held up by the
  game itself, no level helps, and the governor stops at the top.

Only the passes named in skippable_passes can be skipped: those that draw
over what the passes before them did, and keep nothing that a later pass
reads. A name that is not there is ignored, not an error.
*/

#include <string.h>

#include "cseries.h"
#include "cseries/errors.h"
#include "render_policy.h"
#include "../src/port_config.h" /* port: port/linux/src/port_config.c */
#include "../include/halo_render_stages.h"

/* ---------- constants */

#define MINIMUM_LOD_SCALE 0.05
#define MAXIMUM_LOD_SCALE 4.0

enum
{
	MAXIMUM_LEVEL = 6,
	GOVERNOR_WINDOW_MILLISECONDS = 1000,
	GOVERNOR_QUIET_WINDOWS = 3
};

/* the names halo_render_pass gives in render_window (source/render/render.c) */
static const char *const skippable_passes[] =
{
	"object_shadows",
	"lights_specular",
	"bsp_specular_lightmaps",
	"bsp_reflection_masks",
	"bsp_reflection_mirrors",
	"bsp_reflections",
	"bsp_fog",
	"detail_objects",
	"fog_screen",
	"decals_light",
	"decals_alpha_tested",
	"decals_primary",
	"decals_secondary",
	"decals_water",
};

/* each level's multiplier of the detail scale, and the list of passes it
leaves out whatever the settings say (0: none, 1 shiny, 2 some, 3 most) */
static const struct
{
	float detail;
	short stage;
} levels[MAXIMUM_LEVEL + 1] =
{
	{ 1.00f, 0 }, { 0.85f, 0 }, { 0.70f, 1 }, { 0.55f, 1 }, { 0.45f, 2 }, { 0.35f, 2 }, { 0.25f, 3 },
};

static const char *const stage_lists[] = { "", HALO_SKIP_SHINY, HALO_SKIP_SOME, HALO_SKIP_MOST };

/* ---------- private data */

static int manual_level, governor_level;
static unsigned long window_start_ms, window_frames;
static int quiet_windows;

/* ---------- private code */

static boolean pass_is_skippable(const char *name, size_t length)
{
	size_t index;

	for (index = 0; index < sizeof(skippable_passes) / sizeof(skippable_passes[0]); index++)
	{
		if (strlen(skippable_passes[index]) == length && !memcmp(skippable_passes[index], name, length))
			return TRUE;
	}
	return FALSE;
}

/* whether the comma-separated list has the name */
static boolean list_has(const char *list, const char *name, size_t length)
{
	while (list && *list)
	{
		const char *end;

		while (*list == ',' || *list == ' ')
			list++;
		end = list;
		while (*end && *end != ',' && *end != ' ')
			end++;
		if ((size_t)(end - list) == length && !memcmp(list, name, length))
			return TRUE;
		list = end;
	}
	return FALSE;
}

static int effective_level(void)
{
	int level = manual_level + governor_level;

	return level < 0 ? 0 : level > MAXIMUM_LEVEL ? MAXIMUM_LEVEL : level;
}

/* ---------- public code */

/* whether the pass named is to be drawn: FALSE if it may be skipped and
render.skip_passes has it, or the performance level's list does */
unsigned char halo_render_pass_enabled(const char *name)
{
	size_t length = name ? strlen(name) : 0;

	if (!length || !pass_is_skippable(name, length))
		return TRUE;
	if (list_has(config_string("render.skip_passes"), name, length))
		return FALSE;
	return !list_has(stage_lists[levels[effective_level()].stage], name, length);
}

/* whether the clusters in view are the cluster's stored potentially visible
set (render.visibility = "pvs") rather than those the portals show: the
game's own structures_use_pvs_for_vs (structure_visibility.c), which draws
more clusters for a lighter traversal. Only the picture changes: the stored
set is read by the game's other systems too (AI, sound, objects), unchanged */
unsigned char halo_render_visibility_pvs(void)
{
	const char *mode = config_string("render.visibility");

	return mode && !strcmp(mode, "pvs");
}

/* the factor an object's size on the screen is multiplied by to choose its
level of detail (render_objects.c): below 1 the objects use the coarser levels
sooner. render.lod_scale, and the performance level's */
float halo_render_lod_scale(void)
{
	double scale = config_real("render.lod_scale");

	if (!(scale >= MINIMUM_LOD_SCALE))
		scale = scale == 0.0 ? 1.0 : MINIMUM_LOD_SCALE;
	if (scale > MAXIMUM_LOD_SCALE)
		scale = MAXIMUM_LOD_SCALE;
	return (float)(scale * levels[effective_level()].detail);
}

/* Ctrl+Minus and Ctrl+Equals: a step on the player's side of the level */
void halo_render_level_adjust(int delta)
{
	int before = effective_level();

	if (!config_boolean("render.hotkeys"))
		return;
	manual_level += delta;
	/* (not past the ends: more detail than the settings say, or less than the
	most, would be steps that do nothing, and take as many keys to come back) */
	if (manual_level + governor_level < 0)
		manual_level = -governor_level;
	if (manual_level + governor_level > MAXIMUM_LEVEL)
		manual_level = MAXIMUM_LEVEL - governor_level;
	if (effective_level() != before)
		error(_error_silent, "render: performance level %d of %d (by the keys: %d, adaptive: %d)", effective_level(),
			MAXIMUM_LEVEL, manual_level, governor_level);
}

/* once for each frame drawn (render_frame): the governor, if render.adaptive_fps is on */
void halo_render_governor_frame(void)
{
	long wanted = config_integer("render.adaptive_fps");
	unsigned long now = system_milliseconds();

	if (wanted <= 0)
	{
		governor_level = 0;
		window_start_ms = 0;
		return;
	}
	if (!window_start_ms)
	{
		window_start_ms = now;
		window_frames = 0;
		return;
	}
	window_frames++;
	if (now - window_start_ms >= GOVERNOR_WINDOW_MILLISECONDS)
	{
		/* frames a second over the window */
		double rate = window_frames * 1000.0 / (double)(now - window_start_ms);

		if (rate < wanted * 0.9)
		{
			quiet_windows = 0;
			if (governor_level + manual_level < MAXIMUM_LEVEL)
			{
				governor_level++;
				error(_error_silent, "render: %.1f frames a second, under %ld: performance level %d", rate, wanted,
					effective_level());
			}
		}
		else if (rate > wanted * 1.4 && governor_level > 0)
		{
			if (++quiet_windows >= GOVERNOR_QUIET_WINDOWS)
			{
				quiet_windows = 0;
				governor_level--;
				error(_error_silent, "render: %.1f frames a second, well over %ld: performance level %d", rate, wanted,
					effective_level());
			}
		}
		else
			quiet_windows = 0;
		window_start_ms = now;
		window_frames = 0;
	}
}
