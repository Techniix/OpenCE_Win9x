/*
COMMON.C

The SDL 3 functions OpenCE's platform layer calls that do not depend on the
system (port/retro_sdl/README.md): errors, hints, properties, atomics, I/O
streams on the C runtime's files and on memory, surfaces, dates, the
gamepad and key names, and globbing a folder (whose listing is the
backend's: retro_sdl_list_directory). C99 and the C runtime only, so that
every backend (Windows 9x; Mac OS 9 and DOS to come) builds it as it is.
*/

#include <SDL3/SDL.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "retro_sdl_backend.h"

/* ---------- errors */

static char error_text[512];

bool SDL_SetError(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(error_text, sizeof(error_text), format, arguments);
	va_end(arguments);
	error_text[sizeof(error_text) - 1] = '\0';
	return false;
}

const char *SDL_GetError(void)
{
	return error_text;
}

void SDL_free(void *memory)
{
	free(memory);
}

/* ---------- hints: a list of names and values */

enum
{
	MAXIMUM_HINTS = 64
};

static struct
{
	char *name;
	char *value;
} hints[MAXIMUM_HINTS];

static char *string_copy(const char *text)
{
	size_t length = strlen(text) + 1;
	char *copy = (char *)malloc(length);

	if (copy)
		memcpy(copy, text, length);
	return copy;
}

bool SDL_SetHint(const char *name, const char *value)
{
	int index;
	int free_index = -1;

	for (index = 0; index < MAXIMUM_HINTS; index++)
	{
		if (hints[index].name && !strcmp(hints[index].name, name))
			break;
		if (!hints[index].name && free_index < 0)
			free_index = index;
	}
	if (index == MAXIMUM_HINTS)
	{
		if (free_index < 0)
			return SDL_SetError("too many hints");
		index = free_index;
		hints[index].name = string_copy(name);
	}
	free(hints[index].value);
	hints[index].value = value ? string_copy(value) : NULL;
	return true;
}

const char *retro_sdl_hint(const char *name)
{
	int index;

	for (index = 0; index < MAXIMUM_HINTS; index++)
	{
		if (hints[index].name && !strcmp(hints[index].name, name))
			return hints[index].value;
	}
	return NULL;
}

bool SDL_GetHintBoolean(const char *name, bool default_value)
{
	const char *value = retro_sdl_hint(name);

	if (!value || !*value)
		return default_value;
	return !(value[0] == '0' || !strcmp(value, "false"));
}

/* ---------- properties: sets of named values */

enum
{
	MAXIMUM_PROPERTY_SETS = 64,
	MAXIMUM_PROPERTIES = 16
};

enum property_type
{
	_property_none,
	_property_pointer,
	_property_number,
	_property_float,
	_property_boolean
};

struct property
{
	char name[64];
	enum property_type type;
	void *pointer;
	Sint64 number;
	float real;
	bool boolean;
};

static struct
{
	bool used;
	struct property properties[MAXIMUM_PROPERTIES];
} property_sets[MAXIMUM_PROPERTY_SETS];

SDL_PropertiesID SDL_CreateProperties(void)
{
	int index;

	for (index = 1; index < MAXIMUM_PROPERTY_SETS; index++)
	{
		if (!property_sets[index].used)
		{
			memset(&property_sets[index], 0, sizeof(property_sets[index]));
			property_sets[index].used = true;
			return (SDL_PropertiesID)index;
		}
	}
	SDL_SetError("too many property sets");
	return 0;
}

void SDL_DestroyProperties(SDL_PropertiesID properties)
{
	if (properties > 0 && properties < MAXIMUM_PROPERTY_SETS)
		property_sets[properties].used = false;
}

static struct property *property_find(SDL_PropertiesID properties, const char *name, bool make)
{
	struct property *list;
	int index;

	if (properties <= 0 || properties >= MAXIMUM_PROPERTY_SETS || !property_sets[properties].used)
		return NULL;
	list = property_sets[properties].properties;
	for (index = 0; index < MAXIMUM_PROPERTIES; index++)
	{
		if (list[index].type != _property_none && !strcmp(list[index].name, name))
			return &list[index];
	}
	if (!make)
		return NULL;
	for (index = 0; index < MAXIMUM_PROPERTIES; index++)
	{
		if (list[index].type == _property_none)
		{
			snprintf(list[index].name, sizeof(list[index].name), "%s", name);
			return &list[index];
		}
	}
	return NULL;
}

