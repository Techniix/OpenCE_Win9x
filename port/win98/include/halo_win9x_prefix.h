/*
HALO_WIN9X_PREFIX.H

Force-included ahead of every game unit in the Windows 9x build. The build
uses the Linux build's environment (GNU C with MSVC's struct layout and
16-bit wchar_t: port/linux/include/halo_linux_prefix.h) on mingw-w64's
headers and Windows 98's msvcrt.dll. mingw's own definitions come first, so
that the Linux prefix's MSVC inline semantics replace them. The platform
layer is the Windows build's (port/win98/include/halo_win9x_platform_prefix.h),
so the Xbox functions it implements under Windows names are renamed as the
Windows build renames them.
*/

#ifndef __HALO_WIN9X_PREFIX_H
#define __HALO_WIN9X_PREFIX_H

#include <_mingw.h>
#undef __inline
#undef _inline
#undef __forceinline

#define HALO_WIN9X 1

/* the game's struct timeval is the XDK's Winsock one (port/include/xdk);
mingw's <time.h> must not declare its own */
#define _TIMEVAL_DEFINED

#include "../../linux/include/halo_linux_prefix.h"

/* the Xbox functions named like Windows functions, under the names the
platform layer gives them in a Windows program (as halo_windows_prefix.h
names them for the Windows build's game units) */
#include "../../windows/include/halo_windows_api_names.h"

#endif
