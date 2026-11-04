/*
WIN9X_LAUNCHER.C

The setup window of the Windows 9x build: sliders and toggles for what a slow
machine needs to choose, shown before the game opens, with a line of help for
the control under the pointer or the focus, a benchmark button and a banner in
the style of the setup programs of its time. It is plain user32 (the
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
	/* the window's client area, which fits a 640 by 480 screen with its frame and caption */
	WINDOW_WIDTH = 616,
	BANNER_HEIGHT = 52,
	MARGIN = 10,
	GROUP_WIDTH = 296,
	GROUP_PAD = 10,
	GROUP_TITLE = 18,
	ROW_PITCH = 24,
	CHECK_PITCH = 20,

	ID_PRESET = 100,
	ID_DETECT,
	ID_BENCHMARK,
	ID_MODE,
	ID_PLAY,
	ID_SAVE,
	ID_QUIT,
	ID_VSYNC,
	ID_SMOOTH,
	ID_PVS,
	ID_REVERB,
	ID_SOUND,
	ID_NEXT_START,

	SLIDER_SCALE = 0,
	SLIDER_DETAIL,
	SLIDER_EXTRAS,
	SLIDER_FPS,
	SLIDER_TEXTURES,
	SLIDER_KEEP_UP,
	SLIDER_BRIGHTNESS,
	SLIDER_SOUND,
	SLIDER_COUNT,

	MAXIMUM_HINTS = 40,
	HINT_TIMER = 1,
	BENCHMARK_SECONDS = 30
};

enum
{
	RESULT_PLAY = 1,
	RESULT_SAVE,
	RESULT_QUIT,
	RESULT_BENCHMARK
};

struct slider
{
	HWND bar;
	HWND value;
	int minimum, maximum, step;
};

/* the line of help for a control: shown while the pointer is over its row, or it has the focus */
struct hint
{
	HWND control;
	RECT area;
	const char *text;
};

/* ---------- private data */

static const char window_class[] = "OpenCEWin9xSetup";
static struct slider sliders[SLIDER_COUNT];
static HWND window, preset_combo, mode_combo, hardware_text, hint_bar;
static HWND toggles[ID_NEXT_START - ID_VSYNC + 1];
static HFONT font, title_font, subtitle_font;
static int result;
/* the choices as the window left them (read before its controls went) */
static struct launcher_choices final_choices;
static int final_again, final_preset;
/* the window's own notice (why it came up unasked), drawn in the banner */
static char banner_notice[200];
static struct hint hints[MAXIMUM_HINTS];
static int hint_count;
static const char *hint_shown;
static int recommended_preset = -1;
/* a benchmark run under way: the preset it ran at (the window's advice is relative to it) */
static int benchmark_preset;

static const char default_hint[] = "Move the pointer over a setting, or tab to it, to see what it does.";

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
		sprintf(text, "%d fps", launcher_keep_up_fps(position));
		return text;
	case SLIDER_BRIGHTNESS:
		sprintf(text, "%d%%", position * LAUNCHER_BRIGHTNESS_STEP);
		return text;
	case SLIDER_SOUND:
		return launcher_sound_name(position);
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
	char text[24];

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

