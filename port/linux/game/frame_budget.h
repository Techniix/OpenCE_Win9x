/*
FRAME_BUDGET.H

Where a frame's time goes (debug.frame_budget): the main loop's steps and
the render passes timed, and their average in milliseconds a frame written to
the log every so many seconds. For finding what a slow machine, or an
emulated one, spends its frames on before anything is made lighter: it only
reads the clock, and changes neither the game nor the picture.
*/

#ifndef __FRAME_BUDGET_H
#define __FRAME_BUDGET_H
#pragma once

/* the start of a frame of the main loop (main.c's main_loop_iteration):
counts it, and writes the report once a window's seconds have gone */
void halo_budget_frame(void);
/* the time since the last call is the step that was before this one's;
from here on it is name's. The name must be a string that stays (a literal) */
void halo_budget_enter(const char *name);

/* the frame rate of the last report written (else the whole run's so far, else 0): what the setup
window's benchmark tells at the end of its run */
double halo_budget_result(void);

#endif
