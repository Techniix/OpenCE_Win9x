/*
WIN9X_TLS.C

Thread-local storage (__thread, which the platform layer uses: its last
errors) for the executable, as mingw-w64's runtime has it (tlssup.c): the
TLS directory the loader reads (_tls_used, which the linker points the
executable's header at), the template of the variables' first values (the
.tls sections, from _tls_start to _tls_end) and the slot the loader gives
them (_tls_index). Windows 95 and 98 support this in an executable (not in
a DLL loaded later). No callbacks.
*/

#include <windows.h>

ULONG _tls_index = 0;

char _tls_start __attribute__((section(".tls"))) = 0;
char _tls_end __attribute__((section(".tls$ZZZ"))) = 0;

/* (the callbacks' list, empty: between .CRT$XLA and .CRT$XLZ) */
PIMAGE_TLS_CALLBACK __xl_a __attribute__((section(".CRT$XLA"))) = 0;
PIMAGE_TLS_CALLBACK __xl_z __attribute__((section(".CRT$XLZ"))) = 0;

const IMAGE_TLS_DIRECTORY _tls_used __attribute__((section(".rdata$T"))) =
{
	(ULONG_PTR)&_tls_start,
	(ULONG_PTR)&_tls_end,
	(ULONG_PTR)&_tls_index,
	(ULONG_PTR)(&__xl_a + 1),
	0,
	0
};