static int toggle_get(int id)
{
	return (int)SendMessageA(toggles[id - ID_VSYNC], BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void toggle_set(int id, int on)
{
	SendMessageA(toggles[id - ID_VSYNC], BM_SETCHECK, on != 0, 0);
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
	slider_set(SLIDER_SOUND, choices->sound_quality);
	SendMessageA(mode_combo, CB_SETCURSEL, (WPARAM)choices->display_mode, 0);
	toggle_set(ID_VSYNC, choices->vsync);
	toggle_set(ID_SMOOTH, choices->smooth_motion);
	toggle_set(ID_PVS, choices->pvs_visibility);
	toggle_set(ID_REVERB, choices->reverb);
	toggle_set(ID_SOUND, choices->sound);
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
	choices->sound_quality = slider_get(SLIDER_SOUND);
	choices->vsync = toggle_get(ID_VSYNC);
	choices->smooth_motion = toggle_get(ID_SMOOTH);
	choices->pvs_visibility = toggle_get(ID_PVS);
	choices->reverb = toggle_get(ID_REVERB);
	choices->sound = toggle_get(ID_SOUND);
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

/* what the pointer over a rectangle (a control's row), or the focus on control, shows in the help line */
static void add_hint(HWND control, int x, int y, int width, int height, const char *text)
{
	struct hint *hint;

	if (hint_count >= MAXIMUM_HINTS)
		return;
	hint = &hints[hint_count++];
	hint->control = control;
	hint->area.left = x;
	hint->area.top = y;
	hint->area.right = x + width;
	hint->area.bottom = y + height;
	hint->text = text;
}

/* a row: its label, the slider (the scroll bar: the trackbar is comctl32's) and its value's text */
static void make_slider(int which, const char *label, int minimum, int maximum, int x, int y,
	int label_width, int bar_width, int value_width, const char *hint)
{
	struct slider *slider = &sliders[which];
	SCROLLINFO info;

	make("STATIC", label, SS_LEFT, x, y + 3, label_width, 16, 0);
	slider->bar = make("SCROLLBAR", "", SBS_HORZ | WS_TABSTOP, x + label_width + 2, y + 1, bar_width, 18, 0);
	slider->value = make("STATIC", "", SS_LEFT, x + label_width + bar_width + 6, y + 3, value_width, 16, 0);
	slider->minimum = minimum;
	slider->maximum = maximum;
	slider->step = 1;
	info.cbSize = sizeof(info);
	info.fMask = SIF_RANGE | SIF_PAGE;
	info.nMin = minimum;
	info.nMax = maximum;
	info.nPage = 0;
	SetScrollInfo(slider->bar, SB_CTL, &info, TRUE);
	add_hint(slider->bar, x - 4, y, label_width + bar_width + value_width + 10, ROW_PITCH - 2, hint);
}

static void make_toggle(int id, const char *text, int x, int y, int width, const char *hint)
{
	HWND control = make("BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, width, 18, id);

	toggles[id - ID_VSYNC] = control;
	add_hint(control, x - 2, y - 1, width + 4, CHECK_PITCH, hint);
}

static void make_group(const char *title, int x, int y, int width, int height)
{
	make("BUTTON", title, BS_GROUPBOX, x, y, width, height, 0);
}

/* the presets' list, the machine's suggestion marked once Detect has been (or the first run has) found it */
static void presets_list(int selected)
{
	int index;
	char text[40];

	SendMessageA(preset_combo, CB_RESETCONTENT, 0, 0);
	for (index = 0; index < LAUNCHER_PRESET_COUNT; index++)
	{
		if (index == recommended_preset)
			sprintf(text, "%s (suggested)", launcher_preset_name(index));
		else
			strcpy(text, launcher_preset_name(index));
		SendMessageA(preset_combo, CB_ADDSTRING, 0, (LPARAM)text);
	}
	SendMessageA(preset_combo, CB_SETCURSEL, (WPARAM)selected, 0);
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
	static int checked;
	unsigned long speed = measure_megahertz();
	unsigned long memory = megabytes();
	int preset = launcher_suggest(speed, memory);
	char text[200];

	/* (the window checks once as it opens: a click after that says it checked again) */
	if (speed)
		sprintf(text, "%sFound: about %lu MHz, %lu MB of memory. Suggested: %s. (The graphics card is not tested.)",
			checked ? "Checked again. " : "", speed, memory, launcher_preset_name(preset));
	else
		sprintf(text, "%sFound: %lu MB of memory; the processor's speed is not known. Suggested: %s.",
			checked ? "Checked again. " : "", memory, launcher_preset_name(preset));
	checked = 1;
	SetWindowTextA(hardware_text, text);
	recommended_preset = preset;
	presets_list(preset);
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

/* the help line: what is under the pointer, else what has the focus, else how to find out */
static void hint_update(void)
{
	POINT point;
	const char *text = default_hint;
	HWND focus = GetFocus();
	int index;
	int found = 0;

	GetCursorPos(&point);
	ScreenToClient(window, &point);
	for (index = 0; index < hint_count && !found; index++)
	{
		if (PtInRect(&hints[index].area, point))
		{
			text = hints[index].text;
			found = 1;
		}
	}
	for (index = 0; index < hint_count && !found; index++)
	{
		if (hints[index].control == focus)
		{
			text = hints[index].text;
			found = 1;
		}
	}
	if (text != hint_shown)
	{
		hint_shown = text;
		SetWindowTextA(hint_bar, text);
	}
}

/* the banner: a blue band across the top, as the setup programs of the time had, with the game's name and either
the window's subtitle or, in yellow, why it came up unasked */
static void banner_paint(HDC dc)
{
	RECT band;
	int step, steps = WINDOW_WIDTH / 4;
	HFONT old;

	for (step = 0; step < steps; step++)
	{
		HBRUSH brush = CreateSolidBrush(RGB(0, 24 + step * 40 / steps, 96 + step * 70 / steps));

		band.left = step * 4;
		band.right = step == steps - 1 ? WINDOW_WIDTH : band.left + 4;
		band.top = 0;
		band.bottom = BANNER_HEIGHT;
		FillRect(dc, &band, brush);
		DeleteObject(brush);
	}
	/* (a light edge and a dark one under it: the raised look) */
	band.left = 0;
	band.right = WINDOW_WIDTH;
	band.top = BANNER_HEIGHT;
	band.bottom = BANNER_HEIGHT + 1;
	FillRect(dc, &band, GetSysColorBrush(COLOR_BTNSHADOW));
	SetBkMode(dc, TRANSPARENT);
	old = (HFONT)SelectObject(dc, title_font ? title_font : font);
	SetTextColor(dc, RGB(0, 0, 40));
	TextOutA(dc, 15, 5, "HALO", 4);
	SetTextColor(dc, RGB(225, 238, 255));
	TextOutA(dc, 13, 3, "HALO", 4);
	SelectObject(dc, subtitle_font ? subtitle_font : font);
	if (banner_notice[0])
	{
		SetTextColor(dc, RGB(255, 235, 120));
		TextOutA(dc, 14, 34, banner_notice, (int)strlen(banner_notice));
	}
	else
	{
		SetTextColor(dc, RGB(170, 205, 255));
		TextOutA(dc, 14, 34, "Combat Evolved  -  Setup", 24);
	}
	SelectObject(dc, old);
}

static void finish(int how)
{
	if (how != RESULT_QUIT)
	{
		collect_choices(&final_choices);
		final_again = toggle_get(ID_NEXT_START);
		final_preset = (int)SendMessageA(preset_combo, CB_GETCURSEL, 0, 0);
	}
	result = how;
	KillTimer(window, HINT_TIMER);
	DestroyWindow(window);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
	switch (message)
	{
	case WM_PAINT:
	{
		PAINTSTRUCT paint;
		HDC dc = BeginPaint(hwnd, &paint);

		banner_paint(dc);
		EndPaint(hwnd, &paint);
		return 0;
	}
	case WM_TIMER:
		if (wparam == HINT_TIMER)
			hint_update();
		return 0;
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
		case ID_BENCHMARK:
			finish(RESULT_BENCHMARK);
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

/* shows the window; RESULT_PLAY, RESULT_SAVE, RESULT_BENCHMARK or RESULT_QUIT. notice, if any,
says why it came up unasked. Except on RESULT_QUIT, the choices are in choices, and whether the window is
to come again next time, and the preset the list showed. */
static int run_window(const char *notice, struct launcher_choices *choices, int *again, int *preset_out)
{
	WNDCLASSA wc;
	MSG message;
	RECT rect;
	int y, left_x = MARGIN, right_x = MARGIN + GROUP_WIDTH + 4, inner, group_y, group_height, row;
	int screen_x, screen_y, client_height;
	enum { LABEL = 84, BAR = 104, VALUE = 84 };

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = window_proc;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(INT_PTR)(COLOR_BTNFACE + 1);
	wc.lpszClassName = window_class;
	RegisterClassA(&wc);
	font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	title_font = CreateFontA(-30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
	subtitle_font = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
	strncpy(banner_notice, notice ? notice : "", sizeof(banner_notice) - 1);
	hint_count = 0;
	hint_shown = NULL;
	recommended_preset = -1;

	/* the window's height, from its rows: the banner, the preset line and its text, two groups side by side,
	the sound's, the help line and the buttons */
	group_height = GROUP_TITLE + 4 * ROW_PITCH + 2 * CHECK_PITCH + 8;
	client_height = BANNER_HEIGHT + 6 + 28 + 30 + group_height + 6 + (GROUP_TITLE + ROW_PITCH + 8) + 6 + 30 + 6 + 26 + 8;
	rect.left = 0;
	rect.top = 0;
	rect.right = WINDOW_WIDTH;
	rect.bottom = client_height;
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
		*preset_out = LAUNCHER_PRESET_BALANCED;
		return RESULT_PLAY;
	}

	/* the preset line: the list, Detect and Benchmark, and what Detect found */
	y = BANNER_HEIGHT + 6;
	make("STATIC", "Preset:", SS_LEFT, MARGIN, y + 4, 50, 16, 0);
	preset_combo = make("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, MARGIN + 52, y, 170, 120,
		ID_PRESET);
	presets_list(LAUNCHER_PRESET_BALANCED);
	{
		HWND detect_button = make("BUTTON", "Detect this PC", BS_PUSHBUTTON | WS_TABSTOP, MARGIN + 232, y - 1, 110, 24,
			ID_DETECT);
		HWND benchmark_button = make("BUTTON", "Benchmark...", BS_PUSHBUTTON | WS_TABSTOP, MARGIN + 350, y - 1, 110, 24,
			ID_BENCHMARK);

		add_hint(preset_combo, MARGIN, y - 2, 222, 26,
			"A preset sets every control below at once: Fastest for a Pentium II class PC and a 1998 graphics card, Best for the game's own settings.");
		add_hint(detect_button, MARGIN + 230, y - 2, 114, 26,
			"Looks at this PC's processor speed and memory, and suggests a preset. The graphics card is not tested.");
		add_hint(benchmark_button, MARGIN + 348, y - 2, 114, 26,
			"Plays the main menu for 30 seconds with these settings, then says how fast it ran and which preset suits this PC.");
	}
	y += 28;
	hardware_text = make("STATIC", "", SS_LEFT, MARGIN, y, WINDOW_WIDTH - 2 * MARGIN, 28, 0);
	y += 30;

	/* the picture's group, at the left */
	group_y = y;
	make_group("Picture", left_x, group_y, GROUP_WIDTH, group_height);
	inner = left_x + GROUP_PAD;
	row = group_y + GROUP_TITLE;
	make("STATIC", "Display:", SS_LEFT, inner, row + 4, LABEL, 16, 0);
	mode_combo = make("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, inner + LABEL + 2, row, 150, 100,
		ID_MODE);
	SendMessageA(mode_combo, CB_ADDSTRING, 0, (LPARAM)"Fullscreen");
	SendMessageA(mode_combo, CB_ADDSTRING, 0, (LPARAM)"Borderless window");
	SendMessageA(mode_combo, CB_ADDSTRING, 0, (LPARAM)"Window");
	add_hint(mode_combo, inner - 4, row - 1, LABEL + BAR + VALUE + 10, ROW_PITCH - 2,
		"Fullscreen, a window with no border, or an ordinary window. Fullscreen is the fastest on most cards.");
	row += ROW_PITCH;
	make_slider(SLIDER_SCALE, "Picture size:", 0, LAUNCHER_SCALE_STOPS - 1, inner, row, LABEL, BAR, VALUE,
		"The size of the 3D picture. Native is the window's own; Original is the Xbox's 480 lines; smaller sizes draw fewer pixels and stretch them: the biggest speed-up on a slow card.");
	row += ROW_PITCH;
	make_slider(SLIDER_FPS, "Frame limit:", 0, LAUNCHER_FPS_STOPS - 1, inner, row, LABEL, BAR, VALUE,
		"The most frames a second shown. 30 is the game's own rate: more are drawn between its ticks, which a slow PC has no time for.");
	row += ROW_PITCH;
	make_slider(SLIDER_BRIGHTNESS, "Brightness:", LAUNCHER_BRIGHTNESS_MINIMUM / LAUNCHER_BRIGHTNESS_STEP,
		LAUNCHER_BRIGHTNESS_MAXIMUM / LAUNCHER_BRIGHTNESS_STEP, inner, row, LABEL, BAR, VALUE,
		"The display's gamma, for a monitor darker than the Xbox's television. 100% leaves the display alone.");
	row += ROW_PITCH;
	make_toggle(ID_VSYNC, "V-Sync", inner, row, 120,
		"Waits for the display's refresh before showing a frame: no tearing, and a lower rate when the PC is slow.");
	row += CHECK_PITCH;
	make_toggle(ID_SMOOTH, "Smooth motion", inner, row, 140,
		"Draws frames between the game's 30 ticks a second, for the 60 a second a display shows. It costs a little each.");

	/* the detail's group, at the right */
	make_group("Detail", right_x, group_y, GROUP_WIDTH, group_height);
	inner = right_x + GROUP_PAD;
	row = group_y + GROUP_TITLE;
	make_slider(SLIDER_DETAIL, "Object detail:", LAUNCHER_DETAIL_MINIMUM / LAUNCHER_DETAIL_STEP,
		LAUNCHER_DETAIL_MAXIMUM / LAUNCHER_DETAIL_STEP, inner, row, LABEL, BAR, VALUE,
		"How soon objects use coarser models as they get farther. Lower: coarser sooner, and small far ones vanish.");
	row += ROW_PITCH;
	make_slider(SLIDER_EXTRAS, "Extras drawn:", 0, LAUNCHER_EXTRAS_STOPS - 1, inner, row, LABEL, BAR, VALUE,
		"Leaves out passes that only add to the picture: shine and reflections, then shadows and decals, then fog.");
	row += ROW_PITCH;
	make_slider(SLIDER_TEXTURES, "Textures:", 0, LAUNCHER_TEXTURE_STOPS - 1, inner, row, LABEL, BAR, VALUE,
		"The world's textures made smaller: half, a quarter or an eighth of their area in memory. Takes effect with the next map.");
	row += ROW_PITCH;
	make_slider(SLIDER_KEEP_UP, "Keep up:", 0, LAUNCHER_KEEP_UP_STOPS - 1, inner, row, LABEL, BAR, VALUE,
		"Lowers the detail by itself, a level at a time, to hold this frame rate, and raises it when there is room. In the game, Ctrl+Minus and Ctrl+Equals do it by hand.");
	row += ROW_PITCH;
	make_toggle(ID_PVS, "Stored room sets (PVS)", inner, row, 200,
		"Draws the rooms the map's own table says may be seen, a lighter search than the doors': it draws more, for less work.");

	/* the sound's group, across */
	y = group_y + group_height + 6;
	make_group("Sound", MARGIN, y, WINDOW_WIDTH - 2 * MARGIN, GROUP_TITLE + ROW_PITCH + 8);
	row = y + GROUP_TITLE;
	make_slider(SLIDER_SOUND, "Quality:", 0, LAUNCHER_SOUND_STOPS - 1, MARGIN + GROUP_PAD, row, 56, 130, 100,
		"The rate the sound is mixed at, and how it is converted: lower takes less of the processor and loses the highest sounds. Next start.");
	make_toggle(ID_SOUND, "Sound", right_x + GROUP_PAD, row + 2, 100,
		"Sound on or off. Off opens no audio device at all.");
	make_toggle(ID_REVERB, "Room reverb", right_x + GROUP_PAD + 110, row + 2, 140,
		"Sounds echo as the place does: a corridor, a cave, a hall. Costs a good share of the mix.");
	y += GROUP_TITLE + ROW_PITCH + 8 + 6;

	/* the help line, and the buttons */
	hint_bar = make("STATIC", default_hint, SS_LEFT | SS_SUNKEN, MARGIN, y, WINDOW_WIDTH - 2 * MARGIN, 30, 0);
	y += 30 + 6;
	make_toggle(ID_NEXT_START, "Show this window at the next start", MARGIN, y + 4, 250,
		"Brings this window up every time the game starts. Holding Shift as it starts, or SETUP.BAT, shows it once.");
	make("BUTTON", "Play", BS_DEFPUSHBUTTON | WS_TABSTOP, WINDOW_WIDTH - MARGIN - 262, y, 80, 26, ID_PLAY);
	make("BUTTON", "Save and exit", BS_PUSHBUTTON | WS_TABSTOP, WINDOW_WIDTH - MARGIN - 176, y, 100, 26, ID_SAVE);
	make("BUTTON", "Quit", BS_PUSHBUTTON | WS_TABSTOP, WINDOW_WIDTH - MARGIN - 70, y, 70, 26, ID_QUIT);

	launcher_load(choices, read_setting);
	show_choices(choices);
	toggle_set(ID_NEXT_START, config_boolean("launcher.always"));
	if (!config_boolean("launcher.configured"))
		detect();
	SetTimer(window, HINT_TIMER, 150, NULL);
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
	DeleteObject(title_font);
	DeleteObject(subtitle_font);
	*choices = final_choices;
	*again = final_again;
	*preset_out = final_preset;
	return result;
}

/* ---------- the benchmark */

/* the frame rate of the last report of debug.frame_budget (port/linux/game/frame_budget.c) */
double halo_budget_result(void);

/* as the benchmark's run ends with the program: its settings back, and what it measured said */
static void benchmark_end(void)
{
	char text[300], message[420];
	double rate = halo_budget_result();

	launcher_advice(benchmark_preset, rate, text, sizeof(text));
	config_write("debug.exit_after", "0.0");
	config_write("debug.frame_budget", "0");
	config_write_boolean("launcher.benchmark", 0);
	config_write_boolean("launcher.show_next_start", 1);
	sprintf(message, "%s\n\nThe main menu's scene is lighter than a game's: a map in play will run slower. The setup window opens at the next start.",
		text);
	MessageBoxA(NULL, message, "Halo: Combat Evolved - Benchmark", MB_OK | MB_ICONINFORMATION);
}

/* a benchmark that did not end (a crash): its settings are taken back now, or the game would quit after 30 seconds every time */
static int benchmark_undo(void)
{
	if (!config_boolean("launcher.benchmark"))
		return 0;
	config_write("debug.exit_after", "0.0");
	config_write("debug.frame_budget", "0");
	config_write_boolean("launcher.benchmark", 0);
	return 1;
}

/* ---------- public code */

int win9x_launcher_run(int *argc, char **argv)
{
	int index, out = 1, force = 0, skip = 0, ran_before_unfinished = 0;
	char notice[200];
	char flag[MAX_PATH];
	struct launcher_choices choices;
	int again = 0, how, preset = 0, undone;
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
	/* (a benchmark that did not end has left its settings: taken back first, with the window to follow) */
	undone = benchmark_undo();
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
	if (undone)
		strcpy(notice, "The last benchmark did not end; its settings are back to normal.");
	if (!force && !ran_before_unfinished && !undone && !config_boolean("launcher.always") &&
		!config_boolean("launcher.show_next_start") && config_boolean("launcher.configured") &&
		!(GetKeyState(VK_SHIFT) & 0x8000))
	{
		flag_make();
		atexit(flag_remove);
		return 0;
	}

	how = run_window(notice, &choices, &again, &preset);
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
	if (how == RESULT_BENCHMARK)
	{
		/* the main menu for BENCHMARK_SECONDS, a report every 5 seconds, then the verdict as the program ends
		(benchmark_end puts the settings back; launcher.benchmark has them taken back at the next start if
		it never gets to) */
		char seconds[16];

		benchmark_preset = preset;
		sprintf(seconds, "%d.0", BENCHMARK_SECONDS);
		config_write_boolean("launcher.benchmark", 1);
		write_setting("debug.exit_after", seconds);
		write_setting("debug.frame_budget", "5");
		atexit(benchmark_end);
	}
	flag_make();
	atexit(flag_remove);
	return 0;
}
