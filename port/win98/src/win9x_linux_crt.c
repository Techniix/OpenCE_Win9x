/*
WIN9X_LINUX_CRT.C

The Windows 9x build's game units are compiled as the Linux build's are
(port/win98/include/halo_win9x_prefix.h), so they call the C runtime's file
and printf functions under the Linux build's names
(port/linux/include/stdio.h, fcntl.h: halo_linux_fopen, halo_linux_printf
...). Its platform layer is the Windows build's, which translates Xbox paths
in port/windows/src/windows_crt.c: these pass the calls on to it, and to
msvcrt.dll, whose printf takes the MSVC formats the game writes (%I64d) as
they are (the Linux build's port/linux/src/msvc_crt.c translates them for
glibc).
*/

#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>

FILE *halo_windows_fopen(const char *path, const char *mode);
FILE *halo_windows_freopen(const char *path, const char *mode, FILE *stream);
int halo_windows_remove(const char *path);
int halo_windows_rename(const char *old_path, const char *new_path);
int halo_windows_open(const char *path, int flags, ...);

/* ---------- files by Xbox path */

FILE *halo_linux_fopen(const char *path, const char *mode)
{
	return halo_windows_fopen(path, mode);
}

FILE *halo_linux_freopen(const char *path, const char *mode, FILE *stream)
{
	return halo_windows_freopen(path, mode, stream);
}

int halo_linux_remove(const char *path)
{
	return halo_windows_remove(path);
}

int halo_linux_rename(const char *old_path, const char *new_path)
{
	return halo_windows_rename(old_path, new_path);
}

int halo_linux_open(const char *path, int flags, ...)
{
	int mode = 0;

	if (flags & O_CREAT)
	{
		va_list arguments;

		va_start(arguments, flags);
		mode = va_arg(arguments, int);
		va_end(arguments);
	}
	return halo_windows_open(path, flags, mode);
}

/* ---------- printf: msvcrt's, as it is */

int halo_linux_vsnprintf(char *buffer, size_t count, const char *format, va_list arguments)
{
	int result;

	if (!count)
		return _vsnprintf(buffer, 0, format, arguments);
	/* (C99's: always terminated; msvcrt's _vsnprintf is not when it fills
	the buffer, and gives -1) */
	result = _vsnprintf(buffer, count, format, arguments);
	if (result < 0 || (size_t)result >= count)
	{
		buffer[count - 1] = '\0';
		if (result < 0)
			result = (int)count;
	}
	return result;
}

int halo_linux_snprintf(char *buffer, size_t count, const char *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = halo_linux_vsnprintf(buffer, count, format, arguments);
	va_end(arguments);
	return result;
}

int halo_linux_vsprintf(char *buffer, const char *format, va_list arguments)
{
	return vsprintf(buffer, format, arguments);
}

int halo_linux_sprintf(char *buffer, const char *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vsprintf(buffer, format, arguments);
	va_end(arguments);
	return result;
}

int halo_linux_vfprintf(FILE *stream, const char *format, va_list arguments)
{
	return vfprintf(stream, format, arguments);
}

int halo_linux_fprintf(FILE *stream, const char *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vfprintf(stream, format, arguments);
	va_end(arguments);
	return result;
}

int halo_linux_vprintf(const char *format, va_list arguments)
{
	return vfprintf(stdout, format, arguments);
}

int halo_linux_printf(const char *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vfprintf(stdout, format, arguments);
	va_end(arguments);
	return result;
}
