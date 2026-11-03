/*
WIN32.C

The Win32 backend of the SDL-compatible layer (port/retro_sdl/README.md):
the SDL 3 functions OpenCE's platform layer calls that depend on the system,
on what Windows 95 and 98 have. ANSI functions only (the wide ones do
nothing on Windows 9x), OpenGL through WGL, sound through waveOut, the
mouse through window messages (no raw input before Windows XP).

What Windows 9x has nothing for fails as SDL fails when a driver is missing:
gamepads (none are listed), recording, the 2D renderer (the updater's
window), PNG pictures (common.c), custom pointers.
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <commdlg.h>
#include <process.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "retro_sdl_backend.h"

/* ---------- start-up and time */

static LARGE_INTEGER counter_frequency;
static LARGE_INTEGER counter_start;
static SDL_InitFlags initialized;

static void time_initialize(void)
{
	if (counter_frequency.QuadPart)
		return;
	QueryPerformanceFrequency(&counter_frequency);
	QueryPerformanceCounter(&counter_start);
}

bool SDL_Init(SDL_InitFlags flags)
{
	time_initialize();
	if (!initialized)
		timeBeginPeriod(1);
	initialized |= flags;
	return true;
}

Uint64 SDL_GetTicksNS(void)
{
	LARGE_INTEGER now;
	Uint64 elapsed;

	time_initialize();
	if (!counter_frequency.QuadPart)
		return (Uint64)GetTickCount() * 1000000;
	QueryPerformanceCounter(&now);
	elapsed = (Uint64)(now.QuadPart - counter_start.QuadPart);
	return elapsed / (Uint64)counter_frequency.QuadPart * 1000000000 +
		elapsed % (Uint64)counter_frequency.QuadPart * 1000000000 / (Uint64)counter_frequency.QuadPart;
}

Uint64 SDL_GetTicks(void)
{
	return SDL_GetTicksNS() / 1000000;
}

void SDL_Delay(Uint32 milliseconds)
{
	Sleep(milliseconds);
}

/* (sleep to within a few milliseconds of it, then wait it out: Windows 9x
sleeps to the millisecond at best, with timeBeginPeriod) */
void SDL_DelayPrecise(Uint64 nanoseconds)
{
	Uint64 end = SDL_GetTicksNS() + nanoseconds;
	Uint64 now;

	while ((now = SDL_GetTicksNS()) < end)
	{
		Uint64 left = end - now;

		if (left > 2000000)
			Sleep((DWORD)(left / 1000000 - 1));
		else
			Sleep(0);
	}
}

bool SDL_GetCurrentTime(SDL_Time *ticks)
{
	FILETIME now;
	ULARGE_INTEGER value;

	GetSystemTimeAsFileTime(&now);
	value.LowPart = now.dwLowDateTime;
	value.HighPart = now.dwHighDateTime;
	/* (100 ns since 1601 to ns since 1970) */
	*ticks = (SDL_Time)((value.QuadPart - 116444736000000000ULL) * 100);
	return true;
}

/* ---------- threads */

struct SDL_Thread
{
	HANDLE handle;
	SDL_ThreadFunction function;
	void *data;
	int status;
};

static unsigned __stdcall thread_start(void *argument)
{
	SDL_Thread *thread = (SDL_Thread *)argument;

	if (retro_sdl_thread_started)
		retro_sdl_thread_started();
	thread->status = thread->function(thread->data);
	return (unsigned)thread->status;
}

SDL_Thread *SDL_CreateThreadRuntime(SDL_ThreadFunction function, const char *name, void *data,
	SDL_FunctionPointer begin, SDL_FunctionPointer end)
{
	SDL_Thread *thread = (SDL_Thread *)calloc(1, sizeof(*thread));
	unsigned identifier;

	(void)name;
	(void)begin;
	(void)end;
	if (!thread)
	{
		SDL_SetError("out of memory");
		return NULL;
	}
	thread->function = function;
	thread->data = data;
	thread->handle = (HANDLE)_beginthreadex(NULL, 0, thread_start, thread, 0, &identifier);
	if (!thread->handle)
	{
		free(thread);
		SDL_SetError("cannot start a thread");
		return NULL;
	}
	return thread;
}

void SDL_WaitThread(SDL_Thread *thread, int *status)
{
	if (!thread)
		return;
	WaitForSingleObject(thread->handle, INFINITE);
	CloseHandle(thread->handle);
	if (status)
		*status = thread->status;
	free(thread);
}

/* (the thread frees nothing of its own when it ends: what it leaves is the
size of this) */
void SDL_DetachThread(SDL_Thread *thread)
{
	if (thread)
		CloseHandle(thread->handle);
}

SDL_ThreadID SDL_GetCurrentThreadID(void)
{
	return (SDL_ThreadID)GetCurrentThreadId();
}

/* ---------- mutexes and conditions (recursive, as SDL's) */

struct SDL_Mutex
{
	CRITICAL_SECTION section;
};

struct SDL_Condition
{
	CONDITION_VARIABLE variable;
};

SDL_Mutex *SDL_CreateMutex(void)
{
	SDL_Mutex *mutex = (SDL_Mutex *)calloc(1, sizeof(*mutex));

	if (!mutex)
	{
		SDL_SetError("out of memory");
		return NULL;
	}
	InitializeCriticalSection(&mutex->section);
	return mutex;
}

void SDL_DestroyMutex(SDL_Mutex *mutex)
{
	if (!mutex)
		return;
	DeleteCriticalSection(&mutex->section);
	free(mutex);
}

void SDL_LockMutex(SDL_Mutex *mutex)
{
	if (mutex)
		EnterCriticalSection(&mutex->section);
}

void SDL_UnlockMutex(SDL_Mutex *mutex)
{
	if (mutex)
		LeaveCriticalSection(&mutex->section);
}

SDL_Condition *SDL_CreateCondition(void)
{
	SDL_Condition *condition = (SDL_Condition *)calloc(1, sizeof(*condition));

	if (!condition)
	{
		SDL_SetError("out of memory");
		return NULL;
	}
	InitializeConditionVariable(&condition->variable);
	return condition;
}

void SDL_SignalCondition(SDL_Condition *condition)
{
	if (condition)
		WakeConditionVariable(&condition->variable);
}

void SDL_BroadcastCondition(SDL_Condition *condition)
{
	if (condition)
		WakeAllConditionVariable(&condition->variable);
}

bool SDL_WaitConditionTimeout(SDL_Condition *condition, SDL_Mutex *mutex, Sint32 milliseconds)
{
	if (!condition || !mutex)
		return false;
	return SleepConditionVariableCS(&condition->variable, &mutex->section,
		milliseconds < 0 ? INFINITE : (DWORD)milliseconds) != 0;
}

/* ---------- files */

const char *SDL_GetBasePath(void)
{
	static char path[MAX_PATH + 1];

	if (!path[0])
	{
		DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);

		while (length > 0 && path[length - 1] != '\\' && path[length - 1] != '/')
			length--;
		path[length] = '\0';
	}
	return path;
}

static SDL_Time file_time(FILETIME time)
{
	ULARGE_INTEGER value;

	value.LowPart = time.dwLowDateTime;
	value.HighPart = time.dwHighDateTime;
	return value.QuadPart ? (SDL_Time)((value.QuadPart - 116444736000000000ULL) * 100) : 0;
}

