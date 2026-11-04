/* frame_budget.c on a clock the test drives: the steps' times must come out
as they were fed, in order of size, adding up to the window's frames.
Built by frame_budget_test.py (the include of cseries.h is left out there). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "frame_budget.h"

static long long fake_ticks;
static long fake_window = 2;
int QueryPerformanceCounter(long long *count) { *count = fake_ticks; return 1; }
int QueryPerformanceFrequency(long long *frequency) { *frequency = 1000000; return 1; }
long config_integer(const char *name) { (void)name; return fake_window; }
static char log_text[4096];
void platform_log(const char *format, ...)
{
	va_list args;
	size_t length = strlen(log_text);

	va_start(args, format);
	vsnprintf(log_text + length, sizeof(log_text) - length - 1, format, args);
	va_end(args);
	strcat(log_text, "\n");
}

int main(void)
{
	int frame, failures = 0;

	fake_ticks = 5000000;
	/* 20 frames of 100 ms: simulation 60 ms, render pass "a" 30 ms, throttle 10 ms */
	for (frame = 0; frame < 40; frame++)
	{
		halo_budget_frame();
		halo_budget_enter("game simulation");
		fake_ticks += 60000;
		halo_budget_enter("a");
		fake_ticks += 30000;
		halo_budget_enter("throttle (waiting)");
		fake_ticks += 10000;
	}
	fputs(log_text, stdout);
	if (!strstr(log_text, "game simulation") || !strstr(log_text, "60.00 ms"))
	{
		puts("FAIL: simulation not 60 ms");
		failures++;
	}
	if (!strstr(log_text, "30.00 ms") || !strstr(log_text, "10.00 ms") || !strstr(log_text, "100.0 ms a frame"))
	{
		puts("FAIL: the other steps or the frame total");
		failures++;
	}
	fake_window = 0;
	log_text[0] = 0;
	halo_budget_frame();
	halo_budget_enter("x");
	if (log_text[0])
	{
		puts("FAIL: output while off");
		failures++;
	}
	puts(failures ? "FAILED" : "ok");
	return failures != 0;
}