bool retro_sdl_set_pointer_property(SDL_PropertiesID properties, const char *name, void *value)
{
	struct property *property = property_find(properties, name, true);

	if (!property)
		return SDL_SetError("cannot set property %s", name);
	property->type = _property_pointer;
	property->pointer = value;
	return true;
}

bool SDL_SetNumberProperty(SDL_PropertiesID properties, const char *name, Sint64 value)
{
	struct property *property = property_find(properties, name, true);

	if (!property)
		return SDL_SetError("cannot set property %s", name);
	property->type = _property_number;
	property->number = value;
	return true;
}

bool SDL_SetBooleanProperty(SDL_PropertiesID properties, const char *name, bool value)
{
	struct property *property = property_find(properties, name, true);

	if (!property)
		return SDL_SetError("cannot set property %s", name);
	property->type = _property_boolean;
	property->boolean = value;
	return true;
}

void *SDL_GetPointerProperty(SDL_PropertiesID properties, const char *name, void *default_value)
{
	struct property *property = property_find(properties, name, false);

	return property && property->type == _property_pointer ? property->pointer : default_value;
}

float SDL_GetFloatProperty(SDL_PropertiesID properties, const char *name, float default_value)
{
	struct property *property = property_find(properties, name, false);

	if (!property)
		return default_value;
	if (property->type == _property_float)
		return property->real;
	if (property->type == _property_number)
		return (float)property->number;
	return default_value;
}

bool SDL_GetBooleanProperty(SDL_PropertiesID properties, const char *name, bool default_value)
{
	struct property *property = property_find(properties, name, false);

	if (!property)
		return default_value;
	if (property->type == _property_boolean)
		return property->boolean;
	if (property->type == _property_number)
		return property->number != 0;
	return default_value;
}

/* ---------- atomics (the compiler's, which every target has) */

int SDL_SetAtomicInt(SDL_AtomicInt *atomic, int value)
{
	return __atomic_exchange_n(&atomic->value, value, __ATOMIC_SEQ_CST);
}

int SDL_GetAtomicInt(SDL_AtomicInt *atomic)
{
	return __atomic_load_n(&atomic->value, __ATOMIC_SEQ_CST);
}

int SDL_AddAtomicInt(SDL_AtomicInt *atomic, int value)
{
	return __atomic_fetch_add(&atomic->value, value, __ATOMIC_SEQ_CST);
}

/* ---------- I/O streams: a C runtime file, or bytes in memory */

struct SDL_IOStream
{
	FILE *file;
	const unsigned char *memory;
	size_t size;
	size_t position;
};

SDL_IOStream *SDL_IOFromFile(const char *path, const char *mode)
{
	SDL_IOStream *stream;
	char binary_mode[8];
	FILE *file;

	/* (always binary, as SDL's are) */
	snprintf(binary_mode, sizeof(binary_mode), "%s%s", mode, strchr(mode, 'b') ? "" : "b");
	file = fopen(path, binary_mode);
	if (!file)
	{
		SDL_SetError("cannot open %s", path);
		return NULL;
	}
	stream = (SDL_IOStream *)calloc(1, sizeof(*stream));
	if (!stream)
	{
		fclose(file);
		SDL_SetError("out of memory");
		return NULL;
	}
	stream->file = file;
	return stream;
}

SDL_IOStream *SDL_IOFromConstMem(const void *memory, size_t size)
{
	SDL_IOStream *stream;

	if (!memory)
	{
		SDL_SetError("no memory to read");
		return NULL;
	}
	stream = (SDL_IOStream *)calloc(1, sizeof(*stream));
	if (!stream)
	{
		SDL_SetError("out of memory");
		return NULL;
	}
	stream->memory = (const unsigned char *)memory;
	stream->size = size;
	return stream;
}

size_t SDL_ReadIO(SDL_IOStream *stream, void *buffer, size_t size)
{
	size_t count;

	if (!stream)
		return 0;
	if (stream->file)
		return fread(buffer, 1, size, stream->file);
	count = stream->size - stream->position;
	if (count > size)
		count = size;
	memcpy(buffer, stream->memory + stream->position, count);
	stream->position += count;
	return count;
}

