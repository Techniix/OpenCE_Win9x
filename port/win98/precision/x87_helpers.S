/* hybrid precision: the x87 runs in single precision; these do the game's
double-precision arithmetic in double precision, and call the maths library
in double precision, so every result matches SSE2's bit for bit */
	.text
	.globl x87_pc53
	.data
x87_pc53: .short 0x027F
	.text
#define BINARY(name, op) \
	.globl name; name: fldcw x87_pc53; fldl 4(%esp); op 12(%esp); fldcw x87_pc24; ret
BINARY(x87_dadd, faddl)
BINARY(x87_dsub, fsubl)
BINARY(x87_dmul, fmull)
BINARY(x87_ddiv, fdivl)
	.globl x87_dsqrt
x87_dsqrt: fldcw x87_pc53; fldl 4(%esp); fsqrt; fldcw x87_pc24; ret
#define CALL1(name) \
	.globl x87_##name; x87_##name: fldcw x87_pc53; pushl 8(%esp); pushl 8(%esp); call name; addl $8, %esp; fldcw x87_pc24; ret
#define CALL2(name) \
	.globl x87_##name; x87_##name: fldcw x87_pc53; pushl 16(%esp); pushl 16(%esp); pushl 16(%esp); pushl 16(%esp); call name; addl $16, %esp; fldcw x87_pc24; ret
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
	.globl x87_round_f32
x87_round_f32: flds 4(%esp); ret

CALL1(sqrt)