bool SDL_GetPathInfo(const char *path, SDL_PathInfo *information)
{
	WIN32_FILE_ATTRIBUTE_DATA data;

	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data))
		return SDL_SetError("no such path: %s", path);
	if (information)
	{
		information->type = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? SDL_PATHTYPE_DIRECTORY :
			SDL_PATHTYPE_FILE;
		information->size = ((Uint64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
		information->create_time = file_time(data.ftCreationTime);
		information->modify_time = file_time(data.ftLastWriteTime);
		information->access_time = file_time(data.ftLastAccessTime);
	}
	return true;
}

/* (and the folders it is in, as SDL's) */
bool SDL_CreateDirectory(const char *path)
{
	char partial[MAX_PATH];
	size_t index;
	DWORD attributes;

	if (strlen(path) >= sizeof(partial))
		return SDL_SetError("path too long");
	strcpy(partial, path);
	for (index = 1; partial[index]; index++)
	{
		if ((partial[index] == '\\' || partial[index] == '/') && partial[index - 1] != ':')
		{
			char separator = partial[index];

			partial[index] = '\0';
			CreateDirectoryA(partial, NULL);
			partial[index] = separator;
		}
	}
	if (CreateDirectoryA(partial, NULL))
		return true;
	attributes = GetFileAttributesA(partial);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) ? true :
		SDL_SetError("cannot create %s", path);
}

bool SDL_RemovePath(const char *path)
{
	DWORD attributes = GetFileAttributesA(path);

	if (attributes == INVALID_FILE_ATTRIBUTES)
		return true;
	if (attributes & FILE_ATTRIBUTE_DIRECTORY)
		return RemoveDirectoryA(path) ? true : SDL_SetError("cannot remove %s", path);
	return DeleteFileA(path) ? true : SDL_SetError("cannot remove %s", path);
}

/* (over what is there, as SDL's: Windows 9x's MoveFileA does not replace) */
bool SDL_RenamePath(const char *old_path, const char *new_path)
{
	if (MoveFileA(old_path, new_path))
		return true;
	if (GetFileAttributesA(new_path) != INVALID_FILE_ATTRIBUTES && DeleteFileA(new_path) &&
		MoveFileA(old_path, new_path))
	{
		return true;
	}
	return SDL_SetError("cannot rename %s", old_path);
}

bool retro_sdl_list_directory(const char *path, void (*add)(void *context, const char *name), void *context)
{
	char pattern[MAX_PATH + 4];
	WIN32_FIND_DATAA found;
	HANDLE search;
	size_t length = strlen(path);

	if (length + 3 > MAX_PATH)
		return SDL_SetError("path too long");
	strcpy(pattern, path);
	if (length && pattern[length - 1] != '\\' && pattern[length - 1] != '/')
		strcat(pattern, "\\");
	strcat(pattern, "*");
	search = FindFirstFileA(pattern, &found);
	if (search == INVALID_HANDLE_VALUE)
		return GetLastError() == ERROR_FILE_NOT_FOUND ? true : SDL_SetError("cannot read %s", path);
	do
	{
		if (strcmp(found.cFileName, ".") && strcmp(found.cFileName, ".."))
			add(context, found.cFileName);
	} while (FindNextFileA(search, &found));
	FindClose(search);
	return true;
}

/* ---------- displays and their modes */

static SDL_DisplayMode desktop_mode;
static SDL_DisplayMode current_mode;

static void display_mode_from(const DEVMODEA *device, SDL_DisplayMode *mode)
{
	memset(mode, 0, sizeof(*mode));
	mode->displayID = 1;
	mode->format = device->dmBitsPerPel == 16 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_XRGB8888;
	mode->w = (int)device->dmPelsWidth;
	mode->h = (int)device->dmPelsHeight;
	mode->pixel_density = 1.0f;
	mode->refresh_rate = device->dmDisplayFrequency > 1 ? (float)device->dmDisplayFrequency : 0.0f;
	mode->refresh_rate_numerator = device->dmDisplayFrequency > 1 ? (int)device->dmDisplayFrequency : 0;
	mode->refresh_rate_denominator = 1;
}

static void desktop_mode_read(void)
{
	DEVMODEA device;

	memset(&device, 0, sizeof(device));
	device.dmSize = sizeof(device);
	if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &device))
	{
		display_mode_from(&device, &desktop_mode);
	}
	else
	{
		/* (Windows 95 has no ENUM_CURRENT_SETTINGS: the screen's size) */
		memset(&desktop_mode, 0, sizeof(desktop_mode));
		desktop_mode.displayID = 1;
		desktop_mode.format = SDL_PIXELFORMAT_XRGB8888;
		desktop_mode.w = GetSystemMetrics(SM_CXSCREEN);
		desktop_mode.h = GetSystemMetrics(SM_CYSCREEN);
		desktop_mode.pixel_density = 1.0f;
	}
	if (!current_mode.w)
		current_mode = desktop_mode;
}

SDL_DisplayID SDL_GetPrimaryDisplay(void)
{
	return 1;
}

const SDL_DisplayMode *SDL_GetDesktopDisplayMode(SDL_DisplayID display)
{
	(void)display;
	desktop_mode_read();
	return &desktop_mode;
}

const SDL_DisplayMode *SDL_GetCurrentDisplayMode(SDL_DisplayID display)
{
	(void)display;
	desktop_mode_read();
	return &current_mode;
}

bool SDL_GetDisplayUsableBounds(SDL_DisplayID display, SDL_Rect *bounds)
{
	RECT area;

	(void)display;
	if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0))
	{
		area.left = 0;
		area.top = 0;
		area.right = GetSystemMetrics(SM_CXSCREEN);
		area.bottom = GetSystemMetrics(SM_CYSCREEN);
	}
	bounds->x = area.left;
	bounds->y = area.top;
	bounds->w = area.right - area.left;
	bounds->h = area.bottom - area.top;
	return true;
}

/* (the modes of 16 bits a pixel and more, largest first, one allocation
freed with SDL_free, as SDL's) */
SDL_DisplayMode **SDL_GetFullscreenDisplayModes(SDL_DisplayID display, int *count)
{
	DEVMODEA device;
	SDL_DisplayMode found[256];
	SDL_DisplayMode **result;
	SDL_DisplayMode *modes;
	int total = 0;
	DWORD index;
	int i, j;

	(void)display;
	memset(&device, 0, sizeof(device));
	device.dmSize = sizeof(device);
	for (index = 0; total < 256 && EnumDisplaySettingsA(NULL, index, &device); index++)
	{
		SDL_DisplayMode mode;
		bool known = false;

		if (device.dmBitsPerPel < 16)
			continue;
		display_mode_from(&device, &mode);
		for (j = 0; j < total; j++)
		{
			if (found[j].w == mode.w && found[j].h == mode.h && found[j].refresh_rate == mode.refresh_rate)
			{
				/* (the deeper colour of two) */
				if (device.dmBitsPerPel > 16)
					found[j].format = mode.format;
				known = true;
			}
		}
		if (!known)
			found[total++] = mode;
	}
	for (i = 1; i < total; i++)
	{
		SDL_DisplayMode mode = found[i];

		for (j = i; j > 0 && (found[j - 1].w * found[j - 1].h < mode.w * mode.h ||
			(found[j - 1].w * found[j - 1].h == mode.w * mode.h && found[j - 1].refresh_rate < mode.refresh_rate)); j--)
		{
			found[j] = found[j - 1];
		}
		found[j] = mode;
	}
	result = (SDL_DisplayMode **)malloc((size_t)(total + 1) * sizeof(*result) + (size_t)total * sizeof(*modes));
	if (!result)
	{
		SDL_SetError("out of memory");
		return NULL;
	}
	modes = (SDL_DisplayMode *)(result + total + 1);
	for (i = 0; i < total; i++)
	{
		modes[i] = found[i];
		result[i] = &modes[i];
	}
	result[total] = NULL;
	if (count)
		*count = total;
	return result;
}

