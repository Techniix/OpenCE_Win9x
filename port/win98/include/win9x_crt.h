/*
WIN9X_CRT.H

The C runtime functions the platform layer calls that Windows 98's
msvcrt.dll lacks, defined in port/win98/src/win9x_compat.c.
*/

#ifndef __WIN9X_CRT_H
#define __WIN9X_CRT_H

#include <time.h>

struct tm *localtime_r(const time_t *time, struct tm *result);

#endif