size_t SDL_WriteIO(SDL_IOStream *stream, const void *buffer, size_t size)
{
	if (!stream || !stream->file)
	{
		SDL_SetError("the stream is read-only");
		return 0;
	}
	return fwrite(buffer, 1, size, stream->file);
}

Sint64 SDL_SeekIO(SDL_IOStream *stream, Sint64 offset, SDL_IOWhence whence)
{
	Sint64 base;

	if (!stream)
		return -1;
	if (stream->file)
	{
		int origin = whence == SDL_IO_SEEK_SET ? SEEK_SET : whence == SDL_IO_SEEK_CUR ? SEEK_CUR : SEEK_END;

		if (offset > 0x7FFFFFFF || offset < -0x7FFFFFFF || fseek(stream->file, (long)offset, origin))
			return -1;
		return ftell(stream->file);
	}
	base = whence == SDL_IO_SEEK_SET ? 0 : whence == SDL_IO_SEEK_CUR ? (Sint64)stream->position : (Sint64)stream->size;
	if (base + offset < 0 || base + offset > (Sint64)stream->size)
		return -1;
	stream->position = (size_t)(base + offset);
	return (Sint64)stream->position;
}

Sint64 SDL_TellIO(SDL_IOStream *stream)
{
	if (!stream)
		return -1;
	return stream->file ? ftell(stream->file) : (Sint64)stream->position;
}

Sint64 SDL_GetIOSize(SDL_IOStream *stream)
{
	long here;
	long end;

	if (!stream)
		return -1;
	if (!stream->file)
		return (Sint64)stream->size;
	here = ftell(stream->file);
	if (fseek(stream->file, 0, SEEK_END))
		return -1;
	end = ftell(stream->file);
	fseek(stream->file, here, SEEK_SET);
	return end;
}

bool SDL_CloseIO(SDL_IOStream *stream)
{
	bool closed = true;

	if (!stream)
		return false;
	if (stream->file)
		closed = fclose(stream->file) == 0;
	free(stream);
	return closed;
}

void *SDL_LoadFile(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data;
	long length;

	if (size)
		*size = 0;
	if (!file)
	{
		SDL_SetError("cannot open %s", path);
		return NULL;
	}
	if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET))
	{
		fclose(file);
		SDL_SetError("cannot read %s", path);
		return NULL;
	}
	/* (one more byte, zero, as SDL's: the data read as a string ends) */
	data = (unsigned char *)malloc((size_t)length + 1);
	if (!data || fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		free(data);
		fclose(file);
		SDL_SetError("cannot read %s", path);
		return NULL;
	}
	fclose(file);
	data[length] = 0;
	if (size)
		*size = (size_t)length;
	return data;
}

bool SDL_SaveFile(const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	bool written;

	if (!file)
		return SDL_SetError("cannot write %s", path);
	written = fwrite(data, 1, size, file) == size;
	return fclose(file) == 0 && written ? true : SDL_SetError("cannot write %s", path);
}

/* ---------- surfaces: 32-bit pixels in memory */

SDL_Surface *SDL_CreateSurface(int width, int height, SDL_PixelFormat format)
{
	SDL_Surface *surface;

	if (width <= 0 || height <= 0 || SDL_BYTESPERPIXEL(format) != 4)
	{
		SDL_SetError("unsupported surface");
		return NULL;
	}
	surface = (SDL_Surface *)calloc(1, sizeof(*surface));
	if (!surface)
	{
		SDL_SetError("out of memory");
		return NULL;
	}
	surface->format = format;
	surface->w = width;
	surface->h = height;
	surface->pitch = width * 4;
	surface->refcount = 1;
	surface->pixels = calloc((size_t)height, (size_t)surface->pitch);
	if (!surface->pixels)
	{
		free(surface);
		SDL_SetError("out of memory");
		return NULL;
	}
	return surface;
}

void SDL_DestroySurface(SDL_Surface *surface)
{
	if (!surface || --surface->refcount > 0)
		return;
	free(surface->pixels);
	free(surface);
}

SDL_Surface *SDL_DuplicateSurface(SDL_Surface *surface)
{
	SDL_Surface *copy;
	int row;

	if (!surface)
	{
		SDL_SetError("no surface");
		return NULL;
	}
	copy = SDL_CreateSurface(surface->w, surface->h, surface->format);
	if (!copy)
		return NULL;
	for (row = 0; row < surface->h; row++)
	{
		memcpy((unsigned char *)copy->pixels + row * copy->pitch,
			(unsigned char *)surface->pixels + row * surface->pitch, (size_t)surface->w * 4);
	}
	return copy;
}