bool SDL_GetClosestFullscreenDisplayMode(SDL_DisplayID display, int width, int height, float refresh_rate,
	bool high_density, SDL_DisplayMode *closest)
{
	int count = 0;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &count);
	const SDL_DisplayMode *best = NULL;
	int index;

	(void)high_density;
	for (index = 0; modes && index < count; index++)
	{
		const SDL_DisplayMode *mode = modes[index];

		if (mode->w < width || mode->h < height)
			continue;
		if (!best || mode->w * mode->h < best->w * best->h ||
			(mode->w * mode->h == best->w * best->h && refresh_rate > 0.0f &&
				fabsf(mode->refresh_rate - refresh_rate) < fabsf(best->refresh_rate - refresh_rate)))
		{
			best = mode;
		}
	}
	if (best)
		*closest = *best;
	SDL_free(modes);
	return best ? true : SDL_SetError("no display mode of %dx%d", width, height);
}

/* ---------- the window */

struct SDL_Window
{
	HWND handle;
	HDC dc;
	SDL_WindowFlags flags;
	SDL_PropertiesID properties;
	SDL_DisplayMode fullscreen_mode;
	bool fullscreen_mode_set;
	/* the windowed placement, while fullscreen */
	RECT windowed;
	/* relative mouse mode: where the pointer is kept */
	bool relative;
	bool grabbed;
	int last_x, last_y;
};

static SDL_Window *the_window;

/* ---------- events: a queue */

enum
{
	EVENT_QUEUE_SIZE = 256
};

static SDL_Event event_queue[EVENT_QUEUE_SIZE];
static int event_head;
static int event_count;
static CRITICAL_SECTION event_lock;
static LONG event_lock_ready;

static void event_lock_enter(void)
{
	if (!InterlockedExchange(&event_lock_ready, 1))
		InitializeCriticalSection(&event_lock);
	EnterCriticalSection(&event_lock);
}

bool SDL_PushEvent(SDL_Event *event)
{
	bool pushed = false;

	event_lock_enter();
	if (event_count < EVENT_QUEUE_SIZE)
	{
		event_queue[(event_head + event_count) % EVENT_QUEUE_SIZE] = *event;
		event_count++;
		pushed = true;
	}
	LeaveCriticalSection(&event_lock);
	return pushed;
}

static void event_push(SDL_EventType type, SDL_Event *event)
{
	event->type = type;
	event->common.timestamp = SDL_GetTicksNS();
	SDL_PushEvent(event);
}

/* the PC keyboard's scancodes (set 1, as WM_KEYDOWN gives them) to SDL's */
static SDL_Scancode scancode_from(LPARAM parameter)
{
	static const SDL_Scancode plain[0x60] =
	{
		SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
		SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0,
		SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_TAB,
		SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T, SDL_SCANCODE_Y,
		SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_LEFTBRACKET,
		SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_RETURN, SDL_SCANCODE_LCTRL, SDL_SCANCODE_A, SDL_SCANCODE_S,
		SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_J, SDL_SCANCODE_K,
		SDL_SCANCODE_L, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_GRAVE, SDL_SCANCODE_LSHIFT,
		SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_B,
		SDL_SCANCODE_N, SDL_SCANCODE_M, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH,
		SDL_SCANCODE_RSHIFT, SDL_SCANCODE_KP_MULTIPLY, SDL_SCANCODE_LALT, SDL_SCANCODE_SPACE, SDL_SCANCODE_CAPSLOCK,
		SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F5, SDL_SCANCODE_F6,
		SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_NUMLOCKCLEAR,
		SDL_SCANCODE_SCROLLLOCK, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_MINUS,
		SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_KP_1,
		SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_PERIOD, SDL_SCANCODE_UNKNOWN,
		SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_NONUSBACKSLASH, SDL_SCANCODE_F11, SDL_SCANCODE_F12,
	};
	unsigned code = (unsigned)(parameter >> 16) & 0xFF;
	bool extended = (parameter & (1 << 24)) != 0;

	if (extended)
	{
		switch (code)
		{
		case 0x1C: return SDL_SCANCODE_KP_ENTER;
		case 0x1D: return SDL_SCANCODE_RCTRL;
		case 0x35: return SDL_SCANCODE_KP_DIVIDE;
		case 0x37: return SDL_SCANCODE_PRINTSCREEN;
		case 0x38: return SDL_SCANCODE_RALT;
		case 0x47: return SDL_SCANCODE_HOME;
		case 0x48: return SDL_SCANCODE_UP;
		case 0x49: return SDL_SCANCODE_PAGEUP;
		case 0x4B: return SDL_SCANCODE_LEFT;
		case 0x4D: return SDL_SCANCODE_RIGHT;
		case 0x4F: return SDL_SCANCODE_END;
		case 0x50: return SDL_SCANCODE_DOWN;
		case 0x51: return SDL_SCANCODE_PAGEDOWN;
		case 0x52: return SDL_SCANCODE_INSERT;
		case 0x53: return SDL_SCANCODE_DELETE;
		case 0x5B: return SDL_SCANCODE_LGUI;
		case 0x5C: return SDL_SCANCODE_RGUI;
		case 0x5D: return SDL_SCANCODE_APPLICATION;
		default: return SDL_SCANCODE_UNKNOWN;
		}
	}
	if (code == 0x45)
		return SDL_SCANCODE_NUMLOCKCLEAR;
	return code < sizeof(plain) / sizeof(plain[0]) ? plain[code] : SDL_SCANCODE_UNKNOWN;
}

static void mouse_button(Uint8 button, bool down, LPARAM parameter)
{
	SDL_Event event;

	memset(&event, 0, sizeof(event));
	event.button.windowID = 1;
	event.button.button = button;
	event.button.down = down;
	event.button.clicks = 1;
	event.button.x = (float)(short)LOWORD(parameter);
	event.button.y = (float)(short)HIWORD(parameter);
	event_push(down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP, &event);
	if (the_window)
	{
		if (down)
			SetCapture(the_window->handle);
		else
			ReleaseCapture();
	}
}

static void window_center(SDL_Window *window, POINT *center)
{
	RECT client;

	GetClientRect(window->handle, &client);
	center->x = client.right / 2;
	center->y = client.bottom / 2;
}

static void mouse_clip(SDL_Window *window)
{
	RECT client;

	if (!window || !(window->relative || window->grabbed) || GetForegroundWindow() != window->handle)
	{
		ClipCursor(NULL);
		return;
	}
	GetClientRect(window->handle, &client);
	MapWindowPoints(window->handle, NULL, (POINT *)&client, 2);
	ClipCursor(&client);
}

