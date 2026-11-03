/*
WIN9X_CRT.C

C runtime functions the platform layer and its libraries call that Windows
98's msvcrt.dll (Visual C++ 6's) lacks: C99's float maths and 64-bit
conversions, and what mingw-w64's headers call that its runtime library
would define (the build links none: port/win98/build.py). The float maths
here is the platform layer's (sound, drawing, menus), not the game's, whose
maths is musl's in double precision (port/include/halo_math.h): plain
msvcrt.dll double maths is enough.
*/

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>

/* ---------- C99 float maths */

float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
float tanf(float x) { return (float)tan(x); }
float atanf(float x) { return (float)atan(x); }
float atan2f(float y, float x) { return (float)atan2(y, x); }
float sqrtf(float x) { return (float)sqrt(x); }
float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return (float)ceil(x); }
double exp2(double x) { return pow(2.0, x); }
double scalbn(double x, int n) { return ldexp(x, n); }

/* (to the nearest, ties to even: the x87's rounding as the threads run it) */
double rint(double x)
{
	double result;

	__asm__("frndint" : "=t"(result) : "0"(x));
	return result;
}

float rintf(float x)
{
	return (float)rint(x);
}

long lrintf(float x)
{
	long result;

	__asm__("fistpl %0" : "=m"(result) : "t"(x) : "st");
	return result;
}

/* ---------- 64-bit conversions (msvcrt.dll of Windows 98 has no _strtoi64) */

static unsigned long long string_to_unsigned(const char *string, char **end, int base, int *negative,
	int *overflow)
{
	const char *cursor = string;
	unsigned long long value = 0;
	int any = 0;

	*negative = 0;
	*overflow = 0;
	while (isspace((unsigned char)*cursor))
		cursor++;
	if (*cursor == '+' || *cursor == '-')
		*negative = *cursor++ == '-';
	if ((base == 0 || base == 16) && cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X') &&
		isxdigit((unsigned char)cursor[2]))
	{
		cursor += 2;
		base = 16;
	}
	else if (base == 0)
	{
		base = cursor[0] == '0' ? 8 : 10;
	}
	for (;; cursor++)
	{
		int digit;

		if (isdigit((unsigned char)*cursor))
			digit = *cursor - '0';
		else if (isalpha((unsigned char)*cursor))
			digit = tolower((unsigned char)*cursor) - 'a' + 10;
		else
			break;
		if (digit >= base)
			break;
		any = 1;
		if (value > (~0ULL - digit) / base)
			*overflow = 1;
		else
			value = value * base + digit;
	}
	if (end)
		*end = (char *)(any ? cursor : string);
	return value;
}

unsigned long long strtoull(const char *string, char **end, int base)
{
	int negative, overflow;
	unsigned long long value = string_to_unsigned(string, end, base, &negative, &overflow);

	if (overflow)
	{
		errno = ERANGE;
		return ~0ULL;
	}
	return negative ? 0 - value : value;
}

long long strtoll(const char *string, char **end, int base)
{
	int negative, overflow;
	unsigned long long value = string_to_unsigned(string, end, base, &negative, &overflow);

	if (overflow || value > (negative ? (unsigned long long)LLONG_MAX + 1 : (unsigned long long)LLONG_MAX))
	{
		errno = ERANGE;
		return negative ? LLONG_MIN : LLONG_MAX;
	}
	return negative ? (long long)(0 - value) : (long long)value;
}

/* ---------- what mingw-w64's headers call into its runtime library */

/* (atexit is its runtime's: msvcrt.dll has _onexit) */
int atexit(void (*function)(void))
{
	return _onexit((_onexit_t)function) ? 0 : -1;
}

/* (its snprintf and vsnprintf: C99's, always terminated, on msvcrt's
_vsnprintf) */
int __ms_vsnprintf(char *buffer, size_t count, const char *format, va_list arguments)
{
	int result;

	if (!count)
		return _vsnprintf(buffer, 0, format, arguments);
	result = _vsnprintf(buffer, count, format, arguments);
	if (result < 0 || (size_t)result >= count)
		buffer[count - 1] = '\0';
	return result;
}

double __strtod(const char *string, char **end)
{
	return strtod(string, end);
}

/* (ws2tcpip.h's, which ws2_32.dll of Windows 98 does not export: no IPv6) */
const struct in6_addr in6addr_any = { { { 0 } } };
