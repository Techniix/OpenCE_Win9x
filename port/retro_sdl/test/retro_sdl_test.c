/*
RETRO_SDL_TEST.C

A check of port/retro_sdl on the machine itself, apart from the game: a
window with an OpenGL context, its keyboard and mouse, fullscreen, the
pointer's modes, and sound, through the same SDL 3 functions the game's
platform layer calls. What it finds goes to retro_sdl_test.log beside it
(stderr: port/win98/runtime/win9x_start.c), and the window's title shows
the last key and the frames a second.

	Esc       quit
	F         fullscreen at 640x480, and back
	R         the pointer's relative mode (as in play), and back
	Space     the tone on and off
	any key   the window's colour changes

It also starts a thread and waits on a condition, as the game's sound and
file threads do.
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static FILE *report;

#define say(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))

/* ---------- sound: a quiet tone, 440 Hz, its phase kept between calls */

static volatile LONG tone_on = 1;
static volatile LONG callbacks;
static double phase;

static void SDLCALL tone(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
	float samples[2 * 1024];
	int frames = additional / (int)(2 * sizeof(float));

	(void)userdata;
	(void)total;
	InterlockedIncrement(&callbacks);
	while (frames > 0)
	{
		int count = frames > 1024 ? 1024 : frames;
		int index;

		for (index = 0; index < count; index++)
		{
			float value = tone_on ? (float)(0.15 * sin(phase)) : 0.0f;

			samples[index * 2] = value;
			samples[index * 2 + 1] = value;
			phase += 2.0 * 3.14159265358979 * 440.0 / 48000.0;
			if (phase > 2.0 * 3.14159265358979)
				phase -= 2.0 * 3.14159265358979;
		}
		SDL_PutAudioStreamData(stream, samples, count * (int)(2 * sizeof(float)));
		frames -= count;
	}
}

/* ---------- a thread and a condition */

static SDL_Mutex *lock;
static SDL_Condition *signal;
static int answered;

static int SDLCALL worker(void *data)
{
	(void)data;
	SDL_Delay(100);
	SDL_LockMutex(lock);
	answered = 1;
	SDL_SignalCondition(signal);
	SDL_UnlockMutex(lock);
	return 42;
}

static void threads_check(void)
{
	SDL_Thread *thread;
	int status = 0;
	Uint64 start = SDL_GetTicks();

	lock = SDL_CreateMutex();
	signal = SDL_CreateCondition();
	thread = SDL_CreateThread(worker, "worker", NULL);
	SDL_LockMutex(lock);
	while (!answered && SDL_GetTicks() - start < 5000)
		SDL_WaitConditionTimeout(signal, lock, 1000);
	SDL_UnlockMutex(lock);
	SDL_WaitThread(thread, &status);
	say("thread: %s, status %d, %u ms", answered ? "answered" : "NO ANSWER", status,
		(unsigned)(SDL_GetTicks() - start));
}

/* ---------- the window */

static void title_set(SDL_Window *window, const char *text)
{
	HWND handle = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
		NULL);

	if (handle)
		SetWindowTextA(handle, text);
}

static void modes_report(void)
{
	int count = 0, index;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &count);
	const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());

	if (desktop)
		say("desktop: %dx%d at %.0f Hz, format %08x", desktop->w, desktop->h, desktop->refresh_rate,
			(unsigned)desktop->format);
	say("fullscreen modes: %d", count);
	for (index = 0; modes && index < count; index++)
		say("  %dx%d at %.0f Hz%s", modes[index]->w, modes[index]->h, modes[index]->refresh_rate,
			modes[index]->format == SDL_PIXELFORMAT_RGB565 ? " (16-bit only)" : "");
	SDL_free(modes);
}

static void gl_report(void)
{
	const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
	GLint value = 0;

	say("OpenGL vendor: %s", (const char *)glGetString(GL_VENDOR));
	say("OpenGL renderer: %s", (const char *)glGetString(GL_RENDERER));
	say("OpenGL version: %s", (const char *)glGetString(GL_VERSION));
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
	say("OpenGL largest texture: %d", value);
	glGetIntegerv(GL_RED_BITS, &value);
	say("OpenGL bits: red %d", value);
	glGetIntegerv(GL_DEPTH_BITS, &value);
	say("OpenGL bits: depth %d", value);
	glGetIntegerv(GL_STENCIL_BITS, &value);
	say("OpenGL bits: stencil %d", value);
	say("OpenGL multitexture units: %s", SDL_GL_GetProcAddress("glActiveTextureARB") ? "glActiveTextureARB found" :
		"none");
	say("OpenGL swap interval: %s", SDL_GL_SetSwapInterval(1) ? "set" : SDL_GetError());
	/* (one extension a line: the renderer's tiers are chosen from these) */
	say("OpenGL extensions:");
	while (extensions && *extensions)
	{
		const char *end = strchr(extensions, ' ');
		int length = end ? (int)(end - extensions) : (int)strlen(extensions);

		if (length)
			say("  %.*s", length, extensions);
		extensions += length;
		while (*extensions == ' ')
			extensions++;
	}
}

