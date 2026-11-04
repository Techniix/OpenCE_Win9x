/*
WIN9X_LAUNCHER.C

The setup window of the Windows 9x build: sliders and toggles for what a slow
machine needs to choose, shown before the game opens. It is plain user32 (the
classes BUTTON, STATIC, COMBOBOX and SCROLLBAR, which every Windows 95 has: the
trackbar is comctl32's, which an early Windows 95 lacks), in ANSI.

It is shown:
- on the first run (launcher.configured is false);
- when Shift is held as the program starts, or with --setup (SETUP.BAT);
- when launcher.show_next_start or launcher.always is true;
- when the last run did not end (halo_running.flag, beside halo.exe, which
  a run makes and ends removes: a crash or a hang leaves it), with a word
  saying so, so that a setting that stops the game from starting can be
  taken back without a text editor.
HALO_NO_LAUNCHER, a dedicated server (--server) and a program started with
--no-setup skip it.

The settings it writes and reads are in win9x_launcher_logic.c.
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "win9x_launcher_logic.h"
#include "../../linux/src/port_config.h"

/* ---------- constants */

enum
{
	WINDOW_WIDTH = 480,
	LEFT = 14,
	LABEL_WIDTH = 150,
	CONTROL_LEFT = 170,
	SLIDER_WIDTH = 200,
	VALUE_LEFT = 376,
	ROW_HEIGHT = 28,

	ID_PRESET = 100,
	ID_DETECT,
	ID_MODE,
	ID_PLAY,
	ID_SAVE,
	ID_QUIT,
	ID_VSYNC,
	ID_SMOOTH,
	ID_PVS,
	ID_REVERB,
	ID_FAST_MIXING,
	ID_SOUND,
	ID_NEXT_START,

	SLIDER_SCALE = 0,
	SLIDER_DETAIL,
	SLIDER_EXTRAS,
	SLIDER_FPS,
	SLIDER_TEXTURES,
	SLIDER_KEEP_UP,
	SLIDER_BRIGHTNESS,
	SLIDER_COUNT
};

enum
{
	RESULT_PLAY = 1,
	RESULT_SAVE,
	RESULT_QUIT
};

struct slider
{
	HWND bar;
	HWND value;
	int minimum, maximum, step;
};

/* ---------- private data */

static const char window_class[] = "OpenCEWin9xSetup";
static struct slider sliders[SLIDER_COUNT];
static HWND window, preset_combo, mode_combo, hardware_text, notice_text;
static HWND toggles[ID_NEXT_START - ID_VSYNC + 1];
static HFONT font;
static int result;
/* the choices as the window left them (read before its controls went) */
static struct launcher_choices final_choices;
static int final_again;

/* ---------- private code: the settings */

/* a setting's value as text, of any type (config_string is for the strings
alone), in a buffer that stays until the call after next */
static const char *read_setting(const char *name)
{
	static char texts[4][400];
	static int next;
	char *text = texts[next++ & 3];

	if (!config_text(name, text, sizeof(texts[0])))
		text[0] = '\0';
	return text;
}

static int write_setting(const char *name, const char *value)
{
	return config_write(name, value);
}

static void flag_path(char *path, size_t size)
{
	config_folder(path, size);
	strncat(path, "halo_running.flag", size - strlen(path) - 1);
}

static void flag_remove(void)
{
	char path[MAX_PATH];

	flag_path(path, sizeof(path));
	DeleteFileA(path);
}

static void flag_make(void)
{
	char path[MAX_PATH];
	HANDLE file;

	flag_path(path, sizeof(path));
	file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file != INVALID_HANDLE_VALUE)
		CloseHandle(file);
}

/* ---------- private code: the machine */

