/*
WIN9X_LAUNCHER_LOGIC.C

See win9x_launcher_logic.h.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "win9x_launcher_logic.h"

/* ---------- the stops */

static const char *const display_modes[] = { "fullscreen", "borderless", "windowed" };
static const char *const scale_values[LAUNCHER_SCALE_STOPS] = { "original", "25%", "50%", "75%", "native" };
static const char *const scale_names[LAUNCHER_SCALE_STOPS] =
	{ "Original (480 lines)", "25%", "50%", "75%", "Native" };
static const int fps_values[LAUNCHER_FPS_STOPS] = { 30, 60, 120, -1 };
static const char *const fps_names[LAUNCHER_FPS_STOPS] = { "30", "60", "120", "None" };

/* the lists of render.skip_passes, as Video Setup, Performance has them
(tools/port_settings.py _SKIP_*; launcher_test.py checks that they agree) */
#define SKIP_SHINY \
	"lights_specular,bsp_specular_lightmaps,bsp_reflection_masks,bsp_reflection_mirrors,bsp_reflections"
#define SKIP_SOME SKIP_SHINY ",object_shadows,decals_light,decals_alpha_tested,detail_objects"
#define SKIP_MOST SKIP_SOME ",decals_primary,decals_secondary,decals_water,bsp_fog,fog_screen"
static const char *const extras_values[LAUNCHER_EXTRAS_STOPS] = { "", SKIP_SHINY, SKIP_SOME, SKIP_MOST };
static const char *const extras_names[LAUNCHER_EXTRAS_STOPS] =
	{ "All", "No shine", "Fewer", "Bare" };

static const char *const preset_names[LAUNCHER_PRESET_COUNT] = { "Fastest", "Fast", "Balanced", "Best" };

/* ---------- public code */

const char *launcher_preset_name(int preset)
{
	return preset >= 0 && preset < LAUNCHER_PRESET_COUNT ? preset_names[preset] : "";
}

const char *launcher_scale_name(int stop)
{
	return stop >= 0 && stop < LAUNCHER_SCALE_STOPS ? scale_names[stop] : "";
}

const char *launcher_fps_name(int stop)
{
	return stop >= 0 && stop < LAUNCHER_FPS_STOPS ? fps_names[stop] : "";
}

const char *launcher_extras_name(int stop)
{
	return stop >= 0 && stop < LAUNCHER_EXTRAS_STOPS ? extras_names[stop] : "";
}

void launcher_preset(int preset, struct launcher_choices *choices)
{
	memset(choices, 0, sizeof(*choices));
	choices->sound = 1;
	switch (preset)
	{
	case LAUNCHER_PRESET_FASTEST:
		/* a Pentium MMX or Pentium II class machine and a card of 1998:
		the picture drawn small, half the objects' detail, little but the
		walls, and 30 frames a second, the game's own */
		choices->scale_stop = 2;
		choices->detail_percent = 25;
		choices->extras_stop = 3;
		choices->fps_stop = 0;
		choices->pvs_visibility = 1;
		choices->fast_mixing = 1;
		break;
	case LAUNCHER_PRESET_FAST:
		choices->scale_stop = 3;
		choices->detail_percent = 50;
		choices->extras_stop = 2;
		choices->fps_stop = 0;
		choices->fast_mixing = 1;
		break;
	case LAUNCHER_PRESET_BALANCED:
		choices->scale_stop = 4;
		choices->detail_percent = 75;
		choices->extras_stop = 1;
		choices->fps_stop = 1;
		choices->vsync = 1;
		choices->smooth_motion = 1;
		choices->reverb = 1;
		choices->fast_mixing = 1;
		break;
	default:
		/* the game's own settings */
		choices->scale_stop = 4;
		choices->detail_percent = LAUNCHER_DETAIL_MAXIMUM;
		choices->extras_stop = 0;
		choices->fps_stop = LAUNCHER_FPS_STOPS - 1;
		choices->vsync = 1;
		choices->smooth_motion = 1;
		choices->reverb = 1;
		choices->fast_mixing = 0;
		break;
	}
}

int launcher_suggest(unsigned long megahertz, unsigned long megabytes)
{
	/* (0 is not known: the middle) */
	if (!megahertz && !megabytes)
		return LAUNCHER_PRESET_FAST;
	if ((megahertz && megahertz < 350) || (megabytes && megabytes < 48))
		return LAUNCHER_PRESET_FASTEST;
	if ((megahertz && megahertz < 700) || (megabytes && megabytes < 96))
		return LAUNCHER_PRESET_FAST;
	if ((megahertz && megahertz < 1200) || (megabytes && megabytes < 192))
		return LAUNCHER_PRESET_BALANCED;
	return LAUNCHER_PRESET_BEST;
}