int main(int argc, char **argv)
{
	SDL_Window *window;
	SDL_GLContext context;
	SDL_AudioStream *stream = NULL;
	SDL_AudioSpec spec;
	int running = 1, fullscreen = 0, relative = 0;
	float red = 0.0f, green = 0.2f, blue = 0.4f;
	char last_key[64] = "none";
	Uint64 second_start;
	int frames = 0, fps = 0;
	long motions = 0;

	(void)argc;
	(void)argv;
	(void)report;
	say("retro_sdl test");
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
	{
		say("SDL_Init: %s", SDL_GetError());
		return 1;
	}
	say("base path: %s", SDL_GetBasePath());
	threads_check();
	modes_report();

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
	window = SDL_CreateWindow("retro_sdl test", 640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!window)
	{
		say("SDL_CreateWindow: %s", SDL_GetError());
		return 1;
	}
	context = SDL_GL_CreateContext(window);
	if (!context)
		say("SDL_GL_CreateContext: %s", SDL_GetError());
	else
		gl_report();

	spec.format = SDL_AUDIO_F32;
	spec.channels = 2;
	spec.freq = 48000;
	say("sound device: %s", SDL_GetAudioDeviceName(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK));
	stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, tone, NULL);
	if (!stream)
		say("sound: %s", SDL_GetError());
	else
		say("sound: open, %s", SDL_ResumeAudioStreamDevice(stream) ? "playing" : SDL_GetError());

	second_start = SDL_GetTicks();
	while (running)
	{
		SDL_Event event;

		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_EVENT_QUIT:
				say("event: quit");
				running = 0;
				break;
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				say("event: focus gained");
				break;
			case SDL_EVENT_WINDOW_FOCUS_LOST:
				say("event: focus lost");
				break;
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
				if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
				{
					snprintf(last_key, sizeof(last_key), "%s", SDL_GetScancodeName(event.key.scancode));
					red = (float)((event.key.scancode * 37) % 100) / 100.0f;
					green = (float)((event.key.scancode * 61) % 100) / 100.0f;
					blue = (float)((event.key.scancode * 89) % 100) / 100.0f;
				}
				say("event: key %s %s (scancode %d, raw %02x)%s", SDL_GetScancodeName(event.key.scancode),
					event.key.down ? "down" : "up", (int)event.key.scancode, (unsigned)event.key.raw,
					event.key.repeat ? " repeat" : "");
				if (!event.key.down || event.key.repeat)
					break;
				if (event.key.scancode == SDL_SCANCODE_ESCAPE)
					running = 0;
				else if (event.key.scancode == SDL_SCANCODE_F)
				{
					SDL_DisplayMode mode;

					fullscreen = !fullscreen;
					if (fullscreen && SDL_GetClosestFullscreenDisplayMode(SDL_GetPrimaryDisplay(), 640, 480, 0.0f,
						false, &mode))
					{
						SDL_SetWindowFullscreenMode(window, &mode);
					}
					say("fullscreen %s: %s", fullscreen ? "on" : "off",
						SDL_SetWindowFullscreen(window, fullscreen) ? "done" : SDL_GetError());
				}
				else if (event.key.scancode == SDL_SCANCODE_R)
				{
					relative = !relative;
					SDL_SetWindowRelativeMouseMode(window, relative);
					say("relative mouse %s", relative ? "on" : "off");
				}
				else if (event.key.scancode == SDL_SCANCODE_SPACE)
				{
					tone_on = !tone_on;
					say("tone %s (callbacks so far: %ld)", tone_on ? "on" : "off", (long)callbacks);
				}
				break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
				say("event: mouse button %d %s at %.0f,%.0f", event.button.button,
					event.button.down ? "down" : "up", event.button.x, event.button.y);
				break;
			case SDL_EVENT_MOUSE_WHEEL:
				say("event: wheel %.1f", event.wheel.y);
				break;
			case SDL_EVENT_MOUSE_MOTION:
				/* (a line for every hundredth: motion comes in floods) */
				if (++motions % 100 == 1)
					say("event: motion at %.0f,%.0f moved %.0f,%.0f (%ld so far)", event.motion.x, event.motion.y,
						event.motion.xrel, event.motion.yrel, motions);
				break;
			default:
				break;
			}
		}
		if (context)
		{
			glClearColor(red, green, blue, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			SDL_GL_SwapWindow(window);
		}
		else
		{
			SDL_Delay(16);
		}
		frames++;
		if (SDL_GetTicks() - second_start >= 1000)
		{
			char title[128];
			int width = 0, height = 0;

			fps = frames;
			frames = 0;
			second_start = SDL_GetTicks();
			SDL_GetWindowSize(window, &width, &height);
			snprintf(title, sizeof(title), "retro_sdl test - %dx%d, %d fps, last key %s, tone %s", width, height,
				fps, last_key, tone_on ? "on" : "off");
			title_set(window, title);
		}
	}
	say("sound callbacks: %ld", (long)callbacks);
	if (stream)
		SDL_DestroyAudioStream(stream);
	SDL_DestroyWindow(window);
	say("done");
	return 0;
}