static LRESULT CALLBACK window_procedure(HWND handle, UINT message, WPARAM wparameter, LPARAM lparameter)
{
	SDL_Window *window = the_window && the_window->handle == handle ? the_window : NULL;
	SDL_Event event;

	memset(&event, 0, sizeof(event));
	event.window.windowID = 1;
	switch (message)
	{
	case WM_CLOSE:
		event_push(SDL_EVENT_WINDOW_CLOSE_REQUESTED, &event);
		memset(&event, 0, sizeof(event));
		event_push(SDL_EVENT_QUIT, &event);
		return 0;

	case WM_ACTIVATE:
		if (window)
		{
			bool active = LOWORD(wparameter) != WA_INACTIVE;

			event_push(active ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST, &event);
			mouse_clip(window);
		}
		break;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYUP:
	{
		bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;

		event.key.windowID = 1;
		event.key.scancode = scancode_from(lparameter);
		event.key.key = SDL_GetKeyFromScancode(event.key.scancode, SDL_KMOD_NONE, true);
		event.key.raw = (Uint16)((lparameter >> 16) & 0xFF);
		event.key.down = down;
		event.key.repeat = down && (lparameter & (1 << 30)) != 0;
		if (event.key.scancode != SDL_SCANCODE_UNKNOWN)
			event_push(down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP, &event);
		/* (Alt and F10 would open the window's menu) */
		if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
		{
			if (wparameter == VK_F4)
				break;
			return 0;
		}
		return 0;
	}

	case WM_MOUSEMOVE:
	{
		int x = (short)LOWORD(lparameter);
		int y = (short)HIWORD(lparameter);

		event.motion.windowID = 1;
		event.motion.x = (float)x;
		event.motion.y = (float)y;
		if (window && window->relative)
		{
			POINT center;

			window_center(window, &center);
			if (x == center.x && y == center.y)
				return 0;
			event.motion.xrel = (float)(x - center.x);
			event.motion.yrel = (float)(y - center.y);
			ClientToScreen(handle, &center);
			SetCursorPos(center.x, center.y);
		}
		else if (window)
		{
			event.motion.xrel = (float)(x - window->last_x);
			event.motion.yrel = (float)(y - window->last_y);
			window->last_x = x;
			window->last_y = y;
		}
		event_push(SDL_EVENT_MOUSE_MOTION, &event);
		return 0;
	}

	case WM_LBUTTONDOWN: mouse_button(SDL_BUTTON_LEFT, true, lparameter); return 0;
	case WM_LBUTTONUP: mouse_button(SDL_BUTTON_LEFT, false, lparameter); return 0;
	case WM_RBUTTONDOWN: mouse_button(SDL_BUTTON_RIGHT, true, lparameter); return 0;
	case WM_RBUTTONUP: mouse_button(SDL_BUTTON_RIGHT, false, lparameter); return 0;
	case WM_MBUTTONDOWN: mouse_button(SDL_BUTTON_MIDDLE, true, lparameter); return 0;
	case WM_MBUTTONUP: mouse_button(SDL_BUTTON_MIDDLE, false, lparameter); return 0;

	case 0x020A: /* WM_MOUSEWHEEL (Windows 98) */
		event.wheel.windowID = 1;
		event.wheel.y = (float)(short)HIWORD(wparameter) / 120.0f;
		event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
		event_push(SDL_EVENT_MOUSE_WHEEL, &event);
		return 0;

	case WM_SETCURSOR:
		if (LOWORD(lparameter) == HTCLIENT && window && (window->relative))
		{
			SetCursor(NULL);
			return TRUE;
		}
		break;

	case WM_ERASEBKGND:
		return 1;
	}
	return DefWindowProcA(handle, message, wparameter, lparameter);
}

void SDL_PumpEvents(void)
{
	MSG message;

	while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&message);
		DispatchMessageA(&message);
	}
}

bool SDL_PollEvent(SDL_Event *event)
{
	bool got = false;

	SDL_PumpEvents();
	event_lock_enter();
	if (event_count)
	{
		if (event)
		{
			*event = event_queue[event_head];
			event_head = (event_head + 1) % EVENT_QUEUE_SIZE;
			event_count--;
		}
		got = true;
	}
	LeaveCriticalSection(&event_lock);
	return got;
}

/* ---------- the window */

static DWORD window_style(bool fullscreen, SDL_WindowFlags flags)
{
	if (fullscreen)
		return WS_POPUP;
	return WS_OVERLAPPEDWINDOW & ~((flags & SDL_WINDOW_RESIZABLE) ? 0 : (WS_THICKFRAME | WS_MAXIMIZEBOX));
}