/* the processor's speed in MHz: the time-stamp counter over about 60
milliseconds of the performance counter (0 if the processor has none, or the
counter does not run) */
static unsigned long measure_megahertz(void)
{
	LARGE_INTEGER frequency, start, now;
	unsigned long long first, last;
	unsigned long long elapsed;

	BOOL (WINAPI *present)(DWORD) = (BOOL (WINAPI *)(DWORD))(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"),
		"IsProcessorFeaturePresent");

	/* (this build runs on a Pentium MMX or later, which has the counter; the
	function, which Windows 95 may lack, only rules out one that does not) */
	if ((present && !present(8)) || !QueryPerformanceFrequency(&frequency) || !frequency.QuadPart)
		return 0;
	QueryPerformanceCounter(&start);
	__asm__ volatile("rdtsc" : "=A"(first));
	do
	{
		QueryPerformanceCounter(&now);
		elapsed = (unsigned long long)(now.QuadPart - start.QuadPart);
	}
	while (elapsed * 1000ULL < (unsigned long long)frequency.QuadPart * 60ULL);
	__asm__ volatile("rdtsc" : "=A"(last));
	return (unsigned long)(((last - first) * (unsigned long long)frequency.QuadPart) / elapsed / 1000000ULL);
}

static unsigned long megabytes(void)
{
	MEMORYSTATUS status;

	status.dwLength = sizeof(status);
	GlobalMemoryStatus(&status);
	return (unsigned long)(status.dwTotalPhys / (1024UL * 1024UL));
}

/* ---------- private code: the window */

static const char *slider_text(int which, int position, char *text)
{
	switch (which)
	{
	case SLIDER_SCALE:
		return launcher_scale_name(position);
	case SLIDER_DETAIL:
		sprintf(text, "%d%%", position * LAUNCHER_DETAIL_STEP);
		return text;
	case SLIDER_EXTRAS:
		return launcher_extras_name(position);
	case SLIDER_TEXTURES:
		return launcher_texture_name(position);
	case SLIDER_KEEP_UP:
		if (!position)
			return "Off";
		sprintf(text, "%d frames a second", launcher_keep_up_fps(position));
		return text;
	case SLIDER_BRIGHTNESS:
		sprintf(text, "%d%%", position * LAUNCHER_BRIGHTNESS_STEP);
		return text;
	default:
		return launcher_fps_name(position);
	}
}

static int slider_get(int which)
{
	SCROLLINFO info;

	info.cbSize = sizeof(info);
	info.fMask = SIF_POS;
	GetScrollInfo(sliders[which].bar, SB_CTL, &info);
	return info.nPos;
}

static void slider_set(int which, int position)
{
	struct slider *slider = &sliders[which];
	SCROLLINFO info;
	char text[16];

	if (position < slider->minimum)
		position = slider->minimum;
	if (position > slider->maximum)
		position = slider->maximum;
	info.cbSize = sizeof(info);
	info.fMask = SIF_POS;
	info.nPos = position;
	SetScrollInfo(slider->bar, SB_CTL, &info, TRUE);
	SetWindowTextA(slider->value, slider_text(which, position, text));
}

static void show_choices(const struct launcher_choices *choices)
{
	slider_set(SLIDER_SCALE, choices->scale_stop);
	slider_set(SLIDER_DETAIL, choices->detail_percent / LAUNCHER_DETAIL_STEP);
	slider_set(SLIDER_EXTRAS, choices->extras_stop);
	slider_set(SLIDER_FPS, choices->fps_stop);
	slider_set(SLIDER_TEXTURES, choices->texture_detail);
	slider_set(SLIDER_KEEP_UP, launcher_keep_up_stop(choices->keep_up));
	slider_set(SLIDER_BRIGHTNESS, choices->brightness / LAUNCHER_BRIGHTNESS_STEP);
	SendMessageA(mode_combo, CB_SETCURSEL, (WPARAM)choices->display_mode, 0);
	SendMessageA(toggles[ID_VSYNC - ID_VSYNC], BM_SETCHECK, choices->vsync, 0);
	SendMessageA(toggles[ID_SMOOTH - ID_VSYNC], BM_SETCHECK, choices->smooth_motion, 0);
	SendMessageA(toggles[ID_PVS - ID_VSYNC], BM_SETCHECK, choices->pvs_visibility, 0);
	SendMessageA(toggles[ID_REVERB - ID_VSYNC], BM_SETCHECK, choices->reverb, 0);
	SendMessageA(toggles[ID_FAST_MIXING - ID_VSYNC], BM_SETCHECK, choices->fast_mixing, 0);
	SendMessageA(toggles[ID_SOUND - ID_VSYNC], BM_SETCHECK, choices->sound, 0);
}

