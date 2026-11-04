/*
WIN9X_LAUNCHER_LOGIC.C

See win9x_launcher_logic.h.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "win9x_launcher_logic.h"
#include "../../linux/include/halo_render_stages.h"

/* ---------- the stops */

static const char *const display_modes[] = { "fullscreen", "borderless", "windowed" };
static const char *const scale_values[LAUNCHER_SCALE_STOPS] = { "original", "25%", "50%", "75%", "native" };
static const char *const scale_names[LAUNCHER_SCALE_STOPS] =
	{ "Original", "25%", "50%", "75%", "Native" };
static const int fps_values[LAUNCHER_FPS_STOPS] = { 30, 60, 120, -1 };
static const char *const fps_names[LAUNCHER_FPS_STOPS] = { "30", "60", "120", "None" };

/* the lists of render.skip_passes, as Video Setup, Performance has them
(halo_render_stages.h; launcher_test.py checks the menu's) */
static const char *const extras_values[LAUNCHER_EXTRAS_STOPS] = { "", HALO_SKIP_SHINY, HALO_SKIP_SOME, HALO_SKIP_MOST };
static const char *const extras_names[LAUNCHER_EXTRAS_STOPS] =
	{ "All", "No shine", "Fewer", "Bare" };

static const char *const texture_names[LAUNCHER_TEXTURE_STOPS] = { "Full", "Half", "Quarter", "Eighth" };
static const int keep_up_values[LAUNCHER_KEEP_UP_STOPS] = { 0, 20, 25, 30, 40 };

/* the sound's stops: what each writes (audio.sample_rate, audio.resampler, audio.sinc_voices,
audio.buffer_frames: a lower rate has a longer buffer for the processor's stalls, as the cards of the era had) */
static const char *const sound_names[LAUNCHER_SOUND_STOPS] = { "Low (22 kHz)", "Medium (32 kHz)", "High (44 kHz)", "Best (48 kHz)" };
static const int sound_rates[LAUNCHER_SOUND_STOPS] = { 22050, 32000, 44100, 48000 };
static const char *const sound_resamplers[LAUNCHER_SOUND_STOPS] = { "linear", "auto", "auto", "sinc" };
static const int sound_sinc_voices[LAUNCHER_SOUND_STOPS] = { 0, 3, 8, 6 };
static const int sound_buffers[LAUNCHER_SOUND_STOPS] = { 1024, 1024, 768, 512 };

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

const char *launcher_sound_name(int stop)
{
	return stop >= 0 && stop < LAUNCHER_SOUND_STOPS ? sound_names[stop] : "";
}

const char *launcher_texture_name(int stop)
{
	return stop >= 0 && stop < LAUNCHER_TEXTURE_STOPS ? texture_names[stop] : "";
}

int launcher_keep_up_fps(int stop)
{
	return stop >= 0 && stop < LAUNCHER_KEEP_UP_STOPS ? keep_up_values[stop] : 0;
}

int launcher_keep_up_stop(int fps)
{
	int index, best = 0;

	for (index = 1; index < LAUNCHER_KEEP_UP_STOPS; index++)
	{
		int gap = fps > keep_up_values[index] ? fps - keep_up_values[index] : keep_up_values[index] - fps;
		int best_gap = fps > keep_up_values[best] ? fps - keep_up_values[best] : keep_up_values[best] - fps;

		if (gap < best_gap)
			best = index;
	}
	return best;
}