/* (nearest pixels: the menus' pointer, sized once) */
SDL_Surface *SDL_ScaleSurface(SDL_Surface *surface, int width, int height, SDL_ScaleMode mode)
{
	SDL_Surface *scaled;
	int x, y;

	(void)mode;
	if (!surface)
	{
		SDL_SetError("no surface");
		return NULL;
	}
	scaled = SDL_CreateSurface(width, height, surface->format);
	if (!scaled)
		return NULL;
	for (y = 0; y < height; y++)
	{
		const Uint32 *source = (const Uint32 *)((unsigned char *)surface->pixels +
			(size_t)(y * surface->h / height) * surface->pitch);
		Uint32 *destination = (Uint32 *)((unsigned char *)scaled->pixels + (size_t)y * scaled->pitch);

		for (x = 0; x < width; x++)
			destination[x] = source[x * surface->w / width];
	}
	return scaled;
}

/* (one image a surface: the higher density ones are not kept) */
bool SDL_AddSurfaceAlternateImage(SDL_Surface *surface, SDL_Surface *image)
{
	(void)surface;
	(void)image;
	return true;
}

/* (PNG: the window's icon, the menus' pointer and screenshots, which a
backend without an encoder goes without) */
SDL_Surface *SDL_LoadPNG_IO(SDL_IOStream *stream, bool close)
{
	if (stream && close)
		SDL_CloseIO(stream);
	SDL_SetError("PNG is not supported");
	return NULL;
}

bool SDL_SavePNG(SDL_Surface *surface, const char *path)
{
	(void)surface;
	(void)path;
	return SDL_SetError("PNG is not supported");
}

/* ---------- dates */

bool SDL_TimeToDateTime(SDL_Time ticks, SDL_DateTime *date, bool local)
{
	time_t seconds = (time_t)(ticks / 1000000000);
	struct tm *parts = local ? localtime(&seconds) : gmtime(&seconds);

	if (!parts)
		return SDL_SetError("cannot convert the time");
	date->year = parts->tm_year + 1900;
	date->month = parts->tm_mon + 1;
	date->day = parts->tm_mday;
	date->hour = parts->tm_hour;
	date->minute = parts->tm_min;
	date->second = parts->tm_sec;
	date->nanosecond = (int)(ticks % 1000000000);
	date->day_of_week = parts->tm_wday;
	date->utc_offset = 0;
	return true;
}

/* ---------- globbing a folder (the backend lists it) */

/* (* and ? as SDL's globbing has them, case-insensitive with
SDL_GLOB_CASEINSENSITIVE) */
static bool pattern_matches(const char *pattern, const char *name, bool insensitive)
{
	while (*pattern)
	{
		if (*pattern == '*')
		{
			pattern++;
			for (;;)
			{
				if (pattern_matches(pattern, name, insensitive))
					return true;
				if (!*name++)
					return false;
			}
		}
		if (!*name)
			return false;
		if (*pattern != '?' && (insensitive ? tolower((unsigned char)*pattern) != tolower((unsigned char)*name) :
			*pattern != *name))
		{
			return false;
		}
		pattern++;
		name++;
	}
	return !*name;
}

struct glob_list
{
	const char *pattern;
	bool insensitive;
	char **names;
	int count;
	int capacity;
	size_t bytes;
};

static void glob_add(void *context, const char *name)
{
	struct glob_list *list = (struct glob_list *)context;
	char *copy;

	if (list->pattern && !pattern_matches(list->pattern, name, list->insensitive))
		return;
	if (list->count == list->capacity)
	{
		int capacity = list->capacity ? list->capacity * 2 : 32;
		char **names = (char **)realloc(list->names, (size_t)capacity * sizeof(*names));

		if (!names)
			return;
		list->names = names;
		list->capacity = capacity;
	}
	copy = string_copy(name);
	if (copy)
	{
		list->names[list->count++] = copy;
		list->bytes += strlen(copy) + 1;
	}
}

