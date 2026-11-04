/*
RETRO_GAMMA.C

render.gamma: the display's brightness, for the monitors and cards of the
time, which are darker than the Xbox's television (Quake's and Unreal's
brightness slider). 1.0 changes nothing, and until the setting has been moved
the display's ramp is not touched at all.

The ramp is made here and set by the backend (retro_sdl_set_gamma_ramp,
retro_sdl_backend.h: Windows' gdi32 ramp in port/retro_sdl/src/win32.c; Mac
OS 9 and DOS to come), which puts the display's own back as the program ends.
*/

#include "retro_internal.h"
#include "port_config.h"

/* retro_sdl_backend.h's (port/retro_sdl/src/win32.c), declared here as this library is not built with SDL's headers */
int retro_sdl_set_gamma_ramp(const unsigned short ramp[3][256]);

#include <math.h>

/* once a frame: applies render.gamma (0.5 to 2.0) when the settings have changed */
void retro_gamma_update(void)
{
	static unsigned long seen = (unsigned long)-1;
	static int ever_moved;
	unsigned short ramp[3][256];
	double gamma;
	int index;

	if (seen == config_changes())
		return;
	seen = config_changes();
	gamma = config_real("render.gamma");
	if (!(gamma >= 0.5))
		gamma = gamma == 0.0 ? 1.0 : 0.5;
	if (gamma > 2.0)
		gamma = 2.0;
	/* (1.0 from the start: the display's ramp is left as it is) */
	if (gamma == 1.0 && !ever_moved)
		return;
	ever_moved = 1;
	for (index = 0; index < 256; index++)
	{
		double value = gamma == 1.0 ? index / 255.0 : pow(index / 255.0, 1.0 / gamma);

		ramp[0][index] = ramp[1][index] = ramp[2][index] = (unsigned short)(value * 65535.0 + 0.5);
	}
	retro_sdl_set_gamma_ramp(ramp);
}
