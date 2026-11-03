/*
WIN9X_WATCH.H

The Windows 9x build's start-up trace (win9x_watch.c).
*/

#ifndef __WIN9X_WATCH_H
#define __WIN9X_WATCH_H

void win9x_watch_start(void);
/* a line in halo_trace.txt, with the milliseconds since start-up */
void win9x_trace(const char *stage);

#endif