/* (one allocation, as SDL's: the pointers, a NULL, then the names; freed
with SDL_free) */
char **SDL_GlobDirectory(const char *path, const char *pattern, SDL_GlobFlags flags, int *count)
{
	struct glob_list list;
	char **result;
	char *text;
	int index;

	memset(&list, 0, sizeof(list));
	list.pattern = pattern;
	list.insensitive = (flags & SDL_GLOB_CASEINSENSITIVE) != 0;
	if (count)
		*count = 0;
	if (!retro_sdl_list_directory(path, glob_add, &list))
	{
		for (index = 0; index < list.count; index++)
			free(list.names[index]);
		free(list.names);
		return NULL;
	}
	result = (char **)malloc((size_t)(list.count + 1) * sizeof(char *) + list.bytes);
	if (result)
	{
		text = (char *)(result + list.count + 1);
		for (index = 0; index < list.count; index++)
		{
			size_t length = strlen(list.names[index]) + 1;

			memcpy(text, list.names[index], length);
			result[index] = text;
			text += length;
		}
		result[list.count] = NULL;
		if (count)
			*count = list.count;
	}
	for (index = 0; index < list.count; index++)
		free(list.names[index]);
	free(list.names);
	return result;
}

/* ---------- names */

const char *SDL_GetGamepadStringForType(SDL_GamepadType type)
{
	static const char *const names[] =
	{
		NULL, "", "xbox360", "xboxone", "ps3", "ps4", "ps5", "switchpro", "joyconleft", "joyconright", "joyconpair",
		"gamecube"
	};

	return type >= 0 && type < (int)(sizeof(names) / sizeof(names[0])) ? names[type] : NULL;
}

/* SDL's names of the keys a PC keyboard has, by scancode */
static const struct
{
	SDL_Scancode scancode;
	const char *name;
} scancode_names[] =
{
	{ SDL_SCANCODE_RETURN, "Return" }, { SDL_SCANCODE_ESCAPE, "Escape" }, { SDL_SCANCODE_BACKSPACE, "Backspace" },
	{ SDL_SCANCODE_TAB, "Tab" }, { SDL_SCANCODE_SPACE, "Space" }, { SDL_SCANCODE_MINUS, "-" },
	{ SDL_SCANCODE_EQUALS, "=" }, { SDL_SCANCODE_LEFTBRACKET, "[" }, { SDL_SCANCODE_RIGHTBRACKET, "]" },
	{ SDL_SCANCODE_BACKSLASH, "\\" }, { SDL_SCANCODE_SEMICOLON, ";" }, { SDL_SCANCODE_APOSTROPHE, "'" },
	{ SDL_SCANCODE_GRAVE, "`" }, { SDL_SCANCODE_COMMA, "," }, { SDL_SCANCODE_PERIOD, "." },
	{ SDL_SCANCODE_SLASH, "/" }, { SDL_SCANCODE_CAPSLOCK, "CapsLock" }, { SDL_SCANCODE_F1, "F1" },
	{ SDL_SCANCODE_F2, "F2" }, { SDL_SCANCODE_F3, "F3" }, { SDL_SCANCODE_F4, "F4" }, { SDL_SCANCODE_F5, "F5" },
	{ SDL_SCANCODE_F6, "F6" }, { SDL_SCANCODE_F7, "F7" }, { SDL_SCANCODE_F8, "F8" }, { SDL_SCANCODE_F9, "F9" },
	{ SDL_SCANCODE_F10, "F10" }, { SDL_SCANCODE_F11, "F11" }, { SDL_SCANCODE_F12, "F12" },
	{ SDL_SCANCODE_PRINTSCREEN, "PrintScreen" }, { SDL_SCANCODE_SCROLLLOCK, "ScrollLock" },
	{ SDL_SCANCODE_PAUSE, "Pause" }, { SDL_SCANCODE_INSERT, "Insert" }, { SDL_SCANCODE_HOME, "Home" },
	{ SDL_SCANCODE_PAGEUP, "PageUp" }, { SDL_SCANCODE_DELETE, "Delete" }, { SDL_SCANCODE_END, "End" },
	{ SDL_SCANCODE_PAGEDOWN, "PageDown" }, { SDL_SCANCODE_RIGHT, "Right" }, { SDL_SCANCODE_LEFT, "Left" },
	{ SDL_SCANCODE_DOWN, "Down" }, { SDL_SCANCODE_UP, "Up" }, { SDL_SCANCODE_NUMLOCKCLEAR, "Numlock" },
	{ SDL_SCANCODE_KP_DIVIDE, "Keypad /" }, { SDL_SCANCODE_KP_MULTIPLY, "Keypad *" },
	{ SDL_SCANCODE_KP_MINUS, "Keypad -" }, { SDL_SCANCODE_KP_PLUS, "Keypad +" },
	{ SDL_SCANCODE_KP_ENTER, "Keypad Enter" }, { SDL_SCANCODE_KP_1, "Keypad 1" }, { SDL_SCANCODE_KP_2, "Keypad 2" },
	{ SDL_SCANCODE_KP_3, "Keypad 3" }, { SDL_SCANCODE_KP_4, "Keypad 4" }, { SDL_SCANCODE_KP_5, "Keypad 5" },
	{ SDL_SCANCODE_KP_6, "Keypad 6" }, { SDL_SCANCODE_KP_7, "Keypad 7" }, { SDL_SCANCODE_KP_8, "Keypad 8" },
	{ SDL_SCANCODE_KP_9, "Keypad 9" }, { SDL_SCANCODE_KP_0, "Keypad 0" }, { SDL_SCANCODE_KP_PERIOD, "Keypad ." },
	{ SDL_SCANCODE_APPLICATION, "Application" }, { SDL_SCANCODE_LCTRL, "Left Ctrl" },
	{ SDL_SCANCODE_LSHIFT, "Left Shift" }, { SDL_SCANCODE_LALT, "Left Alt" }, { SDL_SCANCODE_LGUI, "Left GUI" },
	{ SDL_SCANCODE_RCTRL, "Right Ctrl" }, { SDL_SCANCODE_RSHIFT, "Right Shift" }, { SDL_SCANCODE_RALT, "Right Alt" },
	{ SDL_SCANCODE_RGUI, "Right GUI" },
};

