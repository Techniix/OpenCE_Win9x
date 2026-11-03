/*
WIN9X_COMPAT.C

The Windows functions the platform layer calls that Windows 95, 98 and Me
do not have (port/win98/include/win9x_compat.h), defined here under the same
names, so that the linker takes these instead of imports Windows 9x cannot
resolve; and the C runtime functions msvcrt.dll of Windows 98 lacks.
*/

#include <windows.h>
#include <wincrypt.h>
#include <errno.h>
#include <io.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../include/win9x_compat.h"

/* ---------- condition variables

A CONDITION_VARIABLE is one pointer, cleared by InitializeConditionVariable
(or by being zero, CONDITION_VARIABLE_INIT): it points at this, made on
first use. A waiter counts itself in and waits on the semaphore; a wake
releases the semaphore once for each waiter not already released. A waiter
whose wait timed out as it was released leaves a release behind, which the
next waiter takes: a spurious wakeup, which condition variables allow (their
users wait in a loop on their condition). */

struct win9x_condition
{
	CRITICAL_SECTION lock;
	HANDLE semaphore;
	/* waiting, and released but not yet woken */
	LONG waiters;
	LONG released;
};

static struct win9x_condition *condition_get(PCONDITION_VARIABLE condition)
{
	struct win9x_condition *made;
	struct win9x_condition *current = (struct win9x_condition *)condition->Ptr;

	if (current)
		return current;
	made = (struct win9x_condition *)calloc(1, sizeof(*made));
	if (!made)
		return NULL;
	InitializeCriticalSection(&made->lock);
	made->semaphore = CreateSemaphoreA(NULL, 0, 0x7FFFFFFF, NULL);
	current = (struct win9x_condition *)InterlockedCompareExchangePointer(&condition->Ptr, made, NULL);
	if (current)
	{
		/* (another thread made one first) */
		CloseHandle(made->semaphore);
		DeleteCriticalSection(&made->lock);
		free(made);
		return current;
	}
	return made;
}

VOID WINAPI InitializeConditionVariable(PCONDITION_VARIABLE condition)
{
	condition->Ptr = NULL;
}

BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE condition, PCRITICAL_SECTION section, DWORD milliseconds)
{
	struct win9x_condition *state = condition_get(condition);
	DWORD result;

	if (!state)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return FALSE;
	}
	EnterCriticalSection(&state->lock);
	state->waiters++;
	LeaveCriticalSection(&state->lock);

	LeaveCriticalSection(section);
	result = WaitForSingleObject(state->semaphore, milliseconds);
	EnterCriticalSection(section);

	EnterCriticalSection(&state->lock);
	state->waiters--;
	if (result == WAIT_OBJECT_0 && state->released > 0)
		state->released--;
	LeaveCriticalSection(&state->lock);

	if (result != WAIT_OBJECT_0)
	{
		SetLastError(result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
		return FALSE;
	}
	return TRUE;
}

static void condition_wake(PCONDITION_VARIABLE condition, BOOL all)
{
	struct win9x_condition *state = condition_get(condition);
	LONG count;

	if (!state)
		return;
	EnterCriticalSection(&state->lock);
	count = state->waiters - state->released;
	if (count > 0)
	{
		if (!all)
			count = 1;
		state->released += count;
		ReleaseSemaphore(state->semaphore, count, NULL);
	}
	LeaveCriticalSection(&state->lock);
}

VOID WINAPI WakeConditionVariable(PCONDITION_VARIABLE condition)
{
	condition_wake(condition, FALSE);
}

VOID WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE condition)
{
	condition_wake(condition, TRUE);
}

/* ---------- processes */

