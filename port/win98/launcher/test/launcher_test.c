/* The setup window's choices (win9x_launcher_logic.c), run on any machine:
each preset written to a fake config.toml and read back must come out as it
went in, and the machine suggestions must be in order. Built and run by
launcher_test.py. */

#include <stdio.h>
#include <string.h>

#include "../win9x_launcher_logic.h"

static char keys[32][48], values[32][400];
static int count;

static int fake_write(const char *name, const char *value)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (!strcmp(keys[index], name))
			break;
	}
	if (index == count)
		strcpy(keys[count++], name);
	strcpy(values[index], value);
	return 1;
}

static const char *fake_read(const char *name)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (!strcmp(keys[index], name))
			return values[index];
	}
	return "";
}

int main(void)
{
	int preset, failures = 0;
	unsigned long suggestions[][3] = { { 233, 64, LAUNCHER_PRESET_FASTEST }, { 400, 128, LAUNCHER_PRESET_FAST },
		{ 800, 256, LAUNCHER_PRESET_BALANCED }, { 2400, 1024, LAUNCHER_PRESET_BEST },
		{ 0, 0, LAUNCHER_PRESET_FAST }, { 1000, 32, LAUNCHER_PRESET_FASTEST }, { 0, 128, LAUNCHER_PRESET_BALANCED } };
	int index;

	for (preset = 0; preset < LAUNCHER_PRESET_COUNT; preset++)
	{
		struct launcher_choices written, read;

		count = 0;
		launcher_preset(preset, &written);
		launcher_apply(&written, fake_write);
		launcher_load(&read, fake_read);
		if (memcmp(&written, &read, sizeof(written)))
		{
			printf("FAIL %s: the choices read back differ\n", launcher_preset_name(preset));
			failures++;
		}
	}
	for (index = 0; index < (int)(sizeof(suggestions) / sizeof(suggestions[0])); index++)
	{
		int got = launcher_suggest(suggestions[index][0], suggestions[index][1]);

		if (got != (int)suggestions[index][2])
		{
			printf("FAIL suggest(%lu MHz, %lu MB) = %d, wanted %lu\n", suggestions[index][0],
				suggestions[index][1], got, suggestions[index][2]);
			failures++;
		}
	}
	/* a benchmark's advice: a lighter preset below 20 frames a second, a heavier from 45, else the same */
	{
		static const double cases[][3] = { { LAUNCHER_PRESET_BALANCED, 8.0, LAUNCHER_PRESET_FAST },
			{ LAUNCHER_PRESET_FASTEST, 8.0, LAUNCHER_PRESET_FASTEST }, { LAUNCHER_PRESET_FAST, 60.0, LAUNCHER_PRESET_BALANCED },
			{ LAUNCHER_PRESET_BEST, 60.0, LAUNCHER_PRESET_BEST }, { LAUNCHER_PRESET_FAST, 30.0, LAUNCHER_PRESET_FAST },
			{ LAUNCHER_PRESET_BEST, 0.0, LAUNCHER_PRESET_BEST } };
		char text[200];

		for (index = 0; index < (int)(sizeof(cases) / sizeof(cases[0])); index++)
		{
			int got = launcher_advice((int)cases[index][0], cases[index][1], text, sizeof(text));

			if (got != (int)cases[index][2] || !text[0])
			{
				printf("FAIL advice(%d, %.1f) = %d, wanted %d\n", (int)cases[index][0], cases[index][1], got,
					(int)cases[index][2]);
				failures++;
			}
		}
	}
	/* the settings a preset writes, for launcher_test.py to compare with the menu's */
	count = 0;
	{
		struct launcher_choices choices;

		launcher_preset(LAUNCHER_PRESET_BEST, &choices);
		for (index = 0; index < LAUNCHER_EXTRAS_STOPS; index++)
		{
			choices.extras_stop = index;
			launcher_apply(&choices, fake_write);
			printf("extras %d %s\n", index, fake_read("render.skip_passes"));
		}
	}
	printf(failures ? "%d FAILED\n" : "ok\n", failures);
	return failures != 0;
}
