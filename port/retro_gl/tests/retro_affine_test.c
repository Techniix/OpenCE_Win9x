/* host test of retro_affine.c: shaders that are a line are found, the others are not */
#include <stdio.h>
#include <math.h>
#include "../src/retro_affine.h"

static int failures;

static float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static void identity(const float *d, const float *s, float f, float *r, void *c)
{ int i; (void)s; (void)f; (void)c; for (i = 0; i < 4; i++) r[i] = d[i]; }
static void tint(const float *d, const float *s, float f, float *r, void *c)
{ int i; (void)s; (void)f; (void)c; for (i = 0; i < 4; i++) r[i] = clampf(d[i] * (0.25f + 0.2f * i) + 0.1f); }
static void times4(const float *d, const float *s, float f, float *r, void *c)
{ int i; (void)s; (void)f; (void)c; for (i = 0; i < 4; i++) r[i] = clampf(d[i] * 4.0f); }
static void fade(const float *d, const float *s, float f, float *r, void *c)
{ (void)s; (void)f; (void)c; r[0] = d[0]; r[1] = d[1]; r[2] = d[2]; r[3] = d[3] * 0.5f; }
static void reads_specular(const float *d, const float *s, float f, float *r, void *c)
{ int i; (void)f; (void)c; for (i = 0; i < 4; i++) r[i] = clampf(d[i] + s[i]); }
static void reads_fog(const float *d, const float *s, float f, float *r, void *c)
{ int i; (void)s; (void)c; for (i = 0; i < 4; i++) r[i] = clampf(d[i] * (1.0f - f)); }
static void mixes(const float *d, const float *s, float f, float *r, void *c)
{ (void)s; (void)f; (void)c; r[0] = r[1] = r[2] = clampf((d[0] + d[1] + d[2]) / 3.0f); r[3] = d[3]; }
static void squares(const float *d, const float *s, float f, float *r, void *c)
{ int i; (void)s; (void)f; (void)c; for (i = 0; i < 4; i++) r[i] = d[i] * d[i]; }
static void threshold(const float *d, const float *s, float f, float *r, void *c)
{ int i; (void)s; (void)f; (void)c; for (i = 0; i < 4; i++) r[i] = d[3] >= 0.5f ? d[i] : 0.0f; }
static void constant(const float *d, const float *s, float f, float *r, void *c)
{ int i; (void)d; (void)s; (void)f; (void)c; for (i = 0; i < 4; i++) r[i] = 0.5f; }

static void expect(const char *name, retro_affine_run run, int valid)
{
	struct retro_affine line;

	retro_affine_fit(&line, run, 0);
	if (line.valid != valid)
	{
		printf("FAIL %s: valid %d, wanted %d\n", name, line.valid, valid);
		failures++;
	}
	else if (valid)
	{
		/* the line must reproduce the shader */
		float d[4] = { 0.3f, 0.6f, 0.9f, 0.5f }, s[4] = { 0.1f, 0.2f, 0.3f, 0.4f }, r[4];
		int i;

		run(d, s, 0.5f, r, 0);
		for (i = 0; i < 4; i++)
			if (fabsf(clampf(line.scale[i] * d[i] + line.offset[i]) - r[i]) > 0.004f)
			{
				printf("FAIL %s: component %d differs\n", name, i);
				failures++;
			}
	}
}

int main(void)
{
	expect("identity", identity, 1);
	expect("tint", tint, 1);
	expect("times4", times4, 1);
	expect("fade", fade, 1);
	expect("constant", constant, 1);
	expect("reads_specular", reads_specular, 0);
	expect("reads_fog", reads_fog, 0);
	expect("mixes", mixes, 0);
	expect("squares", squares, 0);
	expect("threshold", threshold, 0);
	puts(failures ? "failed" : "ok");
	return failures != 0;
}