void launcher_preset(int preset, struct launcher_choices *choices)
{
	memset(choices, 0, sizeof(*choices));
	choices->sound = 1;
	choices->brightness = 100;
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
		choices->sound_quality = 0;
		choices->texture_detail = 2;
		choices->keep_up = 25;
		break;
	case LAUNCHER_PRESET_FAST:
		choices->scale_stop = 3;
		choices->detail_percent = 50;
		choices->extras_stop = 2;
		choices->fps_stop = 0;
		choices->sound_quality = 1;
		choices->texture_detail = 1;
		choices->keep_up = 30;
		break;
	case LAUNCHER_PRESET_BALANCED:
		choices->scale_stop = 4;
		choices->detail_percent = 75;
		choices->extras_stop = 1;
		choices->fps_stop = 1;
		choices->vsync = 1;
		choices->smooth_motion = 1;
		choices->reverb = 1;
		choices->sound_quality = 2;
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
		choices->sound_quality = LAUNCHER_SOUND_STOPS - 1;
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
	sprintf(text, "%d", choices->texture_detail < 0 ? 0 :
		choices->texture_detail >= LAUNCHER_TEXTURE_STOPS ? LAUNCHER_TEXTURE_STOPS - 1 : choices->texture_detail);
	write("render.texture_detail", text);
	sprintf(text, "%d", choices->keep_up > 0 ? choices->keep_up : 0);
	write("render.adaptive_fps", text);
	percent = choices->brightness < LAUNCHER_BRIGHTNESS_MINIMUM ? 100 :
		choices->brightness > LAUNCHER_BRIGHTNESS_MAXIMUM ? LAUNCHER_BRIGHTNESS_MAXIMUM : choices->brightness;
	sprintf(text, "%d.%02d", percent / 100, percent % 100);
	write("render.gamma", text);
	write("display.vsync", choices->vsync ? "true" : "false");
	sprintf(text, "%d", fps_values[choices->fps_stop >= 0 && choices->fps_stop < LAUNCHER_FPS_STOPS ?
		choices->fps_stop : LAUNCHER_FPS_STOPS - 1]);
	write("display.max_fps", text);
	write("display.interpolation", choices->smooth_motion ? "true" : "false");
	write("audio.reverb", choices->reverb ? "true" : "false");
	{
		int stop = choices->sound_quality >= 0 && choices->sound_quality < LAUNCHER_SOUND_STOPS ?
			choices->sound_quality : LAUNCHER_SOUND_STOPS - 1;

		sprintf(text, "%d", sound_rates[stop]);
		write("audio.sample_rate", text);
		write("audio.resampler", sound_resamplers[stop]);
		sprintf(text, "%d", sound_sinc_voices[stop]);
		write("audio.sinc_voices", text);
		sprintf(text, "%d", sound_buffers[stop]);
		write("audio.buffer_frames", text);
	}
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
	text = read("render.texture_detail");
	choices->texture_detail = text && *text ? atoi(text) : 0;
	if (choices->texture_detail < 0 || choices->texture_detail >= LAUNCHER_TEXTURE_STOPS)
		choices->texture_detail = 0;
	text = read("render.adaptive_fps");
	choices->keep_up = text && *text && atoi(text) > 0 ? atoi(text) : 0;
	text = read("render.gamma");
	number = text && *text ? atof(text) : 1.0;
	best = (int)(number * 100.0 + 0.5);
	best = ((best + LAUNCHER_BRIGHTNESS_STEP / 2) / LAUNCHER_BRIGHTNESS_STEP) * LAUNCHER_BRIGHTNESS_STEP;
	choices->brightness = best < LAUNCHER_BRIGHTNESS_MINIMUM || best > LAUNCHER_BRIGHTNESS_MAXIMUM ? 100 : best;
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
	/* (the stop whose rate is the nearest; no rate set is the best's, 48000) */
	text = read("audio.sample_rate");
	number = text && *text ? atof(text) : 48000.0;
	choices->sound_quality = LAUNCHER_SOUND_STOPS - 1;
	{
		double gap = 1e9;

		for (index = 0; index < LAUNCHER_SOUND_STOPS; index++)
		{
			double d = number > sound_rates[index] ? number - sound_rates[index] : sound_rates[index] - number;

			if (d < gap)
			{
				gap = d;
				choices->sound_quality = index;
			}
		}
	}
	text = read("audio.enabled");
	choices->sound = !text || strcmp(text, "false");
}

int launcher_advice(int preset, double fps, char *text, unsigned long size)
{
	int suggested = preset;
	char line[200];

	if (preset < 0 || preset >= LAUNCHER_PRESET_COUNT)
		preset = suggested = LAUNCHER_PRESET_BALANCED;
	if (fps <= 0.0)
		sprintf(line, "The benchmark could not measure a frame rate. Try a lighter preset (Fastest) and run it again.");
	else if (fps < 20.0 && preset > LAUNCHER_PRESET_FASTEST)
	{
		suggested = preset - 1;
		sprintf(line, "%.1f frames a second at the %s preset: too slow. The %s preset is suggested.", fps,
			preset_names[preset], preset_names[suggested]);
	}
	else if (fps < 20.0)
		sprintf(line, "%.1f frames a second even at the %s preset: this PC is under what the game asks for. "
			"Lower the picture size and the frame limit in the setup window.", fps, preset_names[preset]);
	else if (fps >= 45.0 && preset < LAUNCHER_PRESET_BEST)
	{
		suggested = preset + 1;
		sprintf(line, "%.1f frames a second at the %s preset: there is room. The %s preset is suggested.", fps,
			preset_names[preset], preset_names[suggested]);
	}
	else
		sprintf(line, "%.1f frames a second at the %s preset: these settings suit this PC.", fps,
			preset_names[preset]);
	if (text && size)
	{
		strncpy(text, line, size - 1);
		text[size - 1] = '\0';
	}
	return suggested;
}
