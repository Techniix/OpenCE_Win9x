/*
FRAME_BUDGET.C

debug.frame_budget: the main loop's steps and the render passes timed, and
reported to the log every so many seconds (frame_budget.h).

Each halo_budget_enter ends the step before it and begins its own, so no time
goes unaccounted: what is in the report adds up to a frame. Waiting for the
display (throttle) and the swap (present) are steps of their own, to tell a
machine held up by the processor from one waiting for its graphics card.
Nothing here reads or writes the game's state.
*/

#include "cseries.h"
#include "frame_budget.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* port_config.c's */
long config_integer(const char *name);
/* the performance counter (the Xbox kernel's, port/linux/src/xbox_kernel.c, or kernel32's): a 64-bit count
and its frequency, which LARGE_INTEGER's QuadPart is */
int __stdcall QueryPerformanceCounter(long long *count);
int __stdcall QueryPerformanceFrequency(long long *frequency);
/* the port's log (platform.h) */
void platform_log(const char *format, ...);

/* ---------- constants */

enum
{
	MAXIMUM_STEPS = 64,
	REPORTED_STEPS = 14
};

/* ---------- structures */

struct step
{
	const char *name;
	double seconds;
};

/* ---------- globals */

static struct step steps[MAXIMUM_STEPS];
static int step_count;
static int current = -1;
static double last_time;
static double window_start;
static unsigned long window_frames;
static double ticks_per_second;
/* what halo_budget_result gives: the last report's rate, and the whole run's as the fallback */
static double result_rate;
static double run_start;
static double run_seconds;
static unsigned long run_frames;

/* ---------- private code */

static double clock_seconds(void)
{
	long long count = 0;

	if (ticks_per_second == 0.0)
	{
		long long frequency = 0;

		QueryPerformanceFrequency(&frequency);
		ticks_per_second = frequency > 0 ? (double)frequency : 1000000.0;
	}
	QueryPerformanceCounter(&count);
	return (double)count / ticks_per_second;
}

static int step_find(const char *name)
{
	int index;

	/* (the same literal most of the time: the pointer first) */
	for (index = 0; index < step_count; index++)
	{
		if (steps[index].name == name)
			return index;
	}
	for (index = 0; index < step_count; index++)
	{
		if (!strcmp(steps[index].name, name))
			return index;
	}
	if (step_count >= MAXIMUM_STEPS)
		return -1;
	steps[step_count].name = name;
	steps[step_count].seconds = 0.0;
	return step_count++;
}

static int step_compare(const void *a, const void *b)
{
	double x = ((const struct step *)a)->seconds, y = ((const struct step *)b)->seconds;

	return x < y ? 1 : x > y ? -1 : 0;
}

static void report(double elapsed)
{
	double total = 0.0;
	int index;

	for (index = 0; index < step_count; index++)
		total += steps[index].seconds;
	if (window_frames && elapsed > 0.0)
		result_rate = window_frames / elapsed;
	if (window_frames && total > 0.0)
	{
		platform_log("budget: %lu frames in %.1f s, %.2f frames a second, %.1f ms a frame; the steps below:",
			window_frames, elapsed, window_frames / elapsed, total * 1000.0 / window_frames);
		qsort(steps, step_count, sizeof(steps[0]), step_compare);
		for (index = 0; index < step_count && index < REPORTED_STEPS; index++)
		{
			/* (a step with no time: the rollover's own) */
			if (steps[index].seconds * 1000.0 / window_frames < 0.005)
				break;
			platform_log("budget: %8.2f ms %5.1f%%  %s", steps[index].seconds * 1000.0 / window_frames,
				steps[index].seconds * 100.0 / total, steps[index].name);
		}
	}
	step_count = 0;
	current = -1;
	window_frames = 0;
}

/* ---------- public code */

void halo_budget_frame(void)
{
	long window = config_integer("debug.frame_budget");
	double now;

	if (window <= 0)
	{
		if (step_count || window_frames)
		{
			step_count = 0;
			window_frames = 0;
		}
		current = -1;
		window_start = 0.0;
		return;
	}
	now = clock_seconds();
	if (window_start == 0.0)
	{
		window_start = now;
		last_time = now;
		return;
	}
	window_frames++;
	if (!run_start)
		run_start = now;
	run_frames++;
	run_seconds = now - run_start;
	if (now - window_start >= (double)window)
	{
		/* (the time up to now is the step it ended in) */
		halo_budget_enter("frame start");
		report(now - window_start);
		window_start = clock_seconds();
		last_time = window_start;
	}
}

void halo_budget_enter(const char *name)
{
	double now;
	int next;

	if (window_start == 0.0)
		return;
	now = clock_seconds();
	if (current >= 0)
		steps[current].seconds += now - last_time;
	last_time = now;
	next = step_find(name);
	current = next;
}

double halo_budget_result(void)
{
	if (result_rate > 0.0)
		return result_rate;
	return run_seconds > 0.0 ? run_frames / run_seconds : 0.0;
}
