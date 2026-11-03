/*
WIN9X_START.C

The Windows 9x build's start-up, in place of mingw-w64's runtime (which is
not linked: the build uses msvcrt.dll as Windows 98 has it, and nothing
newer). The entry point:
- puts the x87 in single precision, as the game's code runs (hybrid
  precision, port/win98/README.md), before any of it runs;
- starts the trace (win9x_watch.c, halo_trace.txt);
- sends stderr, the game's log, to halo.log beside halo.exe (the program's
  name, .log; a program for the Windows subsystem has no console);
- gets the command line from msvcrt.dll (__getmainargs);
- runs main, which runs the static constructors first (__main, which clang
  calls at main's start on the GNU target), and exits with what it returns.
The constructors are the ones lld lists from the objects' .ctors sections,
by priority (__CTOR_LIST__: a count or -1, the functions, then 0).
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "win9x_watch.h"

typedef void (*constructor)(void);

/* (lld's, from the .ctors and .dtors sections) */
extern constructor __CTOR_LIST__[];
extern constructor __DTOR_LIST__[];

struct startup_information
{
	int new_mode;
};

extern int __getmainargs(int *argc, char ***argv, char ***environment, int wildcard,
	struct startup_information *information);
extern void __set_app_type(int type);
extern void x87_precision_single(void);
extern int main(int argc, char **argv);

static void destructors_run(void)
{
	constructor *entry;

	for (entry = __DTOR_LIST__ + 1; *entry; entry++)
		(*entry)();
}

void __main(void)
{
	static int done;
	long count;

	if (done)
		return;
	done = 1;
	/* (in reverse, as GNU ld's mingw runtime runs them: the last listed,
	the highest priority, first) */
	for (count = 0; __CTOR_LIST__[count + 1]; count++)
		;
	while (count > 0)
		__CTOR_LIST__[count--]();
	atexit(destructors_run);
	win9x_trace("constructors run");
}

/* (as the Windows build's release does, win32_posix.c, which leaves a
stderr that goes somewhere as it is: msvcrt.dll's stderr always has a
descriptor, so that test does not see that there is no console) */
static void log_open(void)
{
	HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
	char path[MAX_PATH];
	DWORD length;
	char *slash;

	if (handle && handle != INVALID_HANDLE_VALUE)
		return;
	/* (the program's name, .log for .exe: halo.log) */
	length = GetModuleFileNameA(NULL, path, sizeof(path) - 4);
	slash = length && length < sizeof(path) - 4 ? strrchr(path, '.') : NULL;
	if (!slash || strchr(slash, '\\'))
		return;
	strcpy(slash, ".log");
	if (freopen(path, "w", stderr))
		setvbuf(stderr, NULL, _IONBF, 0);
	win9x_trace(stderr ? "log: opened" : "log: none");
}

/* the entry point (the linker's --entry), with EBP cleared: the game's
stack walk (source/cseries/stack_walk_windows.c, an assert's report) follows
the frame pointers up until one is 0, and Windows 9x starts a program with
EBP pointing into its own memory, which the program cannot read */
__asm__(".globl _mainCRTStartup\n"
	"_mainCRTStartup:\n"
	"\txorl %ebp, %ebp\n"
	"\tjmp _win9x_start\n");

__attribute__((noreturn)) void win9x_start(void);

void win9x_start(void)
{
	struct startup_information information = { 0 };
	int argc;
	char **argv;
	char **environment;

	x87_precision_single();
	win9x_watch_start();
	win9x_trace("start");
	/* (_GUI_APP) */
	__set_app_type(2);
	__getmainargs(&argc, &argv, &environment, 0, &information);
	log_open();
	__main();
	win9x_trace("main");
	exit(main(argc, argv));
}