/* (fullscreen is the screen's mode changed, as Windows 9x has it: the
window's mode, or the desktop's) */
static bool fullscreen_apply(SDL_Window *window, bool fullscreen)
{
	if (fullscreen)
	{
		const SDL_DisplayMode *mode = window->fullscreen_mode_set ? &window->fullscreen_mode : &desktop_mode;

		desktop_mode_read();
		if (!(window->flags & SDL_WINDOW_FULLSCREEN))
			GetWindowRect(window->handle, &window->windowed);
		if (mode->w != desktop_mode.w || mode->h != desktop_mode.h ||
			(mode->refresh_rate > 0.0f && mode->refresh_rate != desktop_mode.refresh_rate))
		{
			DEVMODEA device;

			memset(&device, 0, sizeof(device));
			device.dmSize = sizeof(device);
			device.dmPelsWidth = (DWORD)mode->w;
			device.dmPelsHeight = (DWORD)mode->h;
			device.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
			if (mode->refresh_rate > 0.0f)
			{
				device.dmDisplayFrequency = (DWORD)mode->refresh_rate;
				device.dmFields |= DM_DISPLAYFREQUENCY;
			}
			if (ChangeDisplaySettingsA(&device, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
				return SDL_SetError("cannot change the display to %dx%d", mode->w, mode->h);
		}
		current_mode = *mode;
		SetWindowLongA(window->handle, GWL_STYLE, (LONG)(window_style(true, window->flags) |
			(IsWindowVisible(window->handle) ? WS_VISIBLE : 0)));
		SetWindowPos(window->handle, HWND_TOP, 0, 0, mode->w, mode->h, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
		window->flags |= SDL_WINDOW_FULLSCREEN;
	}
	else if (window->flags & SDL_WINDOW_FULLSCREEN)
	{
		ChangeDisplaySettingsA(NULL, 0);
		current_mode = desktop_mode;
		SetWindowLongA(window->handle, GWL_STYLE, (LONG)(window_style(false, window->flags) |
			(IsWindowVisible(window->handle) ? WS_VISIBLE : 0)));
		SetWindowPos(window->handle, HWND_NOTOPMOST, window->windowed.left, window->windowed.top,
			window->windowed.right - window->windowed.left, window->windowed.bottom - window->windowed.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
		window->flags &= ~SDL_WINDOW_FULLSCREEN;
	}
	mouse_clip(window);
	return true;
}

SDL_Window *SDL_CreateWindow(const char *title, int width, int height, SDL_WindowFlags flags)
{
	static bool registered;
	SDL_Window *window;
	RECT frame;
	DWORD style;

	if (the_window)
	{
		SDL_SetError("one window only");
		return NULL;
	}
	if (!registered)
	{
		WNDCLASSA window_class;

		memset(&window_class, 0, sizeof(window_class));
		window_class.style = CS_OWNDC;
		window_class.lpfnWndProc = window_procedure;
		window_class.hInstance = GetModuleHandleA(NULL);
		window_class.hIcon = LoadIconA(window_class.hInstance, MAKEINTRESOURCEA(1));
		window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
		window_class.lpszClassName = "retro_sdl";
		if (!RegisterClassA(&window_class))
		{
			SDL_SetError("cannot register the window's class");
			return NULL;
		}
		registered = true;
	}
	desktop_mode_read();
	window = (SDL_Window *)calloc(1, sizeof(*window));
	if (!window)
	{
		SDL_SetError("out of memory");
		return NULL;
	}
	window->flags = flags & (SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
	style = window_style(false, flags);
	frame.left = 0;
	frame.top = 0;
	frame.right = width;
	frame.bottom = height;
	AdjustWindowRect(&frame, style, FALSE);
	window->handle = CreateWindowExA(0, "retro_sdl", title, style, CW_USEDEFAULT, CW_USEDEFAULT,
		frame.right - frame.left, frame.bottom - frame.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
	if (!window->handle)
	{
		free(window);
		SDL_SetError("cannot create the window");
		return NULL;
	}
	window->dc = GetDC(window->handle);
	window->properties = SDL_CreateProperties();
	retro_sdl_set_pointer_property(window->properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, window->handle);
	retro_sdl_set_pointer_property(window->properties, SDL_PROP_WINDOW_WIN32_HDC_POINTER, window->dc);
	the_window = window;
	if (flags & SDL_WINDOW_FULLSCREEN)
		fullscreen_apply(window, true);
	if (!(flags & SDL_WINDOW_HIDDEN))
	{
		ShowWindow(window->handle, SW_SHOW);
		SetForegroundWindow(window->handle);
		UpdateWindow(window->handle);
	}
	return window;
}

void SDL_DestroyWindow(SDL_Window *window)
{
	if (!window)
		return;
	if (window->flags & SDL_WINDOW_FULLSCREEN)
		ChangeDisplaySettingsA(NULL, 0);
	ClipCursor(NULL);
	ReleaseDC(window->handle, window->dc);
	DestroyWindow(window->handle);
	SDL_DestroyProperties(window->properties);
	if (the_window == window)
		the_window = NULL;
	free(window);
}

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *window)
{
	SDL_WindowFlags flags;

	if (!window)
		return 0;
	flags = window->flags;
	if (GetForegroundWindow() == window->handle)
		flags |= SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS;
	if (IsIconic(window->handle))
		flags |= SDL_WINDOW_MINIMIZED;
	if (!IsWindowVisible(window->handle))
		flags |= SDL_WINDOW_HIDDEN;
	if (window->relative)
		flags |= SDL_WINDOW_MOUSE_RELATIVE_MODE;
	if (window->grabbed)
		flags |= SDL_WINDOW_MOUSE_GRABBED;
	return flags;
}

SDL_PropertiesID SDL_GetWindowProperties(SDL_Window *window)
{
	return window ? window->properties : 0;
}

bool SDL_GetWindowSize(SDL_Window *window, int *width, int *height)
{
	RECT client;

	if (!window || !GetClientRect(window->handle, &client))
		return SDL_SetError("no window");
	if (width)
		*width = client.right;
	if (height)
		*height = client.bottom;
	return true;
}

bool SDL_GetWindowSizeInPixels(SDL_Window *window, int *width, int *height)
{
	return SDL_GetWindowSize(window, width, height);
}

float SDL_GetWindowPixelDensity(SDL_Window *window)
{
	(void)window;
	return 1.0f;
}

bool SDL_SetWindowSize(SDL_Window *window, int width, int height)
{
	RECT frame;

	if (!window)
		return SDL_SetError("no window");
	if (window->flags & SDL_WINDOW_FULLSCREEN)
		return true;
	frame.left = 0;
	frame.top = 0;
	frame.right = width;
	frame.bottom = height;
	AdjustWindowRect(&frame, (DWORD)GetWindowLongA(window->handle, GWL_STYLE), FALSE);
	SetWindowPos(window->handle, NULL, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER);
	return true;
}

SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *window)
{
	(void)window;
	return 1;
}

bool SDL_SetWindowFullscreenMode(SDL_Window *window, const SDL_DisplayMode *mode)
{
	if (!window)
		return SDL_SetError("no window");
	window->fullscreen_mode_set = mode != NULL;
	if (mode)
		window->fullscreen_mode = *mode;
	if (window->flags & SDL_WINDOW_FULLSCREEN)
		return fullscreen_apply(window, true);
	return true;
}

const SDL_DisplayMode *SDL_GetWindowFullscreenMode(SDL_Window *window)
{
	return window && window->fullscreen_mode_set ? &window->fullscreen_mode : NULL;
}

bool SDL_SetWindowFullscreen(SDL_Window *window, bool fullscreen)
{
	if (!window)
		return SDL_SetError("no window");
	return fullscreen_apply(window, fullscreen);
}

bool SDL_SetWindowIcon(SDL_Window *window, SDL_Surface *icon)
{
	(void)window;
	(void)icon;
	return SDL_SetError("the window's icon is its executable's");
}

SDL_Window *SDL_GetKeyboardFocus(void)
{
	return the_window && GetForegroundWindow() == the_window->handle ? the_window : NULL;
}

/* ---------- the mouse */

struct SDL_Cursor
{
	HCURSOR handle;
};

static int cursor_shown = 1;

bool SDL_SetWindowRelativeMouseMode(SDL_Window *window, bool enabled)
{
	if (!window)
		return SDL_SetError("no window");
	window->relative = enabled;
	if (enabled)
	{
		POINT center;

		window_center(window, &center);
		ClientToScreen(window->handle, &center);
		SetCursorPos(center.x, center.y);
	}
	mouse_clip(window);
	return true;
}

bool SDL_SetWindowMouseGrab(SDL_Window *window, bool grabbed)
{
	if (!window)
		return SDL_SetError("no window");
	window->grabbed = grabbed;
	mouse_clip(window);
	return true;
}

void SDL_WarpMouseInWindow(SDL_Window *window, float x, float y)
{
	POINT point;

	if (!window)
		window = the_window;
	if (!window)
		return;
	point.x = (LONG)x;
	point.y = (LONG)y;
	window->last_x = point.x;
	window->last_y = point.y;
	ClientToScreen(window->handle, &point);
	SetCursorPos(point.x, point.y);
}

SDL_Cursor *SDL_GetDefaultCursor(void)
{
	static SDL_Cursor arrow;

	if (!arrow.handle)
		arrow.handle = LoadCursorA(NULL, IDC_ARROW);
	return &arrow;
}

SDL_Cursor *SDL_CreateColorCursor(SDL_Surface *surface, int hot_x, int hot_y)
{
	(void)surface;
	(void)hot_x;
	(void)hot_y;
	SDL_SetError("colour pointers are not supported");
	return NULL;
}

void SDL_DestroyCursor(SDL_Cursor *cursor)
{
	(void)cursor;
}

bool SDL_SetCursor(SDL_Cursor *cursor)
{
	if (cursor && cursor->handle)
		SetCursor(cursor->handle);
	return true;
}

bool SDL_ShowCursor(void)
{
	if (cursor_shown <= 0)
	{
		while (ShowCursor(TRUE) < 0)
			;
		cursor_shown = 1;
	}
	return true;
}

bool SDL_HideCursor(void)
{
	if (cursor_shown > 0)
	{
		while (ShowCursor(FALSE) >= 0)
			;
		cursor_shown = 0;
	}
	return true;
}

bool SDL_CursorVisible(void)
{
	return cursor_shown > 0;
}

/* ---------- text input (the keys come as keys: no input method) */

static bool text_input_active;

bool SDL_StartTextInputWithProperties(SDL_Window *window, SDL_PropertiesID properties)
{
	(void)window;
	(void)properties;
	text_input_active = true;
	return true;
}

bool SDL_StopTextInput(SDL_Window *window)
{
	(void)window;
	text_input_active = false;
	return true;
}

bool SDL_TextInputActive(SDL_Window *window)
{
	(void)window;
	return text_input_active;
}

bool SDL_HasScreenKeyboardSupport(void)
{
	return false;
}

/* ---------- OpenGL through WGL */

static struct
{
	int red, green, blue, alpha, depth, stencil;
	bool single_buffer;
} gl_attributes = { 8, 8, 8, 0, 24, 8, false };

static HMODULE opengl_library;

bool SDL_GL_SetAttribute(SDL_GLAttr attribute, int value)
{
	switch (attribute)
	{
	case SDL_GL_RED_SIZE: gl_attributes.red = value; break;
	case SDL_GL_GREEN_SIZE: gl_attributes.green = value; break;
	case SDL_GL_BLUE_SIZE: gl_attributes.blue = value; break;
	case SDL_GL_ALPHA_SIZE: gl_attributes.alpha = value; break;
	case SDL_GL_DEPTH_SIZE: gl_attributes.depth = value; break;
	case SDL_GL_STENCIL_SIZE: gl_attributes.stencil = value; break;
	case SDL_GL_DOUBLEBUFFER: gl_attributes.single_buffer = value == 0; break;
	/* (the context is WGL's legacy one, whatever version or profile is
	asked for: the renderer finds what it has, and floating point
	buffers are not offered) */
	case SDL_GL_FLOATBUFFERS:
		if (value)
			return SDL_SetError("no floating point buffers");
		break;
	default:
		break;
	}
	return true;
}

SDL_GLContext SDL_GL_CreateContext(SDL_Window *window)
{
	PIXELFORMATDESCRIPTOR format;
	HGLRC context;
	int index;

	if (!window)
	{
		SDL_SetError("no window");
		return NULL;
	}
	if (!opengl_library)
		opengl_library = LoadLibraryA("opengl32.dll");
	memset(&format, 0, sizeof(format));
	format.nSize = sizeof(format);
	format.nVersion = 1;
	format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | (gl_attributes.single_buffer ? 0 : PFD_DOUBLEBUFFER);
	format.iPixelType = PFD_TYPE_RGBA;
	format.cColorBits = (BYTE)(gl_attributes.red + gl_attributes.green + gl_attributes.blue > 16 ? 32 : 16);
	format.cAlphaBits = (BYTE)gl_attributes.alpha;
	format.cDepthBits = (BYTE)gl_attributes.depth;
	format.cStencilBits = (BYTE)gl_attributes.stencil;
	format.iLayerType = PFD_MAIN_PLANE;
	index = ChoosePixelFormat(window->dc, &format);
	if (!index || !SetPixelFormat(window->dc, index, &format))
	{
		SDL_SetError("no OpenGL pixel format (error %lu)", GetLastError());
		return NULL;
	}
	context = wglCreateContext(window->dc);
	if (!context)
	{
		SDL_SetError("cannot create an OpenGL context (error %lu)", GetLastError());
		return NULL;
	}
	if (!wglMakeCurrent(window->dc, context))
	{
		wglDeleteContext(context);
		SDL_SetError("cannot make the OpenGL context current");
		return NULL;
	}
	return (SDL_GLContext)context;
}

bool SDL_GL_MakeCurrent(SDL_Window *window, SDL_GLContext context)
{
	return wglMakeCurrent(window ? window->dc : NULL, (HGLRC)context) ? true :
		SDL_SetError("cannot make the OpenGL context current");
}

SDL_FunctionPointer SDL_GL_GetProcAddress(const char *name)
{
	PROC function = wglGetProcAddress(name);

	/* (OpenGL 1.1's own are opengl32.dll's exports, not WGL's) */
	if (!function && opengl_library)
		function = GetProcAddress(opengl_library, name);
	return (SDL_FunctionPointer)function;
}

bool SDL_GL_SwapWindow(SDL_Window *window)
{
	return window && SwapBuffers(window->dc) ? true : SDL_SetError("cannot swap");
}

bool SDL_GL_SetSwapInterval(int interval)
{
	typedef BOOL (WINAPI *swap_interval_proc)(int);
	swap_interval_proc swap_interval = (swap_interval_proc)wglGetProcAddress("wglSwapIntervalEXT");

	if (!swap_interval)
		return SDL_SetError("no swap interval");
	return swap_interval(interval < 0 ? 1 : interval) ? true : SDL_SetError("cannot set the swap interval");
}

/* ---------- the clipboard, messages, links and the file dialog */

char *SDL_GetClipboardText(void)
{
	char *text = NULL;

	if (OpenClipboard(NULL))
	{
		HANDLE data = GetClipboardData(CF_TEXT);
		const char *locked = data ? (const char *)GlobalLock(data) : NULL;

		if (locked)
		{
			size_t length = strlen(locked) + 1;

			text = (char *)malloc(length);
			if (text)
				memcpy(text, locked, length);
			GlobalUnlock(data);
		}
		CloseClipboard();
	}
	if (!text)
	{
		text = (char *)malloc(1);
		if (text)
			text[0] = '\0';
	}
	return text;
}

bool SDL_SetClipboardText(const char *text)
{
	size_t length = strlen(text) + 1;
	HGLOBAL data;
	char *locked;

	if (!OpenClipboard(the_window ? the_window->handle : NULL))
		return SDL_SetError("cannot open the clipboard");
	EmptyClipboard();
	data = GlobalAlloc(GMEM_MOVEABLE, length);
	locked = data ? (char *)GlobalLock(data) : NULL;
	if (locked)
	{
		memcpy(locked, text, length);
		GlobalUnlock(data);
		SetClipboardData(CF_TEXT, data);
	}
	CloseClipboard();
	return locked != NULL;
}

static UINT message_box_icon(SDL_MessageBoxFlags flags)
{
	if (flags & SDL_MESSAGEBOX_ERROR)
		return MB_ICONERROR;
	if (flags & SDL_MESSAGEBOX_WARNING)
		return MB_ICONWARNING;
	return MB_ICONINFORMATION;
}

bool SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char *title, const char *message, SDL_Window *window)
{
	MessageBoxA(window ? window->handle : NULL, message, title, message_box_icon(flags) | MB_OK);
	return true;
}

/* (Windows' boxes have their own buttons: one, two or three buttons are
OK; Yes and No; Yes, No and Cancel, in the order given) */
bool SDL_ShowMessageBox(const SDL_MessageBoxData *data, int *button)
{
	UINT type = data->numbuttons >= 3 ? MB_YESNOCANCEL : data->numbuttons == 2 ? MB_YESNO : MB_OK;
	int result = MessageBoxA(data->window ? data->window->handle : NULL, data->message, data->title,
		message_box_icon(data->flags) | type);
	int index;

	switch (result)
	{
	case IDYES: case IDOK: index = 0; break;
	case IDNO: index = 1; break;
	case IDCANCEL: index = data->numbuttons >= 3 ? 2 : -1; break;
	default: index = -1; break;
	}
	if (button)
		*button = index >= 0 && index < data->numbuttons ? data->buttons[index].buttonID : -1;
	return true;
}

bool SDL_OpenURL(const char *url)
{
	return (INT_PTR)ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL) > 32 ? true :
		SDL_SetError("cannot open %s", url);
}

/* (answered at once: Windows' dialog is modal) */
void SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback, void *userdata, SDL_Window *window,
	const SDL_DialogFileFilter *filters, int count, const char *location, bool many)
{
	char filter_text[1024];
	char path[MAX_PATH];
	OPENFILENAMEA dialog;
	size_t used = 0;
	int index;

	(void)many;
	for (index = 0; index < count && used + 4 < sizeof(filter_text); index++)
	{
		const char *pattern = filters[index].pattern;
		int written = _snprintf(filter_text + used, sizeof(filter_text) - used - 2, "%s", filters[index].name);

		if (written < 0)
			break;
		used += (size_t)written + 1;
		/* ("iso;xiso" to "*.iso;*.xiso") */
		while (*pattern && used + 8 < sizeof(filter_text))
		{
			const char *end = strchr(pattern, ';');
			size_t length = end ? (size_t)(end - pattern) : strlen(pattern);

			if (used + length + 4 >= sizeof(filter_text))
				break;
			memcpy(filter_text + used, strcmp(pattern, "*") ? "*." : "*", strcmp(pattern, "*") ? 2 : 1);
			used += strcmp(pattern, "*") ? 2 : 1;
			if (strcmp(pattern, "*"))
			{
				memcpy(filter_text + used, pattern, length);
				used += length;
			}
			pattern += length;
			if (*pattern == ';')
			{
				filter_text[used++] = ';';
				pattern++;
			}
		}
		filter_text[used++] = '\0';
	}
	filter_text[used++] = '\0';
	filter_text[used] = '\0';

	path[0] = '\0';
	memset(&dialog, 0, sizeof(dialog));
	/* (Windows 95's size, which every version takes) */
	dialog.lStructSize = OPENFILENAME_SIZE_VERSION_400A;
	dialog.hwndOwner = window ? window->handle : NULL;
	dialog.lpstrFilter = count ? filter_text : NULL;
	dialog.lpstrFile = path;
	dialog.nMaxFile = sizeof(path);
	dialog.lpstrInitialDir = location;
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
	if (GetOpenFileNameA(&dialog))
	{
		const char *list[2];

		list[0] = path;
		list[1] = NULL;
		callback(userdata, list, (int)dialog.nFilterIndex - 1);
	}
	else
	{
		const char *list[1];

		list[0] = NULL;
		callback(userdata, CommDlgExtendedError() ? NULL : list, -1);
	}
}

/* ---------- sound: waveOut, 16-bit PCM, fed from a thread */

enum
{
	AUDIO_BUFFER_COUNT = 4,
	AUDIO_BUFFER_FRAMES = 1024,
	AUDIO_MAXIMUM_CHANNELS = 8
};

struct SDL_AudioStream
{
	SDL_AudioSpec spec;
	SDL_AudioStreamCallback callback;
	void *userdata;
	HWAVEOUT device;
	HANDLE event;
	HANDLE thread;
	volatile LONG stopping;
	bool started;
	/* the device's: 2 channels or 1, at this rate */
	int device_channels;
	int device_rate;
	WAVEHDR headers[AUDIO_BUFFER_COUNT];
	short *samples[AUDIO_BUFFER_COUNT];
	/* the float frames the callback put, and how many of them are read */
	float *pending;
	int pending_frames;
	int pending_capacity;
	/* (resampling: where in the pending frames the next output frame is) */
	double position;
};

static const SDL_AudioDeviceID audio_playback_device = 2;

SDL_AudioDeviceID *SDL_GetAudioPlaybackDevices(int *count)
{
	SDL_AudioDeviceID *devices = (SDL_AudioDeviceID *)calloc(2, sizeof(*devices));

	if (count)
		*count = devices && waveOutGetNumDevs() ? 1 : 0;
	if (devices && waveOutGetNumDevs())
		devices[0] = audio_playback_device;
	return devices;
}

SDL_AudioDeviceID *SDL_GetAudioRecordingDevices(int *count)
{
	if (count)
		*count = 0;
	return (SDL_AudioDeviceID *)calloc(1, sizeof(SDL_AudioDeviceID));
}

const char *SDL_GetAudioDeviceName(SDL_AudioDeviceID device)
{
	static WAVEOUTCAPSA capabilities;

	(void)device;
	if (waveOutGetDevCapsA(WAVE_MAPPER, &capabilities, sizeof(capabilities)) != MMSYSERR_NOERROR)
		return "waveOut";
	return capabilities.szPname;
}

bool SDL_GetAudioDeviceFormat(SDL_AudioDeviceID device, SDL_AudioSpec *spec, int *frames)
{
	(void)device;
	if (device == SDL_AUDIO_DEVICE_DEFAULT_RECORDING)
		return SDL_SetError("no recording");
	spec->format = SDL_AUDIO_S16;
	spec->channels = 2;
	spec->freq = 44100;
	if (frames)
		*frames = AUDIO_BUFFER_FRAMES;
	return true;
}

/* (the device's format: the stream's rate if it takes it, else 44100 or
22050 Hz; two channels, or one; the last refusal's reason kept) */
static MMRESULT audio_open_error;

static bool audio_device_open(SDL_AudioStream *stream)
{
	static const int rates[] = { 0, 44100, 22050, 11025 };
	int channels;
	size_t index;

	for (channels = 2; channels >= 1; channels--)
	{
		for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
		{
			WAVEFORMATEX format;
			int rate = rates[index] ? rates[index] : stream->spec.freq;

			memset(&format, 0, sizeof(format));
			format.wFormatTag = WAVE_FORMAT_PCM;
			format.nChannels = (WORD)channels;
			format.nSamplesPerSec = (DWORD)rate;
			format.wBitsPerSample = 16;
			format.nBlockAlign = (WORD)(channels * 2);
			format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
			audio_open_error = waveOutOpen(&stream->device, WAVE_MAPPER, &format, (DWORD_PTR)stream->event, 0,
				CALLBACK_EVENT);
			if (audio_open_error == MMSYSERR_NOERROR)
			{
				stream->device_channels = channels;
				stream->device_rate = rate;
				return true;
			}
		}
	}
	return false;
}

/* (frames the callback put, as the device's: resampled linearly, the
channels past the first two mixed into them) */
static void audio_fill(SDL_AudioStream *stream, short *output, int frames)
{
	double step = (double)stream->spec.freq / (double)stream->device_rate;
	int needed = (int)(stream->position + frames * step) + 2;
	int channels = stream->spec.channels;
	int frame;

	if (stream->pending_frames < needed && stream->callback)
	{
		int missing = needed - stream->pending_frames;
		int bytes = missing * channels * (int)sizeof(float);

		stream->callback(stream->userdata, stream, bytes, bytes);
	}
	for (frame = 0; frame < frames; frame++)
	{
		int index = (int)stream->position;
		float fraction = (float)(stream->position - index);
		float mixed[2] = { 0.0f, 0.0f };
		int channel;

		if (index + 1 < stream->pending_frames)
		{
			const float *a = stream->pending + index * channels;
			const float *b = a + channels;

			for (channel = 0; channel < channels; channel++)
			{
				float value = a[channel] + (b[channel] - a[channel]) * fraction;

				/* (front left and right as they are; centre, the
				low frequencies and the rest into both, quieter) */
				if (channel < 2)
					mixed[channel] += value;
				else
				{
					mixed[0] += value * 0.5f;
					mixed[1] += value * 0.5f;
				}
			}
			if (channels == 1)
				mixed[1] = mixed[0];
		}
		if (stream->device_channels == 1)
		{
			float mono = (mixed[0] + mixed[1]) * 0.5f;

			output[frame] = (short)(mono > 1.0f ? 32767 : mono < -1.0f ? -32768 : mono * 32767.0f);
		}
		else
		{
			for (channel = 0; channel < 2; channel++)
			{
				float value = mixed[channel];

				output[frame * 2 + channel] = (short)(value > 1.0f ? 32767 : value < -1.0f ? -32768 :
					value * 32767.0f);
			}
		}
		stream->position += step;
	}
	/* (the frames read, gone; the fraction kept) */
	{
		int consumed = (int)stream->position;

		if (consumed > stream->pending_frames)
			consumed = stream->pending_frames;
		memmove(stream->pending, stream->pending + consumed * channels,
			(size_t)(stream->pending_frames - consumed) * channels * sizeof(float));
		stream->pending_frames -= consumed;
		stream->position -= consumed;
	}
}

static unsigned __stdcall audio_thread(void *argument)
{
	SDL_AudioStream *stream = (SDL_AudioStream *)argument;
	int index;

	if (retro_sdl_thread_started)
		retro_sdl_thread_started();
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	while (!stream->stopping)
	{
		for (index = 0; index < AUDIO_BUFFER_COUNT; index++)
		{
			WAVEHDR *header = &stream->headers[index];

			if (header->dwFlags & WHDR_INQUEUE)
				continue;
			audio_fill(stream, stream->samples[index], AUDIO_BUFFER_FRAMES);
			waveOutWrite(stream->device, header, sizeof(*header));
		}
		WaitForSingleObject(stream->event, 50);
	}
	return 0;
}

SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID device, const SDL_AudioSpec *spec,
	SDL_AudioStreamCallback callback, void *userdata)
{
	SDL_AudioStream *stream;
	int index;

	if (device == SDL_AUDIO_DEVICE_DEFAULT_RECORDING || (device != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK &&
		device != audio_playback_device))
	{
		SDL_SetError("no recording");
		return NULL;
	}
	if (!spec || spec->format != SDL_AUDIO_F32 || spec->channels < 1 || spec->channels > AUDIO_MAXIMUM_CHANNELS)
	{
		SDL_SetError("unsupported audio format");
		return NULL;
	}
	stream = (SDL_AudioStream *)calloc(1, sizeof(*stream));
	if (!stream)
	{
		SDL_SetError("out of memory");
		return NULL;
	}
	stream->spec = *spec;
	stream->callback = callback;
	stream->userdata = userdata;
	stream->event = CreateEventA(NULL, FALSE, FALSE, NULL);
	if (!stream->event || !audio_device_open(stream))
	{
		if (stream->event)
			CloseHandle(stream->event);
		free(stream);
		/* (MMSYSERR_ALLOCATED: another program has it, as a sound card's
		driver of the time takes one program at a time) */
		if (audio_open_error == MMSYSERR_ALLOCATED)
			SDL_SetError("the sound device is in use by another program");
		else if (audio_open_error == MMSYSERR_NODRIVER || audio_open_error == MMSYSERR_BADDEVICEID)
			SDL_SetError("no sound device (waveOut error %u)", (unsigned)audio_open_error);
		else
			SDL_SetError("cannot open the sound device (waveOut error %u)", (unsigned)audio_open_error);
		return NULL;
	}
	for (index = 0; index < AUDIO_BUFFER_COUNT; index++)
	{
		stream->samples[index] = (short *)calloc(AUDIO_BUFFER_FRAMES * 2, sizeof(short));
		stream->headers[index].lpData = (LPSTR)stream->samples[index];
		stream->headers[index].dwBufferLength = (DWORD)(AUDIO_BUFFER_FRAMES * stream->device_channels * sizeof(short));
		waveOutPrepareHeader(stream->device, &stream->headers[index], sizeof(stream->headers[index]));
	}
	waveOutPause(stream->device);
	return stream;
}

bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream)
{
	unsigned identifier;

	if (!stream)
		return SDL_SetError("no stream");
	if (!stream->started)
	{
		stream->started = true;
		stream->thread = (HANDLE)_beginthreadex(NULL, 0, audio_thread, stream, 0, &identifier);
	}
	waveOutRestart(stream->device);
	return true;
}

bool SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *data, int length)
{
	int frames;

	if (!stream || length < 0)
		return SDL_SetError("no stream");
	frames = length / (int)(stream->spec.channels * sizeof(float));
	if (stream->pending_frames + frames > stream->pending_capacity)
	{
		int capacity = (stream->pending_frames + frames) * 2;
		float *pending = (float *)realloc(stream->pending, (size_t)capacity * stream->spec.channels * sizeof(float));

		if (!pending)
			return SDL_SetError("out of memory");
		stream->pending = pending;
		stream->pending_capacity = capacity;
	}
	memcpy(stream->pending + stream->pending_frames * stream->spec.channels, data,
		(size_t)frames * stream->spec.channels * sizeof(float));
	stream->pending_frames += frames;
	return true;
}

/* (recording's: there is none) */
int SDL_GetAudioStreamAvailable(SDL_AudioStream *stream)
{
	(void)stream;
	return 0;
}

int SDL_GetAudioStreamData(SDL_AudioStream *stream, void *buffer, int length)
{
	(void)stream;
	(void)buffer;
	(void)length;
	return -1;
}

