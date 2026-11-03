/*
WIN98_PROBE.C

Answers the questions that decide how OpenCE can run on Windows 98, on the
machine it runs on, and writes the answers to win98_probe.txt beside it:

 1. the Xbox memory window: the game's data is linked to fixed addresses in
    0x80000000-0xA0000000 (port/linux/src/platform.h). On Windows 98 that
    range is the shared arena, where VirtualAlloc cannot reserve private
    memory. A pagefile-backed file mapping placed with MapViewOfFileEx may
    still land there; the probe tries 512, 128 and 64 MB, then checks that
    the pages read, write and take page protection (the texture watch,
    port/windows/src/win32_memory_watch.c, write-protects pages);
 2. the Custom Edition window at 0x40440000 (23 MB, private arena);
 3. the processor: SSE and SSE2, and whether Windows saves their registers
    (an SSE2 instruction runs or faults). Every networked build computes its
    floating point with SSE2, so a build without it cannot play with them;
 4. OpenGL: the driver's version, GLSL version, limits and the extensions a
    renderer for old hardware could use;
 5. which system libraries and functions the port relies on are present.

It is built without a C runtime and imports only functions every Windows 98
has (kernel32, user32, gdi32); everything else is looked up at run time.
Build: port/win98/probe/build.sh
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* ---------- no C runtime: the few functions the compiler may call */

void *memset(void *d, int c, unsigned int n)
{
	unsigned char *p = (unsigned char *)d;

	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, unsigned int n)
{
	unsigned char *p = (unsigned char *)d;
	const unsigned char *q = (const unsigned char *)s;

	while (n--)
		*p++ = *q++;
	return d;
}

/* ---------- report */

static HANDLE report_file = INVALID_HANDLE_VALUE;
static char line_buffer[1100];
static char summary[2048];

static void out(const char *text)
{
	DWORD written;

	if (report_file != INVALID_HANDLE_VALUE)
		WriteFile(report_file, text, (DWORD)lstrlenA(text), &written, NULL);
}

#define REPORT(...) do { wsprintfA(line_buffer, __VA_ARGS__); out(line_buffer); } while (0)

static void sum(const char *text)
{
	if (lstrlenA(summary) + lstrlenA(text) < (int)sizeof(summary) - 1)
		lstrcatA(summary, text);
}

/* ---------- faults: the unhandled exception filter skips the instruction
that faulted (its length set before it runs) and notes the fault */

static volatile DWORD fault_code;
static volatile DWORD fault_skip;

static LONG WINAPI fault_filter(EXCEPTION_POINTERS *info)
{
	if (!fault_skip)
		return EXCEPTION_CONTINUE_SEARCH;
	fault_code = info->ExceptionRecord->ExceptionCode;
	info->ContextRecord->Eip += fault_skip;
	fault_skip = 0;
	return EXCEPTION_CONTINUE_EXECUTION;
}

/* ---------- 1, 2: memory */

static const char *state_name(DWORD state)
{
	switch (state)
	{
	case MEM_FREE: return "free";
	case MEM_RESERVE: return "reserved";
	case MEM_COMMIT: return "committed";
	default: return "?";
	}
}

static void memory_map(DWORD from, DWORD to)
{
	DWORD address = from;
	int lines = 0;

	REPORT("  address space %08lX-%08lX:\r\n", from, to);
	while (address < to && lines < 80)
	{
		MEMORY_BASIC_INFORMATION info;
		DWORD end;

		if (!VirtualQuery((void *)address, &info, sizeof(info)))
		{
			REPORT("    %08lX: VirtualQuery failed (error %lu)\r\n", address, GetLastError());
			break;
		}
		end = (DWORD)info.BaseAddress + (DWORD)info.RegionSize;
		if (end <= address)
			break;
		REPORT("    %08lX-%08lX %-9s %6lu KB\r\n", (DWORD)info.BaseAddress, end, state_name(info.State),
			(DWORD)info.RegionSize / 1024);
		address = end;
		lines++;
	}
}