void launcher_apply(const struct launcher_choices *choices, launcher_write write)
{
	char text[16];
	int percent = choices->detail_percent;

	if (percent < LAUNCHER_DETAIL_MINIMUM)
		percent = LAUNCHER_DETAIL_MINIMUM;
	if (percent > LAUNCHER_DETAIL_MAXIMUM)
		percent = LAUNCHER_DETAIL_MAXIMUM;
	write("display.mode", display_modes[choices->display_mode >= 0 && choices->display_mode < 3 ?
		choices->display_mode : 0]);
	write("display.resolution_scaling", scale_values[choices->scale_stop >= 0 &&
		choices->scale_stop < LAUNCHER_SCALE_STOPS ? choices->scale_stop : LAUNCHER_SCALE_STOPS - 1]);
	sprintf(text, "%d.%02d", percent / 100, percent % 100);
	write("render.lod_scale", text);
	write("render.skip_passes", extras_values[choices->extras_stop >= 0 &&
		choices->extras_stop < LAUNCHER_EXTRAS_STOPS ? choices->extras_stop : 0]);
	write("render.visibility", choices->pvs_visibility ? "pvs" : "portals");
	write("display.vsync", choices->vsync ? "true" : "false");
	sprintf(text, "%d", fps_values[choices->fps_stop >= 0 && choices->fps_stop < LAUNCHER_FPS_STOPS ?
		choices->fps_stop : LAUNCHER_FPS_STOPS - 1]);
	write("display.max_fps", text);
	write("display.interpolation", choices->smooth_motion ? "true" : "false");
	write("audio.reverb", choices->reverb ? "true" : "false");
	write("audio.resampler", choices->fast_mixing ? "linear" : "sinc");
	write("audio.enabled", choices->sound ? "true" : "false");
}

static int is_true(const char *text)
{
	return text && !strcmp(text, "true");
}

void launcher_load(struct launcher_choices *choices, launcher_read read)
{
	const char *text;
	int index, best;
	double number;

	memset(choices, 0, sizeof(*choices));
	text = read("display.mode");
	for (index = 0; index < 3; index++)
	{
		if (text && !strcmp(text, display_modes[index]))
			choices->display_mode = index;
	}
	text = read("display.resolution_scaling");
	choices->scale_stop = LAUNCHER_SCALE_STOPS - 1;
	for (index = 0; index < LAUNCHER_SCALE_STOPS; index++)
	{
		if (text && !strcmp(text, scale_values[index]))
			choices->scale_stop = index;
	}
	text = read("render.lod_scale");
	number = text && *text ? atof(text) : 1.0;
	if (number <= 0.0)
		number = 1.0;
	best = (int)(number * 100.0 + 0.5);
	/* (the nearest step) */
	best = ((best + LAUNCHER_DETAIL_STEP / 2) / LAUNCHER_DETAIL_STEP) * LAUNCHER_DETAIL_STEP;
	choices->detail_percent = best < LAUNCHER_DETAIL_MINIMUM ? LAUNCHER_DETAIL_MINIMUM :
		best > LAUNCHER_DETAIL_MAXIMUM ? LAUNCHER_DETAIL_MAXIMUM : best;
	/* (a list that is not one of the steps shows as the first, as the menus do) */
	text = read("render.skip_passes");
	for (index = 0; index < LAUNCHER_EXTRAS_STOPS; index++)
	{
		if (text && !strcmp(text, extras_values[index]))
			choices->extras_stop = index;
	}
	text = read("render.visibility");
	choices->pvs_visibility = text && !strcmp(text, "pvs");
	choices->vsync = is_true(read("display.vsync"));
	text = read("display.max_fps");
	number = text && *text ? atof(text) : 0.0;
	choices->fps_stop = LAUNCHER_FPS_STOPS - 1;
	if (number > 0.0)
	{
		double gap = 1e9;

		for (index = 0; index < LAUNCHER_FPS_STOPS - 1; index++)
		{
			double d = number > fps_values[index] ? number - fps_values[index] : fps_values[index] - number;

			if (d < gap)
			{
				gap = d;
				choices->fps_stop = index;
			}
		}
	}
	choices->smooth_motion = is_true(read("display.interpolation"));
	choices->reverb = is_true(read("audio.reverb"));
	text = read("audio.resampler");
	choices->fast_mixing = text && !strcmp(text, "linear");
	text = read("audio.enabled");
	choices->sound = !text || strcmp(text, "false");
}
