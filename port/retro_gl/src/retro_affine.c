/*
RETRO_AFFINE.C

Most draws' pixel shaders reduce, for a vertex, to the diffuse color times a
constant (or the diffuse itself, or a tint, a fade, a constant added). The
combiners are slow on the processor and run for each vertex, so for such a
draw this finds the scale and offset of each component once and the vertices
then take a multiply and an add. The fit is checked against inputs chosen to
catch anything else (another register read, the components mixed, a branch on
the alpha, a curve): a shader that does not hold is left to the combiners,
whose result is then the exact one.
*/

#include "retro_affine.h"

#define CHECK_COUNT 48
#define TOLERANCE 0.006f /* (a vertex color is a byte: 1/255 is 0.0039) */

static float clamped(float value)
{
	return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

/* a small deterministic generator: the same shader gets the same checks */
static unsigned long next_random(unsigned long *state)
{
	*state = *state * 1664525UL + 1013904223UL;
	return (*state >> 16) & 0xffUL;
}

void retro_affine_fit(struct retro_affine *line, retro_affine_run run, void *context)
{
	float diffuse[4], specular[4], low[4], high[4], fog;
	const float step = 16.0f / 255.0f;
	unsigned long state = 12345UL, check;
	int component;

	line->valid = 0;
	/* the fit: the same diffuse in every component, small enough that a scale
	of up to four does not reach the clamp, and a fixed specular and fog */
	for (component = 0; component < 4; component++)
	{
		diffuse[component] = 16.0f / 255.0f;
		specular[component] = 77.0f / 255.0f;
	}
	fog = 51.0f / 255.0f;
	run(diffuse, specular, fog, low, context);
	for (component = 0; component < 4; component++)
		diffuse[component] = 32.0f / 255.0f;
	run(diffuse, specular, fog, high, context);
	for (component = 0; component < 4; component++)
	{
		line->scale[component] = (high[component] - low[component]) / step;
		line->offset[component] = low[component] - line->scale[component] * step;
	}
	/* the checks: every kind of input, the ends of the range among them */
	for (check = 0; check < CHECK_COUNT; check++)
	{
		float result[4];

		for (component = 0; component < 4; component++)
		{
			unsigned long pick = next_random(&state);

			diffuse[component] = (check % 4 == 0 && pick < 64) ? 0.0f : (check % 4 == 1 && pick < 64) ? 1.0f :
				pick * (1.0f / 255.0f);
			specular[component] = next_random(&state) * (1.0f / 255.0f);
		}
		fog = next_random(&state) * (1.0f / 255.0f);
		run(diffuse, specular, fog, result, context);
		for (component = 0; component < 4; component++)
		{
			float expected = clamped(line->scale[component] * diffuse[component] + line->offset[component]);
			float difference = result[component] - expected;

			if (difference > TOLERANCE || difference < -TOLERANCE)
				return;
		}
	}
	line->valid = 1;
}
