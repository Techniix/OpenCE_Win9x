/*
HALO_WIN9X_PLATFORM_PREFIX.H

Force-included ahead of every platform unit (port/linux/src,
port/windows/src) in the Windows 9x build, in place of
halo_windows_prefix.h, which it includes. The Windows build compiles them
with clang's Microsoft target on the Windows SDK; this build compiles them
with the GNU target on mingw-w64's headers (port/win98/README.md), which
need three things to look the same:
- _M_IX86, which the Microsoft target predefines;
- __forceinline as MSVC has it, which the XDK's static inline functions
  use: mingw's is extern;
- no definitions of the _Interlocked* intrinsics of mingw's own: the
  Windows prefix gives their names to the platform layer's
  (port/linux/src/xbox_kernel.c).
*/

#ifndef __HALO_WIN9X_PLATFORM_PREFIX_H
#define __HALO_WIN9X_PLATFORM_PREFIX_H

#ifndef _M_IX86
#define _M_IX86 600
#endif

#include <_mingw.h>
#undef __forceinline
#define __forceinline __inline__ __attribute__((__always_inline__))

#define __INTRINSIC_DEFINED__InterlockedCompareExchange
#define __INTRINSIC_DEFINED__InterlockedDecrement
#define __INTRINSIC_DEFINED__InterlockedExchange
#define __INTRINSIC_DEFINED__InterlockedExchangeAdd
#define __INTRINSIC_DEFINED__InterlockedIncrement

#define HALO_WIN9X 1

#include "../../windows/include/halo_windows_prefix.h"

#endif