static void collect_choices(struct launcher_choices *choices)
{
	memset(choices, 0, sizeof(*choices));
	choices->display_mode = (int)SendMessageA(mode_combo, CB_GETCURSEL, 0, 0);
	choices->scale_stop = slider_get(SLIDER_SCALE);
	choices->detail_percent = slider_get(SLIDER_DETAIL) * LAUNCHER_DETAIL_STEP;
	choices->extras_stop = slider_get(SLIDER_EXTRAS);
	choices->fps_stop = slider_get(SLIDER_FPS);
	choices->texture_detail = slider_get(SLIDER_TEXTURES);
	choices->keep_up = launcher_keep_up_fps(slider_get(SLIDER_KEEP_UP));
	choices->brightness = slider_get(SLIDER_BRIGHTNESS) * LAUNCHER_BRIGHTNESS_STEP;
	choices->vsync = (int)SendMessageA(toggles[ID_VSYNC - ID_VSYNC], BM_GETCHECK, 0, 0) == BST_CHECKED;
	choices->smooth_motion = (int)SendMessageA(toggles[ID_SMOOTH - ID_VSYNC], BM_GETCHECK, 0, 0) == BST_CHECKED;
	choices->pvs_visibility = (int)SendMessageA(toggles[ID_PVS - ID_VSYNC], BM_GETCHECK, 0, 0) == BST_CHECKED;
	choices->reverb = (int)SendMessageA(toggles[ID_REVERB - ID_VSYNC], BM_GETCHECK, 0, 0) == BST_CHECKED;
	choices->fast_mixing = (int)SendMessageA(toggles[ID_FAST_MIXING - ID_VSYNC], BM_GETCHECK, 0, 0) == BST_CHECKED;
	choices->sound = (int)SendMessageA(toggles[ID_SOUND - ID_VSYNC], BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static HWND make(const char *class_name, const char *text, DWORD style, int x, int y, int width, int height,
	int id)
{
	HWND control = CreateWindowExA(0, class_name, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
		(HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);

	if (control && font)
		SendMessageA(control, WM_SETFONT, (WPARAM)font, 0);
	return control;
}

static void make_slider(int which, const char *label, int minimum, int maximum, int step, int y)
{
	struct slider *slider = &sliders[which];
	SCROLLINFO info;

	make("STATIC", label, SS_LEFT, LEFT, y + 3, LABEL_WIDTH, 18, 0);
	slider->bar = make("SCROLLBAR", "", SBS_HORZ | WS_TABSTOP, CONTROL_LEFT, y + 2, SLIDER_WIDTH, 20, 0);
	slider->value = make("STATIC", "", SS_LEFT, VALUE_LEFT, y + 3, WINDOW_WIDTH - VALUE_LEFT - 8, 18, 0);
	slider->minimum = minimum;
	slider->maximum = maximum;
	slider->step = step;
	info.cbSize = sizeof(info);
	info.fMask = SIF_RANGE | SIF_PAGE;
	info.nMin = minimum;
	info.nMax = maximum;
	info.nPage = 0;
	SetScrollInfo(slider->bar, SB_CTL, &info, TRUE);
}

static void preset_chosen(void)
{
	int preset = (int)SendMessageA(preset_combo, CB_GETCURSEL, 0, 0);
	struct launcher_choices choices;

	if (preset < 0 || preset >= LAUNCHER_PRESET_COUNT)
		return;
	launcher_preset(preset, &choices);
	/* (the display mode stays as chosen) */
	choices.display_mode = (int)SendMessageA(mode_combo, CB_GETCURSEL, 0, 0);
	show_choices(&choices);
}

static void detect(void)
{
	unsigned long speed = measure_megahertz();
	unsigned long memory = megabytes();
	int preset = launcher_suggest(speed, memory);
	char text[160];

	if (speed)
		sprintf(text, "Found: about %lu MHz, %lu MB of memory. Suggested: %s. (The graphics card is not tested.)",
			speed, memory, launcher_preset_name(preset));
	else
		sprintf(text, "Found: %lu MB of memory; the processor's speed is not known. Suggested: %s.", memory,
			launcher_preset_name(preset));
	SetWindowTextA(hardware_text, text);
	SendMessageA(preset_combo, CB_SETCURSEL, (WPARAM)preset, 0);
	preset_chosen();
}

static void slide(HWND bar, WPARAM wparam)
{
	int which;

	for (which = 0; which < SLIDER_COUNT; which++)
	{
		if (sliders[which].bar == bar)
		{
			SCROLLINFO info;
			int position = slider_get(which);
			int page = sliders[which].maximum - sliders[which].minimum > 6 ? 4 : 1;

			info.cbSize = sizeof(info);
			info.fMask = SIF_TRACKPOS;
			GetScrollInfo(bar, SB_CTL, &info);
			switch (LOWORD(wparam))
			{
			case SB_LINELEFT: position--; break;
			case SB_LINERIGHT: position++; break;
			case SB_PAGELEFT: position -= page; break;
			case SB_PAGERIGHT: position += page; break;
			case SB_LEFT: position = sliders[which].minimum; break;
			case SB_RIGHT: position = sliders[which].maximum; break;
			case SB_THUMBTRACK:
			case SB_THUMBPOSITION: position = info.nTrackPos; break;
			default: return;
			}
			slider_set(which, position);
			return;
		}
	}
}

static void finish(int how)
{
	if (how != RESULT_QUIT)
	{
		collect_choices(&final_choices);
		final_again = (int)SendMessageA(toggles[ID_NEXT_START - ID_VSYNC], BM_GETCHECK, 0, 0) == BST_CHECKED;
	}
	result = how;
	DestroyWindow(window);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
	switch (message)
	{
	case WM_HSCROLL:
		slide((HWND)lparam, wparam);
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wparam))
		{
		case ID_PRESET:
			if (HIWORD(wparam) == CBN_SELCHANGE)
				preset_chosen();
			return 0;
		case ID_DETECT:
			detect();
			return 0;
		case ID_PLAY:
		case IDOK:
			finish(RESULT_PLAY);
			return 0;
		case ID_SAVE:
			finish(RESULT_SAVE);
			return 0;
		case ID_QUIT:
		case IDCANCEL:
			finish(RESULT_QUIT);
			return 0;
		}
		break;
	case WM_CLOSE:
		finish(RESULT_QUIT);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcA(hwnd, message, wparam, lparam);
}

/* shows the window; RESULT_PLAY, RESULT_SAVE or RESULT_QUIT. notice, if any,
says why it came up unasked. On RESULT_PLAY and RESULT_SAVE, the choices
are in choices, and whether the window is to come again next time. */
static int run_window(const char *notice, struct launcher_choices *choices, int *again)
{
	WNDCLASSA wc;
	MSG message;
	RECT rect;
	int y = 10, index, screen_x, screen_y, height = 470;
	static const char *const toggle_texts[] = { "V-Sync", "Smooth motion", "Stored room sets (PVS)",
		"Room reverb", "Fast sound mixing", "Sound", "Show this window at the next start" };

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = window_proc;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(INT_PTR)(COLOR_BTNFACE + 1);
	wc.lpszClassName = window_class;
	RegisterClassA(&wc);
	font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	rect.left = 0;
	rect.top = 0;
	rect.right = WINDOW_WIDTH;
	rect.bottom = height;
	AdjustWindowRect(&rect, WS_CAPTION | WS_SYSMENU, FALSE);
	screen_x = (GetSystemMetrics(SM_CXSCREEN) - (rect.right - rect.left)) / 2;
	screen_y = (GetSystemMetrics(SM_CYSCREEN) - (rect.bottom - rect.top)) / 2;
	window = CreateWindowExA(WS_EX_CONTROLPARENT, window_class, "Halo: Combat Evolved - Setup",
		WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_CLIPCHILDREN, screen_x < 0 ? 0 : screen_x,
		screen_y < 0 ? 0 : screen_y, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, wc.hInstance, NULL);
	if (!window)
	{
		/* (no window: the game goes on with its settings) */
		launcher_load(choices, read_setting);
		*again = config_boolean("launcher.always");
		return RESULT_PLAY;
	}

	if (notice && *notice)
	{
		notice_text = make("STATIC", notice, SS_LEFT, LEFT, y, WINDOW_WIDTH - 2 * LEFT, 30, 0);
		y += 34;
	}
	make("STATIC", "Preset:", SS_LEFT, LEFT, y + 3, LABEL_WIDTH, 18, 0);
	preset_combo = make("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, CONTROL_LEFT, y, 120, 120,
		ID_PRESET);
	for (index = 0; index < LAUNCHER_PRESET_COUNT; index++)
		SendMessageA(preset_combo, CB_ADDSTRING, 0, (LPARAM)launcher_preset_name(index));
	make("BUTTON", "Detect this PC", BS_PUSHBUTTON | WS_TABSTOP, 300, y - 1, 120, 24, ID_DETECT);
	y += 30;
	hardware_text = make("STATIC", "", SS_LEFT, LEFT, y, WINDOW_WIDTH - 2 * LEFT, 30, 0);
	y += 36;
	make("STATIC", "Display:", SS_LEFT, LEFT, y + 3, LABEL_WIDTH, 18, 0);
	mode_combo = make("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, CONTROL_LEFT, y, 150, 100, ID_MODE);
	SendMessageA(mode_combo, CB_ADDSTRING, 0, (LPARAM)"Fullscreen");
	SendMessageA(mode_combo, CB_ADDSTRING, 0, (LPARAM)"Borderless window");
	SendMessageA(mode_combo, CB_ADDSTRING, 0, (LPARAM)"Window");
	y += ROW_HEIGHT + 4;
	make_slider(SLIDER_SCALE, "Picture size:", 0, LAUNCHER_SCALE_STOPS - 1, 1, y);
	y += ROW_HEIGHT;
	make_slider(SLIDER_DETAIL, "Object detail:", LAUNCHER_DETAIL_MINIMUM / LAUNCHER_DETAIL_STEP,
		LAUNCHER_DETAIL_MAXIMUM / LAUNCHER_DETAIL_STEP, 1, y);
	y += ROW_HEIGHT;
	make_slider(SLIDER_EXTRAS, "Extras drawn:", 0, LAUNCHER_EXTRAS_STOPS - 1, 1, y);
	y += ROW_HEIGHT;
	make_slider(SLIDER_FPS, "Frame rate limit:", 0, LAUNCHER_FPS_STOPS - 1, 1, y);
	y += ROW_HEIGHT;
	make_slider(SLIDER_TEXTURES, "Texture detail:", 0, LAUNCHER_TEXTURE_STOPS - 1, 1, y);
	y += ROW_HEIGHT;
	make_slider(SLIDER_KEEP_UP, "Keep up (auto detail):", 0, LAUNCHER_KEEP_UP_STOPS - 1, 1, y);
	y += ROW_HEIGHT;
	make_slider(SLIDER_BRIGHTNESS, "Brightness:", LAUNCHER_BRIGHTNESS_MINIMUM / LAUNCHER_BRIGHTNESS_STEP,
		LAUNCHER_BRIGHTNESS_MAXIMUM / LAUNCHER_BRIGHTNESS_STEP, 1, y);
	y += ROW_HEIGHT + 6;
	/* (the short toggles two to a row, to keep the window short for a 640 by 480 screen) */
	for (index = 0; index < ID_NEXT_START - ID_VSYNC + 1; index++)
	{
		int last = index == ID_NEXT_START - ID_VSYNC;
		int column = last ? 0 : index % 2;

		toggles[index] = make("BUTTON", toggle_texts[index], BS_AUTOCHECKBOX | WS_TABSTOP,
			LEFT + column * (WINDOW_WIDTH - 2 * LEFT) / 2, y,
			last ? WINDOW_WIDTH - 2 * LEFT : (WINDOW_WIDTH - 2 * LEFT) / 2 - 4, 20, ID_VSYNC + index);
		if (last || column)
			y += 22;
	}
	y += 10;
	make("BUTTON", "Play", BS_DEFPUSHBUTTON | WS_TABSTOP, LEFT, y, 100, 28, ID_PLAY);
	make("BUTTON", "Save and exit", BS_PUSHBUTTON | WS_TABSTOP, LEFT + 110, y, 110, 28, ID_SAVE);
	make("BUTTON", "Quit", BS_PUSHBUTTON | WS_TABSTOP, WINDOW_WIDTH - LEFT - 80, y, 80, 28, ID_QUIT);

	/* (the window's height was for the most it holds) */
	rect.left = 0;
	rect.top = 0;
	rect.right = WINDOW_WIDTH;
	rect.bottom = y + 28 + 12;
	AdjustWindowRect(&rect, WS_CAPTION | WS_SYSMENU, FALSE);
	SetWindowPos(window, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);

	launcher_load(choices, read_setting);
	show_choices(choices);
	SendMessageA(toggles[ID_NEXT_START - ID_VSYNC], BM_SETCHECK, config_boolean("launcher.always"), 0);
	if (!config_boolean("launcher.configured"))
		detect();
	SetForegroundWindow(window);

	while (GetMessageA(&message, NULL, 0, 0) > 0)
	{
		if (!IsDialogMessageA(window, &message))
		{
			TranslateMessage(&message);
			DispatchMessageA(&message);
		}
	}
	UnregisterClassA(window_class, wc.hInstance);
	*choices = final_choices;
	*again = final_again;
	return result ? result : RESULT_QUIT;
}

/* ---------- public code */

/* Called before the game starts, with its command line. 0 to go on to the game, else the program is to
exit. Arguments the window uses (--setup, --no-setup) are taken out. */
int win9x_launcher_run(int *argc, char **argv)
{
	int index, out = 1, force = 0, skip = 0, ran_before_unfinished = 0;
	char notice[200];
	char flag[MAX_PATH];
	struct launcher_choices choices;
	int again = 0, how;
	DWORD attributes;

	for (index = 1; index < *argc; index++)
	{
		if (!_stricmp(argv[index], "--setup") || !_stricmp(argv[index], "-setup") || !_stricmp(argv[index], "/setup"))
			force = 1;
		else if (!_stricmp(argv[index], "--no-setup"))
			skip = 1;
		else
		{
			if (!_stricmp(argv[index], "--server"))
				skip = 1;
			argv[out++] = argv[index];
		}
	}
	argv[out] = NULL;
	*argc = out;
	if (skip || getenv("HALO_NO_LAUNCHER") || config_server_mode_enabled())
		return 0;

	notice[0] = '\0';
	flag_path(flag, sizeof(flag));
	attributes = GetFileAttributesA(flag);
	if (attributes != 0xFFFFFFFFUL)
	{
		ran_before_unfinished = 1;
		strcpy(notice, "Halo did not close normally last time. If a setting is the cause, change it here.");
	}
	if (!force && !ran_before_unfinished && !config_boolean("launcher.always") &&
		!config_boolean("launcher.show_next_start") && config_boolean("launcher.configured") &&
		!(GetKeyState(VK_SHIFT) & 0x8000))
	{
		flag_make();
		atexit(flag_remove);
		return 0;
	}

	how = run_window(notice, &choices, &again);
	if (how == RESULT_QUIT)
	{
		flag_remove();
		return 1;
	}
	launcher_apply(&choices, write_setting);
	config_write_boolean("launcher.configured", 1);
	config_write_boolean("launcher.show_next_start", 0);
	config_write_boolean("launcher.always", again);
	if (how == RESULT_SAVE)
	{
		flag_remove();
		return 1;
	}
	flag_make();
	atexit(flag_remove);
	return 0;
}