void SDL_DestroyAudioStream(SDL_AudioStream *stream)
{
	int index;

	if (!stream)
		return;
	stream->stopping = 1;
	SetEvent(stream->event);
	if (stream->thread)
	{
		WaitForSingleObject(stream->thread, INFINITE);
		CloseHandle(stream->thread);
	}
	waveOutReset(stream->device);
	for (index = 0; index < AUDIO_BUFFER_COUNT; index++)
	{
		waveOutUnprepareHeader(stream->device, &stream->headers[index], sizeof(stream->headers[index]));
		free(stream->samples[index]);
	}
	waveOutClose(stream->device);
	CloseHandle(stream->event);
	free(stream->pending);
	free(stream);
}

/* ---------- gamepads and joysticks: none (yet) */

SDL_JoystickID *SDL_GetGamepads(int *count)
{
	if (count)
		*count = 0;
	return (SDL_JoystickID *)calloc(1, sizeof(SDL_JoystickID));
}

SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID identifier)
{
	(void)identifier;
	SDL_SetError("no gamepads");
	return NULL;
}

void SDL_CloseGamepad(SDL_Gamepad *gamepad)
{
	(void)gamepad;
}

SDL_Gamepad *SDL_GetGamepadFromID(SDL_JoystickID identifier)
{
	(void)identifier;
	return NULL;
}

