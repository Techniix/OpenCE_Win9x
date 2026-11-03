/*
WIN9X_WATCH.C

Where the program is, while nothing else says so (the Windows 9x build's
first runs): a thread that, every few seconds, stops the main thread for a
moment, notes its instruction pointer and the return addresses on its stack
(each word there that points into halo.exe's code), and writes them to
halo_trace.txt beside halo.exe (the program's name, _trace.txt), with the
start-up's stages.
port/win98/tools/symbolize.py names them from build/win98/halo.map.

On unless halo_trace.txt cannot be written; HALO_NO_TRACE turns it off.
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "win9x_watch.h"

enum
{
	WATCH_INTERVAL_MILLISECONDS = 5000,
	WATCH_STACK_WORDS = 8192,
	WATCH_RETURN_ADDRESSES = 32
};

static HANDLE trace_file = INVALID_HANDLE_VALUE;
static HANDLE main_thread;
static DWORD code_start, code_end;
/* the main thread's stack's top (found before anything is stopped: on
Windows 9x, VirtualQuery takes a lock in the system that a stopped thread
may hold, and both then wait forever) */
static DWORD stack_top;
static DWORD started;

static void trace_write(const char *text)
{
	DWORD written;

	if (trace_file != INVALID_HANDLE_VALUE)
	{
		WriteFile(trace_file, text, (DWORD)lstrlenA(text), &written, NULL);
		WriteFile(trace_file, "\r\n", 2, &written, NULL);
	}
}

void win9x_trace(const char *stage)
{
	char prefix[32];

	/* (the stage written as it is, after its time: a watch line runs to
	some 340 characters) */
	wsprintfA(prefix, "%lu ms: ", GetTickCount() - started);
	if (trace_file != INVALID_HANDLE_VALUE)
	{
		DWORD written;

		WriteFile(trace_file, prefix, (DWORD)lstrlenA(prefix), &written, NULL);
	}
	trace_write(stage);
}

static void code_range(void)
{
	BYTE *image = (BYTE *)GetModuleHandleA(NULL);
	IMAGE_NT_HEADERS *headers = (IMAGE_NT_HEADERS *)(image + ((IMAGE_DOS_HEADER *)image)->e_lfanew);
	IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(headers);
	WORD index;

	for (index = 0; index < headers->FileHeader.NumberOfSections; index++, section++)
	{
		if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE)
		{
			DWORD start = (DWORD)image + section->VirtualAddress;
			DWORD end = start + section->Misc.VirtualSize;

			if (!code_start || start < code_start)
				code_start = start;
			if (end > code_end)
				code_end = end;
		}
	}
}

static DWORD WINAPI watch_thread(LPVOID parameter)
{
	static DWORD stack[WATCH_STACK_WORDS];

	(void)parameter;
	for (;;)
	{
		CONTEXT context;
		DWORD found[WATCH_RETURN_ADDRESSES];
		DWORD words = 0, count = 0, index;
		char line[64 + WATCH_RETURN_ADDRESSES * 10];
		char *at;
		BOOL have_context;

		Sleep(WATCH_INTERVAL_MILLISECONDS);
		/* (only memory is read while it is stopped: no lock it may hold) */
		if (SuspendThread(main_thread) == (DWORD)-1)
			return 0;
		context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
		have_context = GetThreadContext(main_thread, &context);
		if (have_context && context.Esp < stack_top && stack_top - context.Esp < 0x1000000)
		{
			words = (stack_top - context.Esp) / 4;
			if (words > WATCH_STACK_WORDS)
				words = WATCH_STACK_WORDS;
			CopyMemory(stack, (void *)context.Esp, words * 4);
		}
		ResumeThread(main_thread);

		if (!have_context)
		{
			win9x_trace("watch: no context");
			continue;
		}
		for (index = 0; index < words && count < WATCH_RETURN_ADDRESSES; index++)
		{
			if (stack[index] >= code_start && stack[index] < code_end)
				found[count++] = stack[index];
		}
		at = line;
		at += wsprintfA(at, "watch: eip %08lx esp %08lx stack", context.Eip, context.Esp);
		for (index = 0; index < count; index++)
			at += wsprintfA(at, " %08lx", found[index]);
		win9x_trace(line);
	}
}

void win9x_watch_start(void)
{
	char path[MAX_PATH];
	DWORD length;
	DWORD identifier;
	char *slash = NULL;
	char *at;

	started = GetTickCount();
	if (GetEnvironmentVariableA("HALO_NO_TRACE", path, sizeof(path)))
		return;
	/* (the program's name, _trace.txt for .exe: halo_trace.txt) */
	length = GetModuleFileNameA(NULL, path, sizeof(path) - 16);
	for (at = path; at < path + length; at++)
	{
		if (*at == '.')
			slash = at;
		else if (*at == '\\')
			slash = NULL;
	}
	if (!slash)
		return;
	lstrcpyA(slash, "_trace.txt");
	trace_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
	if (trace_file == INVALID_HANDLE_VALUE)
		return;
	code_range();
	{
		MEMORY_BASIC_INFORMATION region;

		if (VirtualQuery(&region, &region, sizeof(region)))
			stack_top = (DWORD)region.BaseAddress + region.RegionSize;
	}
	{
		char line[128];
		OSVERSIONINFOA version;

		version.dwOSVersionInfoSize = sizeof(version);
		GetVersionExA(&version);
		wsprintfA(line, "halo.exe at %08lx, code %08lx to %08lx; Windows %lu.%lu.%lu", (DWORD)GetModuleHandleA(NULL),
			code_start, code_end, version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber & 0xFFFF);
		trace_write(line);
	}
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_thread,
		THREAD_ALL_ACCESS, FALSE, 0))
	{
		win9x_trace("watch: cannot open the main thread");
		return;
	}
	if (!CreateThread(NULL, 64 * 1024, watch_thread, NULL, 0, &identifier))
		win9x_trace("watch: cannot start");
}