BOOL WINAPI GetNamedPipeServerProcessId(HANDLE pipe, PULONG process_id)
{
	(void)pipe;
	*process_id = 0;
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL WINAPI K32GetProcessMemoryInfo(HANDLE process, PPROCESS_MEMORY_COUNTERS counters, DWORD size)
{
	(void)process;
	(void)counters;
	(void)size;
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

/* ---------- the C runtime: what Windows 98's msvcrt.dll lacks */

/* localtime_r on msvcrt's localtime, whose result each thread has its own
of */
struct tm *localtime_r(const time_t *time, struct tm *result)
{
	struct tm *local = localtime(time);

	if (!local)
		return NULL;
	*result = *local;
	return result;
}

/* rand_s (Windows XP's msvcrt) on CryptoAPI, which Windows 98 has (Internet
Explorer's): expat's hash salt (port/third_party/expat/random_rand_s.c) */
errno_t rand_s(unsigned int *value)
{
	static HCRYPTPROV provider;

	if (!value)
		return EINVAL;
	if (!provider &&
		!CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
	{
		return EINVAL;
	}
	if (!CryptGenRandom(provider, sizeof(*value), (BYTE *)value))
		return EINVAL;
	return 0;
}

/* the Universal C runtime's stdio streams (__acrt_iob_func: what mingw's
headers call stdin, stdout and stderr through) on msvcrt.dll's _iob */
#undef _iob
__declspec(dllimport) extern FILE _iob[];

FILE *__cdecl __acrt_iob_func(unsigned index)
{
	return &_iob[index];
}

/* (Visual C++ 2005's runtime and later: msvcrt.dll of Windows 98 has none
of these. The bounds-checked functions do what the plain ones do, within
their bounds) */

typedef void (__cdecl *invalid_parameter_handler)(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int,
	uintptr_t);

invalid_parameter_handler __cdecl _set_invalid_parameter_handler(invalid_parameter_handler handler)
{
	(void)handler;
	return NULL;
}

int __cdecl _set_fmode(int mode)
{
	extern __declspec(dllimport) int _fmode;

	_fmode = mode;
	return 0;
}

errno_t __cdecl _localtime32_s(struct tm *result, const long *time)
{
	struct tm *local;
	time_t value = (time_t)*time;

	if (!result || !time)
		return EINVAL;
	local = localtime(&value);
	if (!local)
		return EINVAL;
	*result = *local;
	return 0;
}

errno_t __cdecl _gmtime32_s(struct tm *result, const long *time)
{
	struct tm *utc;
	time_t value = (time_t)*time;

	if (!result || !time)
		return EINVAL;
	utc = gmtime(&value);
	if (!utc)
		return EINVAL;
	*result = *utc;
	return 0;
}

/* (not msvcrt.dll's _chsize, which makes a file longer by writing zeros
to it: the game sizes its six map cache files, z\cache000.map to
cache005.map, 732 MB in all, as it starts; SetEndOfFile gives a file its
clusters without writing them, as the Xbox's did) */
errno_t __cdecl _chsize_s(int handle, long long size)
{
	HANDLE file = (HANDLE)_get_osfhandle(handle);
	LONG high = 0, position_high = 0;
	DWORD position;
	errno_t result = 0;

	if (size < 0 || file == INVALID_HANDLE_VALUE)
		return EINVAL;
	position = SetFilePointer(file, 0, &position_high, FILE_CURRENT);
	if (position == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
		return EBADF;
	high = (LONG)(size >> 32);
	if ((SetFilePointer(file, (LONG)size, &high, FILE_BEGIN) == INVALID_SET_FILE_POINTER &&
		GetLastError() != NO_ERROR) || !SetEndOfFile(file))
	{
		result = GetLastError() == ERROR_DISK_FULL ? ENOSPC : EACCES;
	}
	SetFilePointer(file, (LONG)position, &position_high, FILE_BEGIN);
	return result;
}

errno_t __cdecl freopen_s(FILE **stream, const char *path, const char *mode, FILE *old_stream)
{
	FILE *result = freopen(path, mode, old_stream);

	if (stream)
		*stream = result;
	return result ? 0 : errno;
}

int __cdecl vsnprintf_s(char *buffer, size_t size, size_t count, const char *format, va_list arguments)
{
	size_t limit = count < size ? count + 1 : size;
	int result;

	if (!buffer || !size)
		return -1;
	result = _vsnprintf(buffer, limit, format, arguments);
	if (result < 0 || (size_t)result >= limit)
	{
		buffer[limit - 1] = '\0';
		return -1;
	}
	return result;
}

/* ---------- vectored exception handlers (Windows XP)

The platform layer catches writes to the textures it watches with one
(port/windows/src/win32_memory_watch.c). On Windows 9x they are run from
the top-level filter, which the system calls for a fault no structured
handler takes (and the game's code has none): the handlers first, then the
filter the program set, if any (the crash reporter's). This
SetUnhandledExceptionFilter keeps that filter, as the program sees it,
while the system's stays this one. */

enum
{
	MAXIMUM_VECTORED_HANDLERS = 8
};

static PVECTORED_EXCEPTION_HANDLER vectored_handlers[MAXIMUM_VECTORED_HANDLERS];
static LONG vectored_handler_count;
static LPTOP_LEVEL_EXCEPTION_FILTER program_filter;
static LONG filter_installed;

typedef LPTOP_LEVEL_EXCEPTION_FILTER (WINAPI *set_filter_proc)(LPTOP_LEVEL_EXCEPTION_FILTER);

static LONG WINAPI vectored_filter(EXCEPTION_POINTERS *information)
{
	LONG index;

	for (index = 0; index < vectored_handler_count; index++)
	{
		if (vectored_handlers[index] &&
			vectored_handlers[index](information) == EXCEPTION_CONTINUE_EXECUTION)
		{
			return EXCEPTION_CONTINUE_EXECUTION;
		}
	}
	return program_filter ? program_filter(information) : EXCEPTION_CONTINUE_SEARCH;
}

static void vectored_filter_install(void)
{
	set_filter_proc set_filter;

	if (InterlockedExchange(&filter_installed, 1))
		return;
	set_filter = (set_filter_proc)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetUnhandledExceptionFilter");
	if (set_filter)
		set_filter(vectored_filter);
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter)
{
	LPTOP_LEVEL_EXCEPTION_FILTER previous = program_filter;

	program_filter = filter;
	vectored_filter_install();
	return previous;
}

PVOID WINAPI AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler)
{
	LONG index = InterlockedIncrement(&vectored_handler_count) - 1;

	if (index >= MAXIMUM_VECTORED_HANDLERS)
	{
		InterlockedDecrement(&vectored_handler_count);
		return NULL;
	}
	if (first && index > 0)
	{
		/* (first: before the others) */
		memmove(&vectored_handlers[1], &vectored_handlers[0], index * sizeof(vectored_handlers[0]));
		vectored_handlers[0] = handler;
	}
	else
	{
		vectored_handlers[index] = handler;
	}
	vectored_filter_install();
	return (PVOID)handler;
}

ULONG WINAPI RemoveVectoredExceptionHandler(PVOID handle)
{
	LONG index;

	for (index = 0; index < vectored_handler_count; index++)
	{
		if ((PVOID)vectored_handlers[index] == handle)
		{
			vectored_handlers[index] = NULL;
			return 1;
		}
	}
	return 0;
}

/* ---------- files and memory (Windows 2000) */

BOOL WINAPI GetFileSizeEx(HANDLE file, PLARGE_INTEGER size)
{
	DWORD high = 0;
	DWORD low = GetFileSize(file, &high);

	if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
		return FALSE;
	size->LowPart = low;
	size->HighPart = (LONG)high;
	return TRUE;
}

BOOL WINAPI SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER new_position, DWORD method)
{
	LONG high = distance.HighPart;
	DWORD low;

	SetLastError(NO_ERROR);
	low = SetFilePointer(file, (LONG)distance.LowPart, &high, method);
	if (low == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
		return FALSE;
	if (new_position)
	{
		new_position->LowPart = low;
		new_position->HighPart = high;
	}
	return TRUE;
}

BOOL WINAPI GlobalMemoryStatusEx(LPMEMORYSTATUSEX status)
{
	MEMORYSTATUS memory;

	memory.dwLength = sizeof(memory);
	GlobalMemoryStatus(&memory);
	status->dwMemoryLoad = memory.dwMemoryLoad;
	status->ullTotalPhys = memory.dwTotalPhys;
	status->ullAvailPhys = memory.dwAvailPhys;
	status->ullTotalPageFile = memory.dwTotalPageFile;
	status->ullAvailPageFile = memory.dwAvailPageFile;
	status->ullTotalVirtual = memory.dwTotalVirtual;
	status->ullAvailVirtual = memory.dwAvailVirtual;
	status->ullAvailExtendedVirtual = 0;
	return TRUE;
}

/* ---------- what there is nothing of on Windows 9x: fail as unsupported */

BOOL WINAPI AttachConsole(DWORD process)
{
	(void)process;
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL WINAPI SetThreadStackGuarantee(PULONG size)
{
	(void)size;
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

UINT WINAPI GetSystemFirmwareTable(DWORD provider, DWORD table, PVOID buffer, DWORD size)
{
	(void)provider;
	(void)table;
	(void)buffer;
	(void)size;
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return 0;
}

/* (DbgHelp: crash reports go without a minidump) */
BOOL WINAPI MiniDumpWriteDump(HANDLE process, DWORD process_id, HANDLE file, int type, PVOID exception,
	PVOID user_stream, PVOID callback)
{
	(void)process;
	(void)process_id;
	(void)file;
	(void)type;
	(void)exception;
	(void)user_stream;
	(void)callback;
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

/* (BCrypt, Mbed TLS's entropy: CryptoAPI's, as rand_s) */
LONG WINAPI BCryptGenRandom(PVOID algorithm, PUCHAR buffer, ULONG size, ULONG flags)
{
	static HCRYPTPROV provider;

	(void)algorithm;
	(void)flags;
	if (!provider &&
		!CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
	{
		return (LONG)0xC0000001;
	}
	return CryptGenRandom(provider, size, buffer) ? 0 : (LONG)0xC0000001;
}

/* (WinHTTP: the updater's and the crash reporter's uploads, which Windows
9x goes without; a session that cannot be opened fails them as they fail
offline) */
PVOID WINAPI WinHttpOpen(LPCWSTR agent, DWORD access, LPCWSTR proxy, LPCWSTR bypass, DWORD flags)
{
	(void)agent;
	(void)access;
	(void)proxy;
	(void)bypass;
	(void)flags;
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return NULL;
}

#define WIN9X_WINHTTP_FAILS(name, parameters) \
	BOOL WINAPI name parameters { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

WIN9X_WINHTTP_FAILS(WinHttpCloseHandle, (PVOID handle))
WIN9X_WINHTTP_FAILS(WinHttpCrackUrl, (LPCWSTR url, DWORD length, DWORD flags, PVOID components))
WIN9X_WINHTTP_FAILS(WinHttpQueryHeaders, (PVOID request, DWORD level, LPCWSTR name, PVOID buffer, LPDWORD length,
	LPDWORD index))
WIN9X_WINHTTP_FAILS(WinHttpReadData, (PVOID request, PVOID buffer, DWORD size, LPDWORD read))
WIN9X_WINHTTP_FAILS(WinHttpReceiveResponse, (PVOID request, PVOID reserved))
WIN9X_WINHTTP_FAILS(WinHttpSendRequest, (PVOID request, LPCWSTR headers, DWORD headers_length, PVOID optional,
	DWORD optional_length, DWORD total_length, DWORD_PTR context))
WIN9X_WINHTTP_FAILS(WinHttpSetOption, (PVOID handle, DWORD option, PVOID buffer, DWORD length))
WIN9X_WINHTTP_FAILS(WinHttpSetTimeouts, (PVOID handle, int resolve, int connect, int send, int receive))

PVOID WINAPI WinHttpConnect(PVOID session, LPCWSTR server, WORD port, DWORD reserved)
{
	(void)session;
	(void)server;
	(void)port;
	(void)reserved;
	SetLastError(ERROR_INVALID_HANDLE);
	return NULL;
}

PVOID WINAPI WinHttpOpenRequest(PVOID connection, LPCWSTR verb, LPCWSTR object, LPCWSTR version, LPCWSTR referrer,
	LPCWSTR *types, DWORD flags)
{
	(void)connection;
	(void)verb;
	(void)object;
	(void)version;
	(void)referrer;
	(void)types;
	(void)flags;
	SetLastError(ERROR_INVALID_HANDLE);
	return NULL;
}