const char *SDL_GetScancodeName(SDL_Scancode scancode)
{
	static char letter[2];
	size_t index;

	if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
	{
		letter[0] = (char)('A' + (scancode - SDL_SCANCODE_A));
		return letter;
	}
	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0)
	{
		letter[0] = scancode == SDL_SCANCODE_0 ? '0' : (char)('1' + (scancode - SDL_SCANCODE_1));
		return letter;
	}
	for (index = 0; index < sizeof(scancode_names) / sizeof(scancode_names[0]); index++)
	{
		if (scancode_names[index].scancode == scancode)
			return scancode_names[index].name;
	}
	return "";
}

SDL_Scancode SDL_GetScancodeFromName(const char *name)
{
	int scancode;

	if (!name || !*name)
		return SDL_SCANCODE_UNKNOWN;
	for (scancode = SDL_SCANCODE_A; scancode < SDL_SCANCODE_COUNT; scancode++)
	{
		const char *candidate = SDL_GetScancodeName((SDL_Scancode)scancode);
		size_t index;

		for (index = 0; candidate[index] && name[index] &&
			tolower((unsigned char)candidate[index]) == tolower((unsigned char)name[index]); index++)
			;
		if (*candidate && !candidate[index] && !name[index])
			return (SDL_Scancode)scancode;
	}
	return SDL_SCANCODE_UNKNOWN;
}

/* (a US layout's: the letters, digits and punctuation as their characters,
the rest by scancode, as SDL does for keys without a character) */
SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode scancode, SDL_Keymod modifiers, bool key_event)
{
	static const char punctuation[] = "-=[]\\#;'`,./";
	(void)modifiers;
	(void)key_event;

	if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
		return (SDL_Keycode)('a' + (scancode - SDL_SCANCODE_A));
	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
		return (SDL_Keycode)('1' + (scancode - SDL_SCANCODE_1));
	switch (scancode)
	{
	case SDL_SCANCODE_0: return '0';
	case SDL_SCANCODE_RETURN: return SDLK_RETURN;
	case SDL_SCANCODE_ESCAPE: return SDLK_ESCAPE;
	case SDL_SCANCODE_BACKSPACE: return SDLK_BACKSPACE;
	case SDL_SCANCODE_TAB: return SDLK_TAB;
	case SDL_SCANCODE_SPACE: return SDLK_SPACE;
	case SDL_SCANCODE_DELETE: return SDLK_DELETE;
	default:
		break;
	}
	if (scancode >= SDL_SCANCODE_MINUS && scancode <= SDL_SCANCODE_SLASH)
		return (SDL_Keycode)punctuation[scancode - SDL_SCANCODE_MINUS];
	return SDL_SCANCODE_TO_KEYCODE(scancode);
}
