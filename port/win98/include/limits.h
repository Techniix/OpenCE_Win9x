/*
LIMITS.H

mingw's <limits.h>, less the names the game declares itself as enumerators
(source/cseries/cseries.h): the Xbox C runtime's headers did not define them.
*/

#ifndef __HALO_WIN9X_LIMITS_H
#define __HALO_WIN9X_LIMITS_H

#include_next <limits.h>

#undef LONG_MAX
#undef LONG_MIN
#undef CHAR_MAX
#undef CHAR_MIN

#endif