/* write, read back, write-protect and write again: what the texture watch
needs */
static void memory_exercise(BYTE *base, DWORD size)
{
	volatile DWORD *first = (volatile DWORD *)base;
	volatile DWORD *last = (volatile DWORD *)(base + size - 4);
	DWORD previous = 0;
	BOOL protect_ok;

	*first = 0x12345678;
	*last = 0x9abcdef0;
	REPORT("    write/read first and last page: %s\r\n",
		(*first == 0x12345678 && *last == 0x9abcdef0) ? "ok" : "MISMATCH");

	protect_ok = VirtualProtect(base, 4096, PAGE_READONLY, &previous);
	REPORT("    VirtualProtect(read-only): %s (error %lu, previous %08lX)\r\n", protect_ok ? "ok" : "FAILED",
		protect_ok ? 0 : GetLastError(), previous);
	if (protect_ok)
	{
		fault_code = 0;
		/* mov dword ptr [eax], 1: six bytes */
		fault_skip = 6;
		__asm__ volatile (".byte 0xC7, 0x00, 0x01, 0x00, 0x00, 0x00" : : "a"(base) : "memory");
		fault_skip = 0;
		REPORT("    write to the read-only page: %s (code %08lX)\r\n",
			fault_code ? "faulted, as wanted" : "DID NOT FAULT", fault_code);
		sum(fault_code ? "Page protection in the window: works\r\n" : "Page protection in the window: does NOT fault\r\n");
		VirtualProtect(base, 4096, PAGE_READWRITE, &previous);
	}
	else
	{
		sum("Page protection in the window: VirtualProtect fails\r\n");
	}
}

static void memory_probe(void)
{
	static const DWORD sizes[] = { 0x20000000, 0x08000000, 0x04000000 };
	void *reserved;
	int index;
	BOOL mapped_any = FALSE;

	out("\r\n== 1. Xbox memory window at 0x80000000 ==\r\n");
	memory_map(0x80000000, 0xC0000000);

	reserved = VirtualAlloc((void *)0x80000000, 0x08000000, MEM_RESERVE, PAGE_NOACCESS);
	REPORT("  VirtualAlloc reserve 128 MB at 80000000: %s (error %lu)\r\n",
		reserved == (void *)0x80000000 ? "ok" : (reserved ? "placed elsewhere" : "failed"),
		reserved ? 0 : GetLastError());
	if (reserved)
		VirtualFree(reserved, 0, MEM_RELEASE);

	for (index = 0; index < 3; index++)
	{
		DWORD size = sizes[index];
		HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, NULL);
		void *view;

		if (!mapping)
		{
			REPORT("  file mapping of %lu MB: CreateFileMapping failed (error %lu)\r\n", size >> 20, GetLastError());
			continue;
		}
		view = MapViewOfFileEx(mapping, FILE_MAP_WRITE, 0, 0, size, (void *)0x80000000);
		REPORT("  file mapping of %lu MB at 80000000: %s (error %lu)\r\n", size >> 20,
			view == (void *)0x80000000 ? "ok" : (view ? "placed elsewhere" : "failed"),
			view ? 0 : GetLastError());
		if (view == (void *)0x80000000)
		{
			wsprintfA(line_buffer, "Memory window at 0x80000000: %lu MB mapped\r\n", size >> 20);
			sum(line_buffer);
			memory_exercise((BYTE *)view, size);
			mapped_any = TRUE;
		}
		if (view)
			UnmapViewOfFile(view);
		CloseHandle(mapping);
		if (mapped_any)
			break;
	}
	if (!mapped_any)
	{
		/* not at the address: is a view possible at all, and where? */
		HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x08000000, NULL);
		void *view = mapping ? MapViewOfFileEx(mapping, FILE_MAP_WRITE, 0, 0, 0x08000000, NULL) : NULL;

		REPORT("  128 MB mapping anywhere: %08lX\r\n", (DWORD)view);
		if (view)
			UnmapViewOfFile(view);
		if (mapping)
			CloseHandle(mapping);
		sum("Memory window at 0x80000000: NOT available\r\n");
	}

	out("\r\n== 2. Custom Edition window at 0x40440000 ==\r\n");
	reserved = VirtualAlloc((void *)0x40440000, 0x01700000, MEM_RESERVE, PAGE_NOACCESS);
	REPORT("  VirtualAlloc reserve 23 MB at 40440000: %s (error %lu)\r\n",
		reserved == (void *)0x40440000 ? "ok" : (reserved ? "placed elsewhere" : "failed"),
		reserved ? 0 : GetLastError());
	sum(reserved == (void *)0x40440000 ? "Custom Edition window: ok\r\n" : "Custom Edition window: NOT available\r\n");
	if (reserved)
		VirtualFree(reserved, 0, MEM_RELEASE);
}

