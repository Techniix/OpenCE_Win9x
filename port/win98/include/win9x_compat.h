/*
WIN9X_COMPAT.H

Declarations of the Windows functions the platform layer calls that Windows
95, 98 and Me do not have, and that port/win98/src/win9x_compat.c defines
under the same names in their place (mingw-w64's headers leave them out for
Windows 98, _WIN32_WINNT 0x0410). Force-included ahead of the Windows-facing
units (port/windows/src/win32_*.c) in the Windows 9x build, so that the
platform layer's own sources stay as the Windows build has them.
*/

#ifndef __WIN9X_COMPAT_H
#define __WIN9X_COMPAT_H

#include <windows.h>
#include <psapi.h>

/* condition variables (Vista): a semaphore and counts (win9x_compat.c) */
VOID WINAPI InitializeConditionVariable(PCONDITION_VARIABLE condition);
BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE condition, PCRITICAL_SECTION section, DWORD milliseconds);
VOID WINAPI WakeConditionVariable(PCONDITION_VARIABLE condition);
VOID WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE condition);

/* (Vista) a named pipe's server: unknown, FALSE */
BOOL WINAPI GetNamedPipeServerProcessId(HANDLE pipe, PULONG process_id);
/* (Windows 7, psapi's on Windows 2000 and XP) a process's memory: FALSE */
BOOL WINAPI K32GetProcessMemoryInfo(HANDLE process, PPROCESS_MEMORY_COUNTERS counters, DWORD size);

#endif
