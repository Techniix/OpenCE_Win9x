/*
WIN9X_LAUNCHER_LOGIC.H

The setup window's choices (win9x_launcher.c) and the settings they stand
for, apart from the window, so that a test can run them anywhere
(launcher/test/launcher_test.c): the presets, what a machine's speed and
memory suggest, and the writing and reading of config.toml's settings.

Each choice is a slider's stop or a toggle, and each stop is one the menus
have (Video Setup, Performance: tools/port_settings.py) so that the two
screens agree on the same file.
*/

#ifndef WIN9X_LAUNCHER_LOGIC_H
#define WIN9X_LAUNCHER_LOGIC_H

enum
{
	LAUNCHER_PRESET_FASTEST,
	LAUNCHER_PRESET_FAST,
	LAUNCHER_PRESET_BALANCED,
	LAUNCHER_PRESET_BEST,
	LAUNCHER_PRESET_COUNT
};

/* the stops of the sliders */
enum
{
	LAUNCHER_SCALE_STOPS = 5,	/* original (480 lines), 25%, 50%, 75%, native */
	LAUNCHER_FPS_STOPS = 4,		/* 30, 60, 120, none */
	LAUNCHER_EXTRAS_STOPS = 4,	/* all, no shine, fewer, bare */
	LAUNCHER_DETAIL_MINIMUM = 15,	/* object detail, in percent, in steps of 5 */
	LAUNCHER_DETAIL_MAXIMUM = 100,
	LAUNCHER_DETAIL_STEP = 5
};

struct launcher_choices
{
	int display_mode;	/* 0 fullscreen, 1 borderless, 2 windowed */
	int scale_stop;
	int detail_percent;
	int extras_stop;
	int fps_stop;
	int vsync;
	int smooth_motion;
	int pvs_visibility;
	int reverb;
	int fast_mixing;	/* audio.resampler "linear" rather than "sinc" */
	int sound;
};

typedef int (*launcher_write)(const char *name, const char *value);
typedef const char *(*launcher_read)(const char *name);

const char *launcher_preset_name(int preset);
void launcher_preset(int preset, struct launcher_choices *choices);
/* the preset a machine of this many MHz (0: unknown) and megabytes of
memory is suggested; its graphics card is not known here */
int launcher_suggest(unsigned long megahertz, unsigned long megabytes);

const char *launcher_scale_name(int stop);
const char *launcher_fps_name(int stop);
const char *launcher_extras_name(int stop);

/* writes every setting the choices stand for, through write */
void launcher_apply(const struct launcher_choices *choices, launcher_write write);
/* the choices as the settings read through read now have them (the nearest
stops, as the menus show them) */
void launcher_load(struct launcher_choices *choices, launcher_read read);

#endif