/* ---------- 3: processor */

static void cpuid(DWORD leaf, DWORD *a, DWORD *b, DWORD *c, DWORD *d)
{
	__asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static BOOL cpuid_present(void)
{
	DWORD before, after;

	/* the ID bit of EFLAGS toggles where CPUID exists */
	__asm__ volatile (
		"pushfl\n\tpopl %0\n\tmovl %0, %1\n\txorl $0x200000, %1\n\t"
		"pushl %1\n\tpopfl\n\tpushfl\n\tpopl %1\n\tpushl %0\n\tpopfl"
		: "=&r"(before), "=&r"(after));
	return ((before ^ after) & 0x200000) != 0;
}

static void processor_probe(void)
{
	DWORD a, b, c, d, highest;
	DWORD vendor[4];
	DWORD brand[13];
	BOOL sse, sse2;

	out("\r\n== 3. Processor ==\r\n");
	if (!cpuid_present())
	{
		out("  no CPUID\r\n");
		sum("Processor: no CPUID (too old)\r\n");
		return;
	}
	cpuid(0, &highest, &vendor[0], &vendor[2], &vendor[1]);
	vendor[3] = 0;
	REPORT("  vendor: %s\r\n", (char *)vendor);
	cpuid(1, &a, &b, &c, &d);
	sse = (d >> 25) & 1;
	sse2 = (d >> 26) & 1;
	REPORT("  family/model/stepping signature %08lX, MMX %d, SSE %d, SSE2 %d, SSE3 %d\r\n",
		a, (int)((d >> 23) & 1), (int)sse, (int)sse2, (int)(c & 1));
	cpuid(0x80000000, &a, &b, &c, &d);
	if (a >= 0x80000004)
	{
		cpuid(0x80000002, &brand[0], &brand[1], &brand[2], &brand[3]);
		cpuid(0x80000003, &brand[4], &brand[5], &brand[6], &brand[7]);
		cpuid(0x80000004, &brand[8], &brand[9], &brand[10], &brand[11]);
		brand[12] = 0;
		REPORT("  name: %s\r\n", (char *)brand);
	}

	if (sse2)
	{
		/* pxor xmm0, xmm0: four bytes. Faults (invalid instruction) where
		Windows has not enabled the SSE registers. */
		fault_code = 0;
		fault_skip = 4;
		__asm__ volatile (".byte 0x66, 0x0F, 0xEF, 0xC0" ::: "memory");
		fault_skip = 0;
		REPORT("  SSE2 instruction: %s (code %08lX)\r\n", fault_code ? "FAULTED" : "ran", fault_code);
		sum(fault_code ? "SSE2: CPU has it, Windows does NOT enable it\r\n" : "SSE2: ok (network play possible)\r\n");
	}
	else if (sse)
	{
		sum("SSE2: missing (offline-only build needed)\r\n");
	}
	else
	{
		sum("SSE2: missing, no SSE either (offline-only build needed)\r\n");
	}
}

/* ---------- 4: OpenGL */

typedef HGLRC (WINAPI *create_context_proc)(HDC);
typedef BOOL (WINAPI *make_current_proc)(HDC, HGLRC);
typedef BOOL (WINAPI *delete_context_proc)(HGLRC);
typedef const BYTE *(WINAPI *get_string_proc)(DWORD);
typedef void (WINAPI *get_integer_proc)(DWORD, int *);

static int contains_word(const char *list, const char *word)
{
	int length = lstrlenA(word);
	const char *p = list;

	while (*p)
	{
		int i = 0;

		while (word[i] && p[i] == word[i])
			i++;
		if (i == length && (p[i] == ' ' || p[i] == 0) && (p == list || p[-1] == ' '))
			return 1;
		p++;
	}
	return 0;
}

static void opengl_probe(HINSTANCE instance)
{
	static const char *const wanted[] = {
		/* a GL 2-class renderer */
		"GL_ARB_shader_objects", "GL_ARB_vertex_shader", "GL_ARB_fragment_shader",
		"GL_ARB_shading_language_100", "GL_ARB_vertex_buffer_object", "GL_ARB_pixel_buffer_object",
		"GL_EXT_framebuffer_object", "GL_ARB_texture_non_power_of_two", "GL_EXT_texture_compression_s3tc",
		"GL_ARB_occlusion_query", "GL_EXT_texture_filter_anisotropic", "GL_ARB_texture_rectangle",
		"GL_EXT_draw_range_elements", "GL_ARB_texture_env_combine", "GL_ARB_texture_cube_map",
		"GL_ARB_vertex_array_object", "GL_APPLE_vertex_array_object", "GL_ARB_map_buffer_range",
		"GL_ARB_draw_elements_base_vertex", "GL_ARB_sync", "GL_NV_fence", "GL_APPLE_fence",
		/* the Xbox GPU's own model, on NVIDIA GeForce 3/4/FX */
		"GL_NV_register_combiners", "GL_NV_register_combiners2", "GL_NV_texture_shader",
		"GL_NV_texture_shader2", "GL_NV_texture_shader3", "GL_NV_vertex_program", "GL_NV_vertex_program1_1",
		"GL_ARB_vertex_program", "GL_ARB_fragment_program", "GL_ATI_fragment_shader",
		0
	};
	HMODULE library;
	create_context_proc create_context;
	make_current_proc make_current;
	delete_context_proc delete_context;
	get_string_proc get_string;
	get_integer_proc get_integer;
	WNDCLASSA window_class;
	PIXELFORMATDESCRIPTOR format;
	HWND window;
	HDC dc;
	HGLRC context;
	int format_index;
	const char *version;
	const char *extensions;
	const char *glsl;
	int index;
	int value;

	out("\r\n== 4. OpenGL ==\r\n");
	library = LoadLibraryA("opengl32.dll");
	if (!library)
	{
		out("  no opengl32.dll\r\n");
		sum("OpenGL: no opengl32.dll\r\n");
		return;
	}
	create_context = (create_context_proc)GetProcAddress(library, "wglCreateContext");
	make_current = (make_current_proc)GetProcAddress(library, "wglMakeCurrent");
	delete_context = (delete_context_proc)GetProcAddress(library, "wglDeleteContext");
	get_string = (get_string_proc)GetProcAddress(library, "glGetString");
	get_integer = (get_integer_proc)GetProcAddress(library, "glGetIntegerv");
	if (!create_context || !make_current || !delete_context || !get_string || !get_integer)
	{
		out("  opengl32.dll lacks the basic functions\r\n");
		sum("OpenGL: broken opengl32.dll\r\n");
		return;
	}

	memset(&window_class, 0, sizeof(window_class));
	window_class.style = CS_OWNDC;
	window_class.lpfnWndProc = DefWindowProcA;
	window_class.hInstance = instance;
	window_class.lpszClassName = "win98_probe";
	RegisterClassA(&window_class);
	window = CreateWindowExA(0, "win98_probe", "probe", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, instance, NULL);
	dc = window ? GetDC(window) : NULL;
	if (!dc)
	{
		out("  cannot create a window\r\n");
		sum("OpenGL: no window\r\n");
		return;
	}
	memset(&format, 0, sizeof(format));
	format.nSize = sizeof(format);
	format.nVersion = 1;
	format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	format.iPixelType = PFD_TYPE_RGBA;
	format.cColorBits = 32;
	format.cDepthBits = 24;
	format.cStencilBits = 8;
	format_index = ChoosePixelFormat(dc, &format);
	DescribePixelFormat(dc, format_index, sizeof(format), &format);
	REPORT("  pixel format %d: colour %d, depth %d, stencil %d, %s\r\n", format_index, format.cColorBits,
		format.cDepthBits, format.cStencilBits,
		(format.dwFlags & PFD_GENERIC_FORMAT) ? "SOFTWARE (Microsoft GDI generic)" : "hardware");
	if (!format_index || !SetPixelFormat(dc, format_index, &format))
	{
		REPORT("  SetPixelFormat failed (error %lu)\r\n", GetLastError());
		sum("OpenGL: no pixel format\r\n");
		return;
	}
	context = create_context(dc);
	if (!context || !make_current(dc, context))
	{
		REPORT("  no context (error %lu)\r\n", GetLastError());
		sum("OpenGL: no context\r\n");
		return;
	}

	version = (const char *)get_string(0x1F02);
	glsl = (const char *)get_string(0x8B8C);
	extensions = (const char *)get_string(0x1F03);
	REPORT("  vendor:   %s\r\n", (const char *)get_string(0x1F00));
	REPORT("  renderer: %s\r\n", (const char *)get_string(0x1F01));
	REPORT("  version:  %s\r\n", version ? version : "?");
	REPORT("  GLSL:     %s\r\n", glsl ? glsl : "none");
	wsprintfA(line_buffer, "OpenGL: %s, GLSL %s\r\n", version ? version : "?", glsl ? glsl : "none");
	sum(line_buffer);

	value = 0; get_integer(0x0D33, &value); REPORT("  max texture size: %d\r\n", value);
	value = 0; get_integer(0x84E2, &value); REPORT("  texture units (fixed function): %d\r\n", value);
	value = 0; get_integer(0x8872, &value); REPORT("  texture image units (shaders): %d\r\n", value);
	value = 0; get_integer(0x8869, &value); REPORT("  vertex attributes: %d\r\n", value);
	value = 0; get_integer(0x8B4A, &value); REPORT("  vertex uniform components: %d\r\n", value);
	value = 0; get_integer(0x8B49, &value); REPORT("  fragment uniform components: %d\r\n", value);
	value = 0; get_integer(0x84FF, &value); REPORT("  max anisotropy: %d\r\n", value);
	value = 0; get_integer(0x854D, &value); REPORT("  register combiners (NV): %d\r\n", value);

	out("  extensions of interest:\r\n");
	for (index = 0; wanted[index]; index++)
		REPORT("    %-36s %s\r\n", wanted[index], (extensions && contains_word(extensions, wanted[index])) ? "yes" : "-");
	out("  all extensions:\r\n    ");
	out(extensions ? extensions : "(none)");
	out("\r\n");

	make_current(NULL, NULL);
	delete_context(context);
	ReleaseDC(window, dc);
	DestroyWindow(window);
}

/* ---------- 5: libraries and functions */

static void function_probe(const char *library_name, const char *const *names)
{
	HMODULE library = LoadLibraryA(library_name);
	int index;

	if (!library)
	{
		REPORT("  %-14s MISSING\r\n", library_name);
		return;
	}
	REPORT("  %-14s present\r\n", library_name);
	for (index = 0; names[index]; index++)
		REPORT("    %-40s %s\r\n", names[index], GetProcAddress(library, names[index]) ? "yes" : "-");
}

static void library_probe(void)
{
	static const char *const kernel32[] = {
		"InitializeCriticalSectionAndSpinCount", "TryEnterCriticalSection", "SwitchToThread",
		"SignalObjectAndWait", "AddVectoredExceptionHandler", "GetTickCount64", "CreateWaitableTimerA",
		"IsProcessorFeaturePresent", "GetFileAttributesExA", "InterlockedCompareExchange",
		"CreateFileW", "MultiByteToWideChar", "GetVersionExA", "QueueUserAPC", "CreateToolhelp32Snapshot",
		"InitializeConditionVariable", "FlsAlloc", "GetSystemTimeAsFileTime", 0 };
	static const char *const ws2_32[] = { "WSAStartup", "getaddrinfo", "freeaddrinfo", "WSAPoll",
		"inet_pton", "select", "WSAEventSelect", 0 };
	static const char *const advapi32[] = { "CryptAcquireContextA", "CryptGenRandom", "RegOpenKeyExA", 0 };
	static const char *const iphlpapi[] = { "GetAdaptersInfo", "GetAdaptersAddresses", "GetBestInterface", 0 };
	static const char *const dsound[] = { "DirectSoundCreate", "DirectSoundCreate8", 0 };
	static const char *const dinput[] = { "DirectInputCreateA", 0 };
	static const char *const dinput8[] = { "DirectInput8Create", 0 };
	static const char *const winmm[] = { "joyGetPosEx", "timeBeginPeriod", "waveOutOpen", 0 };
	static const char *const d3d8[] = { "Direct3DCreate8", 0 };
	static const char *const d3d9[] = { "Direct3DCreate9", 0 };
	static const char *const msvcrt[] = { "_beginthreadex", "_ftol", "_vsnprintf", "_aligned_malloc",
		"_snwprintf", "strtok_s", 0 };
	static const char *const none[] = { 0 };

	out("\r\n== 5. Libraries and functions ==\r\n");
	function_probe("kernel32.dll", kernel32);
	function_probe("ws2_32.dll", ws2_32);
	function_probe("advapi32.dll", advapi32);
	function_probe("iphlpapi.dll", iphlpapi);
	function_probe("dsound.dll", dsound);
	function_probe("dinput.dll", dinput);
	function_probe("dinput8.dll", dinput8);
	function_probe("winmm.dll", winmm);
	function_probe("d3d8.dll", d3d8);
	function_probe("d3d9.dll", d3d9);
	function_probe("msvcrt.dll", msvcrt);
	function_probe("unicows.dll", none);
	function_probe("winhttp.dll", none);
	function_probe("wininet.dll", none);
	function_probe("dbghelp.dll", none);
	function_probe("bcrypt.dll", none);
	function_probe("xinput1_3.dll", none);
}

/* ---------- system */

static void system_probe(void)
{
	OSVERSIONINFOA version;
	MEMORYSTATUS memory;
	LARGE_INTEGER frequency;

	out("== 0. System ==\r\n");
	memset(&version, 0, sizeof(version));
	version.dwOSVersionInfoSize = sizeof(version);
	GetVersionExA(&version);
	REPORT("  Windows %lu.%lu build %lu, platform %lu (%s), \"%s\"\r\n", version.dwMajorVersion,
		version.dwMinorVersion, version.dwBuildNumber & 0xffff, version.dwPlatformId,
		version.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS ? "9x" : "NT", version.szCSDVersion);
	wsprintfA(line_buffer, "Windows %lu.%lu.%lu %s\r\n", version.dwMajorVersion, version.dwMinorVersion,
		version.dwBuildNumber & 0xffff, version.szCSDVersion);
	sum(line_buffer);
	memory.dwLength = sizeof(memory);
	GlobalMemoryStatus(&memory);
	REPORT("  RAM %lu MB (%lu MB free), page file %lu MB, address space %lu MB free\r\n",
		(DWORD)memory.dwTotalPhys >> 20, (DWORD)memory.dwAvailPhys >> 20, (DWORD)memory.dwTotalPageFile >> 20,
		(DWORD)memory.dwAvailVirtual >> 20);
	wsprintfA(line_buffer, "RAM: %lu MB\r\n", (DWORD)memory.dwTotalPhys >> 20);
	sum(line_buffer);
	if (QueryPerformanceFrequency(&frequency))
		REPORT("  performance counter: %lu Hz\r\n", frequency.LowPart);
	else
		out("  performance counter: none\r\n");
}

/* ---------- entry */

void WINAPI probe_entry(void)
{
	HINSTANCE instance = GetModuleHandleA(NULL);
	char path[MAX_PATH];
	int length;

	length = (int)GetModuleFileNameA(NULL, path, MAX_PATH);
	while (length > 0 && path[length - 1] != '\\')
		length--;
	path[length] = 0;
	lstrcatA(path, "win98_probe.txt");
	report_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	SetUnhandledExceptionFilter(fault_filter);
	out("OpenCE Windows 98 probe\r\n\r\n");
	system_probe();
	memory_probe();
	processor_probe();
	opengl_probe(instance);
	library_probe();
	out("\r\n== Summary ==\r\n");
	out(summary);
	if (report_file != INVALID_HANDLE_VALUE)
		CloseHandle(report_file);

	lstrcatA(summary, "\r\nThe full report is in win98_probe.txt beside this program.");
	MessageBoxA(NULL, summary, "OpenCE Windows 98 probe", MB_OK | MB_ICONINFORMATION);
	ExitProcess(0);
}
