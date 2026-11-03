/*
STRING.H

mingw's <string.h> without its strnlen(), which the game declares with
another signature (source/cseries/cseries.h).
*/

#ifndef __HALO_WIN9X_STRING_H
#define __HALO_WIN9X_STRING_H

#define strnlen halo_win9x_crt_strnlen
#include_next <string.h>
#undef strnlen

#endif
