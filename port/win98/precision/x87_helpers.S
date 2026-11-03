/* hybrid precision: the x87 runs in single precision; these do the game's
double-precision arithmetic in double precision, and call the maths library
in double precision, so every result matches SSE2's bit for bit
(port/win98/README.md). C's names have a leading underscore on 32-bit
Windows. */

#ifdef _WIN32
#define SYM(name) _##name
#else
#define SYM(name) name
#endif

	.data
	.globl SYM(x87_pc24)
	.globl SYM(x87_pc53)
/* single precision, round to nearest, every exception masked: how the
game's threads run */
SYM(x87_pc24): .short 0x007F
/* double precision, for the helpers' operations */
SYM(x87_pc53): .short 0x027F
	.text

/* void x87_precision_single(void): the calling thread to single precision
(each thread that runs the game's code, as it starts) */
	.globl SYM(x87_precision_single)
SYM(x87_precision_single): fldcw SYM(x87_pc24); ret
#define BINARY(name, op) \
	.globl SYM(name); SYM(name): fldcw SYM(x87_pc53); fldl 4(%esp); op 12(%esp); fldcw SYM(x87_pc24); ret
BINARY(x87_dadd, faddl)
BINARY(x87_dsub, fsubl)
BINARY(x87_dmul, fmull)
BINARY(x87_ddiv, fdivl)
	.globl SYM(x87_dsqrt)
SYM(x87_dsqrt): fldcw SYM(x87_pc53); fldl 4(%esp); fsqrt; fldcw SYM(x87_pc24); ret
#define CALL1(name) \
	.globl SYM(x87_##name); SYM(x87_##name): fldcw SYM(x87_pc53); pushl 8(%esp); pushl 8(%esp); call SYM(name); addl $8, %esp; fldcw SYM(x87_pc24); ret
#define CALL2(name) \
	.globl SYM(x87_##name); SYM(x87_##name): fldcw SYM(x87_pc53); pushl 16(%esp); pushl 16(%esp); pushl 16(%esp); pushl 16(%esp); call SYM(name); addl $16, %esp; fldcw SYM(x87_pc24); ret
CALL1(halo_sin)
CALL1(halo_cos)
CALL1(halo_tan)
CALL1(halo_asin)
CALL1(halo_acos)
CALL1(halo_atan)
CALL1(halo_exp)
CALL1(halo_log)
CALL1(halo_log2)
CALL1(halo_log10)
CALL2(halo_atan2)
CALL2(halo_pow)
CALL2(fmod)
/* an integer converted to single precision: fild loads it exactly; storing
it rounds it to single as cvtsi2ss does */
	.globl SYM(x87_round_f32)
SYM(x87_round_f32): flds 4(%esp); ret

CALL1(sqrt)
