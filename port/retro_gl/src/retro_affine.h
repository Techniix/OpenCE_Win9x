/*
RETRO_AFFINE.H

Finds out whether a pixel shader's combiners, run on the processor for each
vertex (retro_pixel.c), are only a scale and an offset of the vertex's color
(retro_affine.c).
*/

#ifndef __RETRO_AFFINE_H
#define __RETRO_AFFINE_H

/* the combiners' result for these inputs (each RGBA, 0 to 1) */
typedef void (*retro_affine_run)(const float *diffuse, const float *specular, float fog, float *result,
	void *context);

/* each component of the result = clamp(scale * the diffuse's component + offset, 0, 1) */
struct retro_affine
{
	int valid;
	float scale[4];
	float offset[4];
};

/* fits the line from the shader and checks it against many inputs (all
three kinds, so that a shader reading the specular or the fog, or mixing the
components, is not taken for a line); valid is 0 when it does not hold */
void retro_affine_fit(struct retro_affine *line, retro_affine_run run, void *context);

#endif
