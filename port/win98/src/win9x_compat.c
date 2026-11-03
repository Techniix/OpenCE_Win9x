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
