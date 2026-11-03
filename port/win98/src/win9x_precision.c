/*
WIN9X_PRECISION.C

The x87's control word under hybrid precision (port/win98/README.md): every
thread that runs the game's code runs in single precision, round to nearest
(x87_helpers.S: x87_pc24, x87_precision_single). The game resets the control
word to the C runtime's default on its own (real_math_reset_precision:
_control87(CW_DEFAULT, 0xFFFFF)), which in msvcrt.dll is double precision:
this _control87, which the linker takes in its place, keeps single
precision. (The SSE2 builds' _control87, port/linux/src/msvc_crt.c, sets the
SSE control register; the game's results do not depend on it there.)
*/

extern void x87_precision_single(void);

unsigned int _control87(unsigned int new_value, unsigned int mask)
{
	(void)new_value;
	(void)mask;
	x87_precision_single();
	/* (_PC_24 | _RC_NEAR | every exception masked, as msvcrt reports it) */
	return 0x0002001F;
}

/* each thread port/retro_sdl starts (SDL_CreateThreadRuntime, the sound's):
Windows starts it with the control word the C runtime's default leaves */
void retro_sdl_thread_started(void)
{
	x87_precision_single();
}
