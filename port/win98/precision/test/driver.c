/* determinism test: the game's own maths, same inputs, hash of outputs per function */
#include "cseries.h"
#include "real_math.h"
#include "periodic_functions.h"

static unsigned long rng = 0x12345678;
static unsigned long next(void) { rng = rng * 1664525UL + 1013904223UL; return rng; }
static real rf(void)
{
	unsigned long b = next();
	unsigned long e = 121 + (next() % 12);
	union { unsigned long u; real f; } v;
	v.u = (b & 0x807FFFFF) | (e << 23);
	return v.f;
}
static real unit(void) { union { unsigned long u; real f; } v; v.u = 0x3F800000 | (next() & 0x7FFFFF); return v.f - 1.0f; }
static void rv(real_vector3d *v) { v->i = rf(); v->j = rf(); v->k = rf(); }
static void rq(real_quaternion *q) { q->v.i = rf(); q->v.j = rf(); q->v.k = rf(); q->w = rf(); }

static unsigned long h;
static void mix(const void *p, int n) { const unsigned char *c = p; while (n--) { h ^= *c++; h *= 16777619UL; } }

static int sys_write(int fd, const void *b, int n) { int r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(4), "b"(fd), "c"(b), "d"(n) : "memory"); return r; }
static void put(const char *s) { int n = 0; while (s[n]) n++; sys_write(1, s, n); }
static void report(const char *name)
{
	char buf[12]; int i;
	put(name); put(" ");
	for (i = 0; i < 8; i++) { int d = (h >> (28 - 4 * i)) & 15; buf[i] = d < 10 ? '0' + d : 'a' + d - 10; }
	buf[8] = '\n'; buf[9] = 0; put(buf);
}

#define N 300000
#define NUMBER_OF_PERIODIC_FUNCTIONS 12
#define BEGIN h = 2166136261UL; rng = 0x12345678;

void driver_main(void)
{
	long i; short f;
	real_vector3d a, b, out; real_quaternion q0, q1, qr; real_point3d p, po, t0, t1, t2;
	real_euler_angles3d angles; real s, x, v;

	{ double dd; real_matrix4x3 m;
	BEGIN for (i = 0; i < N; i++) { dd = sin(rf()); mix(&dd, 8); } report("halo_sin");
	BEGIN for (i = 0; i < N; i++) { dd = cos(rf()); mix(&dd, 8); } report("halo_cos");
	BEGIN for (i = 0; i < N; i++) { dd = acos(unit()); mix(&dd, 8); } report("halo_acos");
	BEGIN for (i = 0; i < N; i++) { dd = atan2(rf(), rf()); mix(&dd, 8); } report("halo_atan2");
	BEGIN for (i = 0; i < N; i++) { dd = pow(unit() + 0.5f, rf()); mix(&dd, 8); } report("halo_pow");
	BEGIN for (i = 0; i < N; i++) { angles.yaw = rf(); angles.pitch = rf(); angles.roll = rf(); matrix4x3_rotation_from_angles(&m, angles.yaw, angles.pitch, angles.roll); mix(&m, sizeof(m)); } report("matrix4x3_rotation_from_angles");
	}
	BEGIN periodic_functions_initialize();
	for (i = 0; i < N; i++) for (f = 0; f < NUMBER_OF_PERIODIC_FUNCTIONS; f++) { s = periodic_function_evaluate(f, rf()); mix(&s, 4); }
	report("periodic_function_evaluate");
	BEGIN for (i = 0; i < N; i++) for (f = 0; f < NUMBER_OF_TRANSITION_FUNCTIONS; f++) { s = transition_function_evaluate(f, unit()); mix(&s, 4); }
	report("transition_function_evaluate");
	BEGIN for (i = 0; i < N; i++) { rq(&q0); rq(&q1); quaternions_interpolate(&q0, &q1, unit(), &qr); mix(&qr, 16); }
	report("quaternions_interpolate");
	BEGIN for (i = 0; i < N; i++) { rq(&q0); rq(&q1); quaternions_interpolate_and_normalize(&q0, &q1, unit(), &qr); mix(&qr, 16); }
	report("quaternions_interpolate_and_normalize");
	BEGIN for (i = 0; i < N; i++) { rq(&q0); p.x = rf(); p.y = rf(); p.z = rf(); quaternion_transform_point(&q0, &p, &po); mix(&po, 12); }
	report("quaternion_transform_point");
	BEGIN for (i = 0; i < N; i++) { rv(&a); rv(&b); s = angle_between_vectors3d(&a, &b); mix(&s, 4); }
	report("angle_between_vectors3d");
	BEGIN for (i = 0; i < N; i++) { rv(&a); rv(&b); normalize3d(&a); normalize3d(&b); normals_interpolate(&a, &b, unit(), &out); mix(&out, 12); }
	report("normals_interpolate");
	BEGIN for (i = 0; i < N; i++) { rv(&a); rv(&b); x = unit(); yaw_vectors(&a, &b, x, 1.0f - x * x); mix(&a, 12); }
	report("yaw_vectors");
	BEGIN for (i = 0; i < N; i++) { p.x = rf(); p.y = rf(); p.z = rf(); rv(&a); t0.x = rf(); t0.y = rf(); t0.z = rf(); rv(&b); s = (real)vector_intersects_pill3d(&p, &a, &t0, &b, rf()); mix(&s, 4); }
	report("vector_intersects_pill3d");
	BEGIN for (i = 0; i < N; i++) { p.x = rf(); p.y = rf(); p.z = rf(); t0.x = rf(); t0.y = rf(); t0.z = rf(); t1.x = rf(); t1.y = rf(); t1.z = rf(); t2.x = rf(); t2.y = rf(); t2.z = rf(); f = sphere_intersects_triangle3d(&p, rf() * 4.0f, &t0, &t1, &t2); mix(&f, 2); }
	report("sphere_intersects_triangle3d");
	BEGIN for (i = 0; i < N; i++) { angles.yaw = rf(); angles.pitch = rf(); angles.roll = rf(); vectors3d_from_euler_angles3d(&a, &b, &angles); mix(&a, 12); mix(&b, 12); }
	report("vectors3d_from_euler_angles3d");
	BEGIN for (i = 0; i < N; i++) { x = rf(); v = rf(); accelerate_to_velocity(&x, &v, rf(), rf(), -64.0f, 64.0f, (boolean)(i & 1)); mix(&x, 4); mix(&v, 4); }
	report("accelerate_to_velocity");
	BEGIN for (i = 0; i < N; i++) { rv(&a); rv(&b); f = accelerate_to_velocity3d(&a, &b, unit()); mix(&f, 2); mix(&a, 12); }
	report("accelerate_to_velocity3d");
	BEGIN for (i = 0; i < N; i++) { rv(&a); s = normalize3d(&a); mix(&s, 4); mix(&a, 12); }
	report("normalize3d");
}

#ifdef X87
static unsigned short x87_start_cw = X87_START_CW;
#endif
void c_start(void)
{
#ifdef X87
	__asm__ volatile("fninit; fldcw %0" : : "m"(x87_start_cw));
#endif
	driver_main();
	__asm__ volatile("int $0x80" : : "a"(1), "b"(0));
}
__asm__(".globl _start\n_start: andl $-16, %esp\n call c_start\n");