SDL_JoystickID SDL_GetGamepadID(SDL_Gamepad *gamepad)
{
	(void)gamepad;
	return 0;
}

const char *SDL_GetGamepadName(SDL_Gamepad *gamepad)
{
	(void)gamepad;
	return NULL;
}

const char *SDL_GetGamepadPath(SDL_Gamepad *gamepad)
{
	(void)gamepad;
	return NULL;
}

Uint16 SDL_GetGamepadVendor(SDL_Gamepad *gamepad)
{
	(void)gamepad;
	return 0;
}

Uint16 SDL_GetGamepadProduct(SDL_Gamepad *gamepad)
{
	(void)gamepad;
	return 0;
}

SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *gamepad)
{
	(void)gamepad;
	return SDL_GAMEPAD_TYPE_UNKNOWN;
}

bool SDL_GetGamepadButton(SDL_Gamepad *gamepad, SDL_GamepadButton button)
{
	(void)gamepad;
	(void)button;
	return false;
}

Sint16 SDL_GetGamepadAxis(SDL_Gamepad *gamepad, SDL_GamepadAxis axis)
{
	(void)gamepad;
	(void)axis;
	return 0;
}

bool SDL_RumbleGamepad(SDL_Gamepad *gamepad, Uint16 low, Uint16 high, Uint32 duration)
{
	(void)gamepad;
	(void)low;
	(void)high;
	(void)duration;
	return SDL_SetError("no gamepads");
}

SDL_JoystickID SDL_AttachVirtualJoystick(const SDL_VirtualJoystickDesc *description)
{
	(void)description;
	SDL_SetError("no virtual joysticks");
	return 0;
}

SDL_Joystick *SDL_OpenJoystick(SDL_JoystickID identifier)
{
	(void)identifier;
	SDL_SetError("no joysticks");
	return NULL;
}

bool SDL_SetJoystickVirtualAxis(SDL_Joystick *joystick, int axis, Sint16 value)
{
	(void)joystick;
	(void)axis;
	(void)value;
	return SDL_SetError("no joysticks");
}

bool SDL_SetJoystickVirtualButton(SDL_Joystick *joystick, int button, bool down)
{
	(void)joystick;
	(void)button;
	(void)down;
	return SDL_SetError("no joysticks");
}

/* ---------- the 2D renderer: none (the updater's window goes without) */

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, const char *name)
{
	(void)window;
	(void)name;
	SDL_SetError("no 2D renderer");
	return NULL;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
	(void)renderer;
}

bool SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	(void)renderer;
	(void)r;
	(void)g;
	(void)b;
	(void)a;
	return SDL_SetError("no 2D renderer");
}

bool SDL_SetRenderScale(SDL_Renderer *renderer, float x, float y)
{
	(void)renderer;
	(void)x;
	(void)y;
	return SDL_SetError("no 2D renderer");
}

bool SDL_SetRenderVSync(SDL_Renderer *renderer, int vsync)
{
	(void)renderer;
	(void)vsync;
	return SDL_SetError("no 2D renderer");
}

bool SDL_RenderClear(SDL_Renderer *renderer)
{
	(void)renderer;
	return SDL_SetError("no 2D renderer");
}

bool SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_FRect *rectangle)
{
	(void)renderer;
	(void)rectangle;
	return SDL_SetError("no 2D renderer");
}

bool SDL_RenderDebugText(SDL_Renderer *renderer, float x, float y, const char *text)
{
	(void)renderer;
	(void)x;
	(void)y;
	(void)text;
	return SDL_SetError("no 2D renderer");
}

bool SDL_RenderPresent(SDL_Renderer *renderer)
{
	(void)renderer;
	return SDL_SetError("no 2D renderer");
}
