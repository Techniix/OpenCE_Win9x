/*
RETRO_DEVICE.C

The Xbox Direct3D 8 device on OpenGL 1.1, for the cards of 1998 to 2001
(a 3dfx Voodoo3 first) under Windows 9x, and later Mac OS 9 and DOS: the
same functions port/linux/src/d3d8_gl.c implements on OpenGL 4.5, whose
comments explain the Xbox's conventions kept here. What a card of the time
cannot do is done on the processor or left out:
- the vertex programs run on the processor (retro_vertex.c), and OpenGL
  takes their positions in clip space, through identity matrices;
- the pixel shaders are mapped onto the texture units (retro_pixel.c);
- textures are decoded, fitted and kept at 16 bits (retro_textures.c);
- only the window's back buffer is drawn into: a draw or clear into any
  other target (the shadow maps, the screen effects' copies, the water's
  reflection) is left out, and a texture made from one is not sampled;
- visibility tests (lens flares) report everything visible;
- the screen is the Xbox's 640x480, letterboxed in the window.
*/

#include "retro_internal.h"
#include "xgpu.h"
#include "sdl_platform.h"
#include "halo_ui_pointer.h"
#include "port_config.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (d3d8_resources.c's) */
void d3d8_surface_initialize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

/* ---------- state the XDK header's inline functions read and write */

DWORD D3D__RenderState[D3DRS_MAX];
DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX];
WORD *D3D__IndexData;
BYTE D3D__StateBlockDirty[1024];

#define VERTEX_PROGRAM_SIGNATURE 0x72767368UL /* 'rvsh' */
#define VERTEX_PROGRAM_SLOTS 136
#define VISIBILITY_TEST_SLOTS 4096
/* (what a test reports: more than any test covers, so fully visible) */
#define VISIBILITY_ALL_SAMPLES 1000000
#define RENDER_TARGET_MEMORY 64

static struct
{
	D3DPRESENT_PARAMETERS presentation;
	D3DSurface back_buffer;
	D3DSurface depth_buffer;
	D3DSurface *render_target;
	D3DSurface *depth_stencil;
	D3DVIEWPORT8 viewport;
	D3DMATRIX transforms[D3DTS_MAX];
	D3DBaseTexture *textures[D3DTSS_MAXSTAGES];
	D3DPalette *palettes[D3DTSS_MAXSTAGES];
	D3DSHADERCONSTANTMODE shader_constant_mode;

	struct retro_vertex_program *vertex_shader;
	BOOL fixed_function;
	struct retro_vertex_program *program_slots[VERTEX_PROGRAM_SLOTS];
	unsigned long program_address;
	float constants[RETRO_CONSTANT_COUNT][4];
	float viewport_scale[4];
	float viewport_offset[4];

	struct
	{
		DWORD data;
		UINT stride;
	} streams[16];
	UINT base_vertex_index;

	float attributes[RETRO_ATTRIBUTE_COUNT][4];
	BOOL immediate_active;
	D3DPRIMITIVETYPE immediate_type;
	float *immediate_vertices;
	unsigned long immediate_count;
	unsigned long immediate_capacity;

	/* the vertices of a draw, as the program left them, and the indices
	that draw them */
	struct retro_vertex *vertices;
	unsigned long vertex_capacity;
	WORD *indices;
	unsigned long index_capacity;

	/* the targets the game has drawn into besides the back buffer (their
	Data), whose textures are not sampled */
	DWORD render_targets[RENDER_TARGET_MEMORY];
	unsigned long render_target_count;

	BOOL query_pending[VISIBILITY_TEST_SLOTS];
	BOOL visibility_test_active;

	/* where the 640x480 screen is in the window (letterboxed), in pixels
	from the window's bottom left, as of the frame's start */
	int area[6];

	unsigned long frame;
	unsigned long next_vertex_shader_id;
	BOOL gl_ready;
	BOOL created;
} device;

static struct
{
	unsigned long draws, skipped, vertices;
	/* (where the time goes, in nanoseconds since the last log: the vertex
	programs, the pixel shaders and textures, the OpenGL calls, the swap,
	whole frames; the slowest frame, for the stutter) */
	Uint64 vertex_time, pixel_time, submit_time, swap_time, frames_time, slowest, logged, frame_start;
	unsigned long frames;
} statistics;

float retro_dword_to_float(DWORD value)
{
	union { DWORD d; float f; } u;

	u.d = value;
	return u.f;
}

void retro_color_to_floats(D3DCOLOR color, float *rgba)
{
	rgba[0] = ((color >> 16) & 0xff) / 255.0f;
	rgba[1] = ((color >> 8) & 0xff) / 255.0f;
	rgba[2] = (color & 0xff) / 255.0f;
	rgba[3] = ((color >> 24) & 0xff) / 255.0f;
}

const D3DBaseTexture *retro_device_texture(int stage)
{
	return stage >= 0 && stage < D3DTSS_MAXSTAGES ? device.textures[stage] : NULL;
}

const D3DPalette *retro_device_palette(int stage)
{
	return stage >= 0 && stage < D3DTSS_MAXSTAGES ? device.palettes[stage] : NULL;
}

int retro_device_gl_ready(void)
{
	return device.gl_ready;
}

/* (memory_watch.c and menu_files.c ask) */
int xgpu_gl_ready(void)
{
	return device.gl_ready;
}

void xgpu_gl_state_invalidate(void)
{
}

int retro_device_is_render_target(DWORD data)
{
	unsigned long index;

	if (data == device.back_buffer.Data || data == device.depth_buffer.Data)
		return TRUE;
	for (index = 0; index < device.render_target_count; index++)
	{
		if (device.render_targets[index] == data)
			return TRUE;
	}
	return FALSE;
}

/* ---------- the screen: the Xbox's 640x480 (port/linux/src/d3d8_gl.c
draws wider ones; these cards are best kept to the Xbox's) */

long halo_screen_width(void)
{
	return SCREEN_WIDTH;
}

float halo_screen_pixel_scale(void)
{
	return 1.0f;
}

int halo_model_detail_stock(void)
{
	return TRUE;
}

float halo_model_detail_scale(int level)
{
	(void)level;
	return 1.0f;
}

float halo_screen_scale(void)
{
	return 1.0f;
}

long halo_shadow_map_scale(void)
{
	return 1;
}

void halo_screen_ui_offset(unsigned char centered)
{
	(void)centered;
}

void halo_screen_ui_offset_view(long view_width)
{
	(void)view_width;
}

long halo_screen_ui_offset_current(void)
{
	return 0;
}

long halo_screen_commit(void)
{
	return SCREEN_WIDTH;
}

void halo_screen_anti_alias(short x0, short y0, short x1, short y1)
{
	(void)x0;
	(void)y0;
	(void)x1;
	(void)y1;
}

void halo_screen_tone_map(short x0, short y0, short x1, short y1)
{
	(void)x0;
	(void)y0;
	(void)x1;
	(void)y1;
}

int halo_hdr_calibration_available(void)
{
	return 0;
}

void halo_hdr_calibration_show(int pattern, int paper_white, int peak_brightness)
{
	(void)pattern;
	(void)paper_white;
	(void)peak_brightness;
}

void d3d8_gl_set_height_fade(float inverse_softness, float height, float offset, float scale)
{
	(void)inverse_softness;
	(void)height;
	(void)offset;
	(void)scale;
}

/* the 640x480 screen's place in the window: as large as fits, its shape kept */
static void screen_area_update(void)
{
	int width = SCREEN_WIDTH, height = SCREEN_HEIGHT, fit_width, fit_height;

	platform_video_drawable_size(&width, &height);
	if (width <= 0 || height <= 0)
	{
		width = SCREEN_WIDTH;
		height = SCREEN_HEIGHT;
	}
	fit_width = width;
	fit_height = width * SCREEN_HEIGHT / SCREEN_WIDTH;
	if (fit_height > height)
	{
		fit_height = height;
		fit_width = height * SCREEN_WIDTH / SCREEN_HEIGHT;
	}
	if (width != device.area[4] || height != device.area[5])
	{
		platform_log("Direct3D: the window is %dx%d, the screen drawn at %dx%d in it", width, height, fit_width, fit_height);
		if (device.area[4])
			retro_gl_window_changed();
		device.area[4] = width;
		device.area[5] = height;
	}
	device.area[0] = (width - fit_width) / 2;
	device.area[1] = (height - fit_height) / 2;
	device.area[2] = fit_width;
	device.area[3] = fit_height;
}

/* a rectangle of the screen (Direct3D's: from the top left) in the window's
pixels (OpenGL's: from the bottom left) */
static void screen_rectangle(long x, long y, long width, long height, RGLint *out)
{
	long x0 = device.area[0] + x * device.area[2] / SCREEN_WIDTH;
	long x1 = device.area[0] + (x + width) * device.area[2] / SCREEN_WIDTH;
	long y0 = device.area[1] + (SCREEN_HEIGHT - y - height) * device.area[3] / SCREEN_HEIGHT;
	long y1 = device.area[1] + (SCREEN_HEIGHT - y) * device.area[3] / SCREEN_HEIGHT;

	out[0] = (RGLint)x0;
	out[1] = (RGLint)y0;
	out[2] = (RGLint)(x1 - x0);
	out[3] = (RGLint)(y1 - y0);
}

/* ---------- vertical blank emulation (as d3d8_gl.c: a 60 Hz thread) */

static SDL_Mutex *vertical_blank_lock;
static SDL_Condition *vertical_blank_condition;
static D3DCALLBACK vertical_blank_callback;
static unsigned long vertical_blank_count;
static volatile unsigned int flip_count;
static unsigned long pending_flips;
static BOOL vertical_blank_started;

static int SDLCALL vertical_blank_thread(void *unused)
{
	Uint64 next = SDL_GetTicksNS();

	(void)unused;
	for (;;)
	{
		D3DCALLBACK callback;
		Uint64 now;

		next += 1000000000ULL / 60;
		now = SDL_GetTicksNS();
		if (next > now)
			SDL_DelayPrecise(next - now);
		else
			next = now;
		SDL_LockMutex(vertical_blank_lock);
		vertical_blank_count++;
		if (pending_flips)
		{
			pending_flips--;
			flip_count++;
		}
		callback = vertical_blank_callback;
		SDL_BroadcastCondition(vertical_blank_condition);
		SDL_UnlockMutex(vertical_blank_lock);
		if (callback)
			callback(0);
	}
	return 0;
}

static void vertical_blank_start(void)
{
	if (vertical_blank_started)
		return;
	vertical_blank_lock = SDL_CreateMutex();
	vertical_blank_condition = SDL_CreateCondition();
	if (vertical_blank_lock && vertical_blank_condition)
	{
		SDL_Thread *thread = SDL_CreateThread(vertical_blank_thread, "vertical blank", NULL);

		if (thread)
		{
			SDL_DetachThread(thread);
			vertical_blank_started = TRUE;
			return;
		}
	}
	platform_log("cannot start the vertical blank thread");
}

volatile unsigned int *d3d_find_flipcount(void)
{
	return &flip_count;
}

void WINAPI D3DDevice_SetVerticalBlankCallback(D3DCALLBACK callback)
{
	vertical_blank_start();
	if (!vertical_blank_started)
		return;
	SDL_LockMutex(vertical_blank_lock);
	vertical_blank_callback = callback;
	SDL_UnlockMutex(vertical_blank_lock);
}

void WINAPI D3DDevice_BlockUntilVerticalBlank(void)
{
	unsigned long count;

	vertical_blank_start();
	if (!vertical_blank_started)
		return;
	SDL_LockMutex(vertical_blank_lock);
	count = vertical_blank_count;
	while (vertical_blank_count == count)
		SDL_WaitConditionTimeout(vertical_blank_condition, vertical_blank_lock, 100);
	SDL_UnlockMutex(vertical_blank_lock);
}

/* ---------- constants and the viewport */

static void viewport_update_constants(void)
{
	/* (a 24-bit depth buffer's range, or 16 bits for a D16 target) */
	float zscale = 16777215.0f;

	if (device.depth_stencil)
	{
		struct xgpu_texture_description description;

		xgpu_texture_describe(device.depth_stencil->Format, device.depth_stencil->Size, &description);
		if (description.format == D3DFMT_D16 || description.format == D3DFMT_LIN_D16 ||
			description.format == D3DFMT_F16 || description.format == D3DFMT_LIN_F16)
		{
			zscale = 65535.0f;
		}
	}
	device.viewport_scale[0] = device.viewport.Width * 0.5f;
	device.viewport_scale[1] = -(float)device.viewport.Height * 0.5f;
	device.viewport_scale[2] = zscale * (device.viewport.MaxZ - device.viewport.MinZ);
	device.viewport_scale[3] = 0.0f;
	device.viewport_offset[0] = device.viewport.X + device.viewport.Width * 0.5f;
	device.viewport_offset[1] = device.viewport.Y + device.viewport.Height * 0.5f;
	device.viewport_offset[2] = zscale * device.viewport.MinZ;
	device.viewport_offset[3] = 0.0f;
	if (!(device.shader_constant_mode & D3DSCM_NORESERVEDCONSTANTS))
	{
		memcpy(device.constants[RETRO_CONSTANT_BIAS - 38], device.viewport_scale, sizeof(device.viewport_scale));
		memcpy(device.constants[RETRO_CONSTANT_BIAS - 37], device.viewport_offset, sizeof(device.viewport_offset));
	}
}

static void surface_dimensions(const D3DSurface *surface, unsigned long *width, unsigned long *height)
{
	struct xgpu_texture_description description;

	xgpu_texture_describe(surface->Format, surface->Size, &description);
	*width = description.width;
	*height = description.height;
}

/* ---------- the device */

static D3DDevice *device_pointer(void)
{
	return (D3DDevice *)&device;
}

Direct3D *WINAPI Direct3DCreate8(UINT sdk_version)
{
	(void)sdk_version;
	return (Direct3D *)1;
}

void WINAPI Direct3D_SetPushBufferSize(DWORD push_buffer_size, DWORD segment_count)
{
	(void)push_buffer_size;
	(void)segment_count;
}

static void gl_initialize(void)
{
	if (!retro_gl_load())
	{
		platform_log("Direct3D: this OpenGL lacks what the renderer needs; nothing is displayed");
		return;
	}
	RGL.rgl_glMatrixMode(RGL_PROJECTION);
	RGL.rgl_glLoadIdentity();
	RGL.rgl_glMatrixMode(RGL_MODELVIEW);
	RGL.rgl_glLoadIdentity();
	RGL.rgl_glDisable(RGL_LIGHTING);
	RGL.rgl_glDisable(RGL_DITHER);
	RGL.rgl_glHint(RGL_PERSPECTIVE_CORRECTION_HINT, RGL_NICEST);
	RGL.rgl_glEnableClientState(RGL_VERTEX_ARRAY);
	RGL.rgl_glEnableClientState(RGL_COLOR_ARRAY);
	device.gl_ready = TRUE;
	screen_area_update();
	/* (the textures' pages write-protected, so that a write to one is seen:
	retro_textures.c) */
	memory_watch_initialize();
}

HRESULT WINAPI Direct3D_CreateDevice(UINT adapter, D3DDEVTYPE device_type, void *unused, DWORD behavior_flags,
	D3DPRESENT_PARAMETERS *presentation_parameters, D3DDevice **returned_device)
{
	int index;

	(void)adapter;
	(void)device_type;
	(void)unused;
	(void)behavior_flags;
	if (!device.created)
	{
		memset(&device, 0, sizeof(device));
		if (presentation_parameters)
			device.presentation = *presentation_parameters;
		d3d8_surface_initialize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, SCREEN_WIDTH, SCREEN_HEIGHT);
		d3d8_surface_initialize(&device.depth_buffer, D3DFMT_LIN_D24S8, SCREEN_WIDTH, SCREEN_HEIGHT);
		device.render_target = &device.back_buffer;
		device.depth_stencil = &device.depth_buffer;
		for (index = 0; index < D3DTS_MAX; index++)
		{
			device.transforms[index]._11 = 1.0f;
			device.transforms[index]._22 = 1.0f;
			device.transforms[index]._33 = 1.0f;
			device.transforms[index]._44 = 1.0f;
		}
		device.viewport.Width = SCREEN_WIDTH;
		device.viewport.Height = SCREEN_HEIGHT;
		device.viewport.MaxZ = 1.0f;
		device.next_vertex_shader_id = 1;
		D3D__RenderState[D3DRS_ZENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZWRITEENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
		D3D__RenderState[D3DRS_COLORWRITEENABLE] = D3DCOLORWRITEENABLE_ALL;
		D3D__RenderState[D3DRS_SRCBLEND] = D3DBLEND_ONE;
		D3D__RenderState[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
		D3D__RenderState[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
		D3D__RenderState[D3DRS_CULLMODE] = D3DCULL_CCW;
		D3D__RenderState[D3DRS_FRONTFACE] = D3DFRONT_CW;
		D3D__RenderState[D3DRS_FILLMODE] = D3DFILL_SOLID;
		D3D__RenderState[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILWRITEMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
		for (index = 0; index < D3DTSS_MAXSTAGES; index++)
		{
			D3D__TextureState[index][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_MAGFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MINFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MAXANISOTROPY] = 1;
		}
		viewport_update_constants();
		/* (the window's own depth and stencil buffers, which this device
		draws with: port/retro_sdl) */
		SDL_SetHint("RETRO_SDL_GL_MINIMUM_DEPTH", "16");
		SDL_SetHint("RETRO_SDL_GL_MINIMUM_STENCIL", "8");
		SDL_SetHint("RETRO_SDL_GL_FIXED_SIZE", "640x480");
		if (!config_boolean("debug.null_renderer") && platform_video_initialize(SCREEN_WIDTH, SCREEN_HEIGHT))
			gl_initialize();
		else
			platform_log("Direct3D: running without a window (nothing is displayed)");
		device.created = TRUE;
	}
	*returned_device = device_pointer();
	return S_OK;
}

ULONG WINAPI D3DDevice_Release(void)
{
	return 1;
}

void WINAPI D3DDevice_GetDeviceCaps(D3DCAPS8 *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->DeviceType = D3DDEVTYPE_HAL;
	caps->MaxTextureWidth = 4096;
	caps->MaxTextureHeight = 4096;
	caps->MaxVolumeExtent = 512;
	caps->MaxTextureRepeat = 8192;
	caps->MaxTextureAspectRatio = 4096;
	caps->MaxAnisotropy = 4;
	caps->MaxTextureBlendStages = 4;
	caps->MaxSimultaneousTextures = 4;
	caps->MaxActiveLights = 8;
	caps->MaxVertexBlendMatrices = 4;
	caps->MaxPointSize = 64.0f;
	caps->MaxPrimitiveCount = 0xfffff;
	caps->MaxVertexIndex = 0xffff;
	caps->MaxStreams = 16;
	caps->MaxStreamStride = 255;
	caps->VertexShaderVersion = D3DVS_VERSION(1, 1);
	caps->MaxVertexShaderConst = 192;
	caps->PixelShaderVersion = D3DPS_VERSION(1, 1);
	caps->MaxPixelShaderValue = 1.0f;
}

void WINAPI D3DDevice_GetBackBuffer(INT back_buffer, D3DBACKBUFFER_TYPE type, D3DSurface **result)
{
	(void)back_buffer;
	(void)type;
	/* (the front buffer, -1, which the loading screen blurs into its
	light's trails, is the back buffer here: the trails go without it) */
	device.back_buffer.Common++;
	*result = &device.back_buffer;
}

HRESULT WINAPI D3DDevice_GetDepthStencilSurface(D3DSurface **result)
{
	*result = device.depth_stencil;
	if (!*result)
		return D3DERR_NOTFOUND;
	(*result)->Common++;
	return S_OK;
}

void WINAPI D3DDevice_SetRenderTarget(D3DSurface *render_target, D3DSurface *depth_stencil)
{
	if (render_target)
		device.render_target = render_target;
	device.depth_stencil = depth_stencil;
	if (device.render_target)
	{
		unsigned long width, height, index;

		if (device.render_target != &device.back_buffer)
		{
			for (index = 0; index < device.render_target_count; index++)
			{
				if (device.render_targets[index] == device.render_target->Data)
					break;
			}
			if (index == device.render_target_count && device.render_target_count < RENDER_TARGET_MEMORY)
				device.render_targets[device.render_target_count++] = device.render_target->Data;
		}
		surface_dimensions(device.render_target, &width, &height);
		device.viewport.X = 0;
		device.viewport.Y = 0;
		device.viewport.Width = width;
		device.viewport.Height = height;
		device.viewport.MinZ = 0.0f;
		device.viewport.MaxZ = 1.0f;
	}
	viewport_update_constants();
}

/* draws and clears reach the window only while the back buffer is the target */
static BOOL drawing_to_screen(void)
{
	return device.gl_ready && device.render_target == &device.back_buffer;
}

void WINAPI D3DDevice_SetViewport(CONST D3DVIEWPORT8 *viewport)
{
	device.viewport = *viewport;
	viewport_update_constants();
}

void WINAPI D3DDevice_SetTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		device.transforms[state] = *matrix;
}

void WINAPI D3DDevice_GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		*matrix = device.transforms[state];
}

void WINAPI D3DDevice_SetFlickerFilter(DWORD filter) { (void)filter; }
void WINAPI D3DDevice_SetSoftDisplayFilter(BOOL enable) { (void)enable; }

void WINAPI D3DDevice_SetShaderConstantMode(D3DSHADERCONSTANTMODE mode)
{
	device.shader_constant_mode = mode;
	viewport_update_constants();
}

BOOL WINAPI D3DDevice_IsBusy(void)
{
	return FALSE;
}

void WINAPI D3DDevice_KickPushBuffer(void)
{
	if (device.gl_ready)
		RGL.rgl_glFlush();
}

void WINAPI D3DDevice_InsertCallback(D3DCALLBACKTYPE type, D3DCALLBACK callback, DWORD context)
{
	(void)type;
	if (callback)
		callback(context);
}

HRESULT WINAPI D3DDevice_PersistDisplay(void)
{
	return S_OK;
}

/* ---------- visibility tests: everything visible (no occlusion queries) */

void WINAPI D3DDevice_BeginVisibilityTest(void)
{
	device.visibility_test_active = TRUE;
}

HRESULT WINAPI D3DDevice_EndVisibilityTest(DWORD index)
{
	device.visibility_test_active = FALSE;
	device.query_pending[index % VISIBILITY_TEST_SLOTS] = TRUE;
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetVisibilityTestResult(DWORD index, UINT *result, ULONGLONG *time_stamp)
{
	if (time_stamp)
		*time_stamp = 0;
	if (result)
		*result = device.query_pending[index % VISIBILITY_TEST_SLOTS] ? VISIBILITY_ALL_SAMPLES : 0;
	return S_OK;
}

/* ---------- render and texture stage state (as d3d8_gl.c) */

void D3DFASTCALL D3DDevice_SetRenderState_Simple(DWORD method, DWORD value)
{
	(void)method;
	(void)value;
}

void D3DFASTCALL D3DDevice_SetRenderState_Deferred(D3DRENDERSTATETYPE state, DWORD value)
{
	if ((unsigned long)state < D3DRS_MAX)
		D3D__RenderState[state] = value;
}

void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value)
{
	float offset = -(float)value;
	float slope = offset * 0.25f;
	DWORD enable = value != 0;

	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZSLOPESCALE], &slope, sizeof(slope));
	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZOFFSET], &offset, sizeof(offset));
	D3D__RenderState[D3DRS_POINTOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_WIREFRAMEOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_SOLIDOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_ZBIAS] = value;
}

void WINAPI D3DDevice_SetRenderStateNotInline(D3DRENDERSTATETYPE state, DWORD value)
{
	if (state == D3DRS_ZBIAS)
		D3DDevice_SetRenderState_ZBias(value);
	else if ((unsigned long)state < D3DRS_MAX)
		D3D__RenderState[state] = value;
}

#define COMPLEX_RENDER_STATE(name, state) \
	void WINAPI D3DDevice_SetRenderState_##name(DWORD value) { D3D__RenderState[state] = value; }

COMPLEX_RENDER_STATE(PSTextureModes, D3DRS_PSTEXTUREMODES)
COMPLEX_RENDER_STATE(VertexBlend, D3DRS_VERTEXBLEND)
COMPLEX_RENDER_STATE(FogColor, D3DRS_FOGCOLOR)
COMPLEX_RENDER_STATE(FillMode, D3DRS_FILLMODE)
COMPLEX_RENDER_STATE(BackFillMode, D3DRS_BACKFILLMODE)
COMPLEX_RENDER_STATE(TwoSidedLighting, D3DRS_TWOSIDEDLIGHTING)
COMPLEX_RENDER_STATE(NormalizeNormals, D3DRS_NORMALIZENORMALS)
COMPLEX_RENDER_STATE(ZEnable, D3DRS_ZENABLE)
COMPLEX_RENDER_STATE(StencilEnable, D3DRS_STENCILENABLE)
COMPLEX_RENDER_STATE(StencilFail, D3DRS_STENCILFAIL)
COMPLEX_RENDER_STATE(FrontFace, D3DRS_FRONTFACE)
COMPLEX_RENDER_STATE(CullMode, D3DRS_CULLMODE)
COMPLEX_RENDER_STATE(TextureFactor, D3DRS_TEXTUREFACTOR)
COMPLEX_RENDER_STATE(LogicOp, D3DRS_LOGICOP)
COMPLEX_RENDER_STATE(EdgeAntiAlias, D3DRS_EDGEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleAntiAlias, D3DRS_MULTISAMPLEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleMask, D3DRS_MULTISAMPLEMASK)
COMPLEX_RENDER_STATE(MultiSampleType, D3DRS_MULTISAMPLETYPE)
COMPLEX_RENDER_STATE(ShadowFunc, D3DRS_SHADOWFUNC)
COMPLEX_RENDER_STATE(LineWidth, D3DRS_LINEWIDTH)
COMPLEX_RENDER_STATE(Dxt1NoiseEnable, D3DRS_DXT1NOISEENABLE)
COMPLEX_RENDER_STATE(YuvEnable, D3DRS_YUVENABLE)
COMPLEX_RENDER_STATE(OcclusionCullEnable, D3DRS_OCCLUSIONCULLENABLE)
COMPLEX_RENDER_STATE(StencilCullEnable, D3DRS_STENCILCULLENABLE)
COMPLEX_RENDER_STATE(RopZCmpAlwaysRead, D3DRS_ROPZCMPALWAYSREAD)
COMPLEX_RENDER_STATE(RopZRead, D3DRS_ROPZREAD)
COMPLEX_RENDER_STATE(DoNotCullUncompressed, D3DRS_DONOTCULLUNCOMPRESSED)

void D3DFASTCALL D3DDevice_SetTextureState_Deferred(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES && (unsigned long)type < D3DTSS_MAX)
		D3D__TextureState[stage][type] = value;
}

void WINAPI D3DDevice_SetTextureState_TexCoordIndex(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_TEXCOORDINDEX] = value;
}

void WINAPI D3DDevice_SetTextureState_BorderColor(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_BORDERCOLOR] = value;
}

void WINAPI D3DDevice_SetTextureState_ColorKeyColor(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_COLORKEYCOLOR] = value;
}

void WINAPI D3DDevice_SetTextureState_BumpEnv(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES && (unsigned long)type < D3DTSS_MAX)
		D3D__TextureState[stage][type] = value;
}

void WINAPI D3DDevice_SetTexture(DWORD stage, D3DBaseTexture *texture)
{
	if (stage < D3DTSS_MAXSTAGES)
		device.textures[stage] = texture;
}

void WINAPI D3DDevice_SetPalette(DWORD stage, D3DPalette *palette)
{
	if (stage < D3DTSS_MAXSTAGES)
		device.palettes[stage] = palette;
}

void WINAPI D3DDevice_SetPixelShaderProgram(D3DPIXELSHADERDEF *definition)
{
	if (!definition)
		return;
	memcpy(&D3D__RenderState[D3DRS_PSALPHAINPUTS0], definition->PSAlphaInputs, sizeof(definition->PSAlphaInputs));
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSABCD] = definition->PSFinalCombinerInputsABCD;
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSEFG] = definition->PSFinalCombinerInputsEFG;
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT0_0], definition->PSConstant0, sizeof(definition->PSConstant0));
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT1_0], definition->PSConstant1, sizeof(definition->PSConstant1));
	memcpy(&D3D__RenderState[D3DRS_PSALPHAOUTPUTS0], definition->PSAlphaOutputs, sizeof(definition->PSAlphaOutputs));
	memcpy(&D3D__RenderState[D3DRS_PSRGBINPUTS0], definition->PSRGBInputs, sizeof(definition->PSRGBInputs));
	D3D__RenderState[D3DRS_PSCOMPAREMODE] = definition->PSCompareMode;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0] = definition->PSFinalCombinerConstant0;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1] = definition->PSFinalCombinerConstant1;
	memcpy(&D3D__RenderState[D3DRS_PSRGBOUTPUTS0], definition->PSRGBOutputs, sizeof(definition->PSRGBOutputs));
	D3D__RenderState[D3DRS_PSCOMBINERCOUNT] = definition->PSCombinerCount;
	D3D__RenderState[D3DRS_PSTEXTUREMODES] = definition->PSTextureModes;
	D3D__RenderState[D3DRS_PSDOTMAPPING] = definition->PSDotMapping;
	D3D__RenderState[D3DRS_PSINPUTTEXTURE] = definition->PSInputTexture;
}

/* ---------- vertex shaders */

HRESULT WINAPI D3DDevice_CreateVertexShader(CONST DWORD *declaration, CONST DWORD *function, DWORD *handle, DWORD usage)
{
	struct retro_vertex_program *program = calloc(1, sizeof(*program));

	(void)usage;
	if (!program)
		return E_OUTOFMEMORY;
	program->signature = VERTEX_PROGRAM_SIGNATURE;
	program->id = device.next_vertex_shader_id++;
	if (function)
	{
		/* header: program type in the low word, instruction count in the high */
		unsigned long count = function[0] >> 16;

		program->instructions = calloc(count ? count : 1, sizeof(*program->instructions));
		if (!program->instructions)
		{
			free(program);
			return E_OUTOFMEMORY;
		}
		retro_vertex_program_decode(program, function + 1, count);
	}
	retro_vertex_declaration_parse(program, declaration);
	*handle = (DWORD)program;
	return S_OK;
}

static struct retro_vertex_program *program_from_handle(DWORD handle)
{
	struct retro_vertex_program *program = (struct retro_vertex_program *)handle;

	if (!handle || (handle & 1) || program->signature != VERTEX_PROGRAM_SIGNATURE)
		return NULL;
	return program;
}

void halo_vertex_shader_lighting(unsigned long handle)
{
	(void)handle;
}

void WINAPI D3DDevice_DeleteVertexShader(DWORD handle)
{
	(void)handle;
}

void WINAPI D3DDevice_SetVertexShader(DWORD handle)
{
	struct retro_vertex_program *program = program_from_handle(handle);

	if (program)
	{
		device.vertex_shader = program;
		device.program_address = 0;
		device.program_slots[0] = program;
		device.fixed_function = FALSE;
	}
	else if (!handle)
	{
		device.fixed_function = TRUE;
	}
}

void WINAPI D3DDevice_LoadVertexShader(DWORD handle, DWORD address)
{
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_slots[address] = program_from_handle(handle);
}

void WINAPI D3DDevice_SelectVertexShader(DWORD handle, DWORD address)
{
	struct retro_vertex_program *program = program_from_handle(handle);

	device.fixed_function = FALSE;
	if (program)
		device.vertex_shader = program;
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_address = address;
}

void WINAPI D3DDevice_GetVertexShaderSize(DWORD handle, UINT *size)
{
	struct retro_vertex_program *program = program_from_handle(handle);

	*size = program ? program->instruction_count : 0;
}

void WINAPI D3DDevice_SetVertexShaderConstant(INT reg, CONST void *constant_data, DWORD constant_count)
{
	long first = reg + RETRO_CONSTANT_BIAS;

	if (first < 0 || first >= RETRO_CONSTANT_COUNT)
		return;
	if (first + (long)constant_count > RETRO_CONSTANT_COUNT)
		constant_count = RETRO_CONSTANT_COUNT - first;
	memcpy(device.constants[first], constant_data, constant_count * sizeof(device.constants[0]));
}

static struct retro_vertex_program *current_program(void)
{
	struct retro_vertex_program *program = device.program_slots[device.program_address];

	return program ? program : device.vertex_shader;
}

/* ---------- raster state */

static RGLenum stencil_operation(DWORD operation)
{
	if (!operation)
		return RGL_ZERO;
	/* (the wrapping ones saturate without EXT_stencil_wrap) */
	if (operation == RGL_INCR_WRAP && !retro_gl_capabilities.stencil_wrap)
		return RGL_INCR;
	if (operation == RGL_DECR_WRAP && !retro_gl_capabilities.stencil_wrap)
		return RGL_DECR;
	return (RGLenum)operation;
}

/* a blend factor OpenGL 1.1 has: the constant color's (OpenGL 1.2's
imaging subset, which these cards lack) as one or zero */
static RGLenum blend_factor(DWORD factor)
{
	switch (factor)
	{
	case D3DBLEND_CONSTANTCOLOR:
	case D3DBLEND_CONSTANTALPHA:
		return RGL_ONE;
	case D3DBLEND_INVCONSTANTCOLOR:
	case D3DBLEND_INVCONSTANTALPHA:
		return RGL_ZERO;
	default:
		return (RGLenum)factor;
	}
}

static void raster_state_apply(void)
{
	DWORD *rs = D3D__RenderState;
	DWORD write = rs[D3DRS_COLORWRITEENABLE];
	BOOL has_depth = device.depth_stencil != NULL;
	BOOL depth_test = has_depth && rs[D3DRS_ZENABLE];
	RGLint viewport[4];

	screen_rectangle((long)device.viewport.X, (long)device.viewport.Y, (long)device.viewport.Width,
		(long)device.viewport.Height, viewport);
	RGL.rgl_glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	/* (the NV2A clips to the viewport: a split screen's windows) */
	RGL.rgl_glScissor(viewport[0], viewport[1], viewport[2], viewport[3]);
	RGL.rgl_glEnable(RGL_SCISSOR_TEST);
	RGL.rgl_glDepthRange(device.viewport.MinZ, device.viewport.MaxZ);
	if (depth_test)
	{
		RGL.rgl_glEnable(RGL_DEPTH_TEST);
		RGL.rgl_glDepthFunc(rs[D3DRS_ZFUNC] ? (RGLenum)rs[D3DRS_ZFUNC] : RGL_NEVER);
	}
	else
	{
		RGL.rgl_glDisable(RGL_DEPTH_TEST);
	}
	RGL.rgl_glDepthMask(depth_test && rs[D3DRS_ZWRITEENABLE] ? RGL_TRUE : RGL_FALSE);
	if (has_depth && rs[D3DRS_STENCILENABLE] && retro_gl_capabilities.stencil_bits)
	{
		RGL.rgl_glEnable(RGL_STENCIL_TEST);
		RGL.rgl_glStencilFunc(rs[D3DRS_STENCILFUNC] ? (RGLenum)rs[D3DRS_STENCILFUNC] : RGL_NEVER,
			(RGLint)rs[D3DRS_STENCILREF], rs[D3DRS_STENCILMASK]);
		RGL.rgl_glStencilOp(stencil_operation(rs[D3DRS_STENCILFAIL]), stencil_operation(rs[D3DRS_STENCILZFAIL]),
			stencil_operation(rs[D3DRS_STENCILPASS]));
		RGL.rgl_glStencilMask(rs[D3DRS_STENCILWRITEMASK]);
	}
	else
	{
		RGL.rgl_glDisable(RGL_STENCIL_TEST);
	}
	if (rs[D3DRS_ALPHABLENDENABLE])
	{
		RGL.rgl_glEnable(RGL_BLEND);
		RGL.rgl_glBlendFunc(blend_factor(rs[D3DRS_SRCBLEND]), blend_factor(rs[D3DRS_DESTBLEND]));
	}
	else
	{
		RGL.rgl_glDisable(RGL_BLEND);
	}
	if (rs[D3DRS_ALPHATESTENABLE])
	{
		RGL.rgl_glEnable(RGL_ALPHA_TEST);
		RGL.rgl_glAlphaFunc(rs[D3DRS_ALPHAFUNC] ? (RGLenum)rs[D3DRS_ALPHAFUNC] : RGL_NEVER,
			(float)(rs[D3DRS_ALPHAREF] & 0xff) / 255.0f);
	}
	else
	{
		RGL.rgl_glDisable(RGL_ALPHA_TEST);
	}
	RGL.rgl_glColorMask((write & D3DCOLORWRITEENABLE_RED) != 0, (write & D3DCOLORWRITEENABLE_GREEN) != 0,
		(write & D3DCOLORWRITEENABLE_BLUE) != 0, (write & D3DCOLORWRITEENABLE_ALPHA) != 0);
	/* the cull mode names the winding to discard; FRONTFACE names the
	front winding */
	if (rs[D3DRS_CULLMODE] != D3DCULL_NONE)
	{
		RGL.rgl_glEnable(RGL_CULL_FACE);
		RGL.rgl_glFrontFace(rs[D3DRS_FRONTFACE] == D3DFRONT_CCW ? RGL_CCW : RGL_CW);
		RGL.rgl_glCullFace(rs[D3DRS_CULLMODE] == rs[D3DRS_FRONTFACE] ? RGL_FRONT : RGL_BACK);
	}
	else
	{
		RGL.rgl_glDisable(RGL_CULL_FACE);
	}
	if (rs[D3DRS_SOLIDOFFSETENABLE])
	{
		RGL.rgl_glEnable(RGL_POLYGON_OFFSET_FILL);
		RGL.rgl_glPolygonOffset(retro_dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]),
			retro_dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]));
	}
	else
	{
		RGL.rgl_glDisable(RGL_POLYGON_OFFSET_FILL);
	}
}

/* ---------- tracing (debug.gpu_trace_frame: every draw of that frame) */

static BOOL trace_frame(void)
{
	static long frame = -2;

	if (frame == -2)
		frame = config_integer("debug.gpu_trace_frame");
	return frame >= 0 && (unsigned long)frame == device.frame;
}

static void trace_draw(const char *kind, D3DPRIMITIVETYPE type, unsigned long count,
	const struct retro_pixel_setup *setup, const struct retro_vertex *vertex)
{
	DWORD *rs = D3D__RenderState;
	char textures[160];
	int unit;

	textures[0] = '\0';
	for (unit = 0; unit < setup->units; unit++)
	{
		const D3DBaseTexture *texture = retro_device_texture(setup->unit_stage[unit]);
		char one[48];

		snprintf(one, sizeof(one), " t%d=%08lx/%08lx/%08lx", setup->unit_stage[unit],
			texture ? (unsigned long)texture->Data : 0, texture ? (unsigned long)texture->Format : 0,
			texture ? (unsigned long)texture->Size : 0);
		strcat(textures, one);
	}
	platform_log("trace %s: program %lu, shader %08lx, %s %lu,%s; blend %lu %lx %lx, alpha test %lu %lx %lu, z %lu %lu, "
		"cull %lx, view %lu %lu %lu %lu; first at %.2f %.2f %.2f %.2f color %02x%02x%02x%02x uv %.3f %.3f",
		kind, device.fixed_function ? 0 : current_program()->id, retro_pixel_hash(),
		type == D3DPT_QUADLIST ? "quads" : type == D3DPT_TRIANGLESTRIP ? "strip" : type == D3DPT_TRIANGLEFAN ? "fan" :
		type == D3DPT_TRIANGLELIST ? "triangles" : "other", count, textures,
		(unsigned long)rs[D3DRS_ALPHABLENDENABLE], (unsigned long)rs[D3DRS_SRCBLEND], (unsigned long)rs[D3DRS_DESTBLEND],
		(unsigned long)rs[D3DRS_ALPHATESTENABLE], (unsigned long)rs[D3DRS_ALPHAFUNC], (unsigned long)rs[D3DRS_ALPHAREF],
		(unsigned long)rs[D3DRS_ZENABLE], (unsigned long)rs[D3DRS_ZWRITEENABLE], (unsigned long)rs[D3DRS_CULLMODE],
		(unsigned long)device.viewport.X, (unsigned long)device.viewport.Y, (unsigned long)device.viewport.Width,
		(unsigned long)device.viewport.Height, vertex->position[0], vertex->position[1], vertex->position[2],
		vertex->position[3], vertex->diffuse[0], vertex->diffuse[1], vertex->diffuse[2], vertex->diffuse[3],
		vertex->texture[0][0], vertex->texture[0][1]);
}

/* ---------- drawing */

static RGLenum primitive_mode(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
	case D3DPT_POINTLIST: return RGL_POINTS;
	case D3DPT_LINELIST: return RGL_LINES;
	case D3DPT_LINELOOP: return RGL_LINE_LOOP;
	case D3DPT_LINESTRIP: return RGL_LINE_STRIP;
	case D3DPT_TRIANGLESTRIP: return RGL_TRIANGLE_STRIP;
	case D3DPT_TRIANGLEFAN: return RGL_TRIANGLE_FAN;
	case D3DPT_QUADLIST: return RGL_QUADS;
	case D3DPT_QUADSTRIP: return RGL_QUAD_STRIP;
	case D3DPT_POLYGON: return RGL_POLYGON;
	default: return RGL_TRIANGLES;
	}
}

static BOOL vertices_reserve(unsigned long count)
{
	if (count > device.vertex_capacity)
	{
		unsigned long capacity = count + count / 2 + 64;
		struct retro_vertex *vertices = realloc(device.vertices, capacity * sizeof(*vertices));

		if (!vertices)
			return FALSE;
		device.vertices = vertices;
		device.vertex_capacity = capacity;
	}
	return TRUE;
}

static BOOL indices_reserve(unsigned long count)
{
	if (count > device.index_capacity)
	{
		unsigned long capacity = count + count / 2 + 64;
		WORD *indices = realloc(device.indices, capacity * sizeof(*indices));

		if (!indices)
			return FALSE;
		device.indices = indices;
		device.index_capacity = capacity;
	}
	return TRUE;
}

/* the draw's state, if it can be drawn at all */
static struct retro_vertex_program *draw_begin(BOOL immediate)
{
	struct retro_vertex_program *program = device.fixed_function ? NULL : current_program();

	retro_gl_check();
	if (!drawing_to_screen() || (device.fixed_function ? !immediate : !program || !program->instruction_count))
	{
		if (trace_frame())
			platform_log("trace: left out (%s)", !drawing_to_screen() ? "another target" : "no program");
		statistics.skipped++;
		return NULL;
	}
	statistics.draws++;
	return program ? program : (struct retro_vertex_program *)1;
}

static void vertex_state(struct retro_vertex_state *state)
{
	state->constants = (const float (*)[4])device.constants;
	memcpy(state->viewport_scale, device.viewport_scale, sizeof(state->viewport_scale));
	memcpy(state->viewport_offset, device.viewport_offset, sizeof(state->viewport_offset));
	state->screen_offset = 0.0f;
}

/* vertices [first, first + count) of the streams, run through the program
into device.vertices */
static BOOL vertices_from_streams(const struct retro_vertex_program *program, unsigned long first, unsigned long count)
{
	Uint64 started = SDL_GetTicksNS();
	struct retro_vertex_state state;
	float inputs[RETRO_ATTRIBUTE_COUNT][4];
	const unsigned char *bases[RETRO_ATTRIBUTE_COUNT];
	unsigned long strides[RETRO_ATTRIBUTE_COUNT];
	unsigned long vertex, element;

	if (!vertices_reserve(count))
		return FALSE;
	vertex_state(&state);
	memcpy(inputs, device.attributes, sizeof(inputs));
	for (element = 0; element < program->element_count; element++)
	{
		const struct retro_vertex_element *declared = &program->elements[element];
		DWORD data = device.streams[declared->stream].data;

		bases[element] = data ? (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(data) + declared->offset : NULL;
		strides[element] = device.streams[declared->stream].stride;
	}
	for (vertex = 0; vertex < count; vertex++)
	{
		for (element = 0; element < program->element_count; element++)
		{
			if (bases[element])
			{
				retro_vertex_fetch(&program->elements[element], bases[element] + (first + vertex) * strides[element],
					inputs[program->elements[element].reg]);
			}
		}
		retro_vertex_run(program, &state, (const float (*)[4])inputs, &device.vertices[vertex]);
	}
	statistics.vertices += count;
	statistics.vertex_time += SDL_GetTicksNS() - started;
	return TRUE;
}

/* draws device.vertices: by indices (into them), or all of them in order */
static void vertices_draw(D3DPRIMITIVETYPE type, unsigned long vertex_count, const WORD *indices,
	unsigned long index_count)
{
	struct retro_pixel_setup setup;
	struct retro_vertex *vertices = device.vertices;
	int unit;
	Uint64 started = SDL_GetTicksNS(), prepared;

	raster_state_apply();
	retro_pixel_prepare(&setup, vertex_count ? &vertices[0] : NULL);
	retro_pixel_vertex_colors(vertices, vertex_count);
	prepared = SDL_GetTicksNS();
	statistics.pixel_time += prepared - started;
	if (trace_frame())
		trace_draw(indices ? "indexed" : "draw", type, indices ? index_count : vertex_count, &setup, vertices);
	RGL.rgl_glVertexPointer(4, RGL_FLOAT, sizeof(*vertices), vertices->position);
	if (setup.diffuse)
	{
		RGL.rgl_glEnableClientState(RGL_COLOR_ARRAY);
		RGL.rgl_glColorPointer(4, RGL_UNSIGNED_BYTE, sizeof(*vertices), vertices->diffuse);
	}
	else
	{
		RGL.rgl_glDisableClientState(RGL_COLOR_ARRAY);
		RGL.rgl_glColor4ub(255, 255, 255, 255);
	}
	for (unit = 0; unit < retro_gl_capabilities.texture_units; unit++)
	{
		int stage = setup.unit_stage[unit];

		if (RGL.rgl_glClientActiveTextureARB)
			RGL.rgl_glClientActiveTextureARB(RGL_TEXTURE0 + unit);
		if (stage < 0)
		{
			RGL.rgl_glDisableClientState(RGL_TEXTURE_COORD_ARRAY);
			continue;
		}
		/* (a linear texture's coordinates are in texels: scaled in place,
		each vertex's for this unit's stage) */
		if (setup.texture_scale[unit][0] != 1.0f || setup.texture_scale[unit][1] != 1.0f)
		{
			unsigned long vertex;

			for (vertex = 0; vertex < vertex_count; vertex++)
			{
				vertices[vertex].texture[stage][0] *= setup.texture_scale[unit][0];
				vertices[vertex].texture[stage][1] *= setup.texture_scale[unit][1];
			}
		}
		RGL.rgl_glEnableClientState(RGL_TEXTURE_COORD_ARRAY);
		RGL.rgl_glTexCoordPointer(4, RGL_FLOAT, sizeof(*vertices), vertices->texture[stage]);
	}
	if (RGL.rgl_glClientActiveTextureARB)
		RGL.rgl_glClientActiveTextureARB(RGL_TEXTURE0);
	if (indices)
		RGL.rgl_glDrawElements(primitive_mode(type), (RGLsizei)index_count, RGL_UNSIGNED_SHORT, indices);
	else
		RGL.rgl_glDrawArrays(primitive_mode(type), 0, (RGLsizei)vertex_count);
	statistics.submit_time += SDL_GetTicksNS() - prepared;
}

void WINAPI D3DDevice_SetStreamSource(UINT stream_number, D3DVertexBuffer *stream_data, UINT stride)
{
	if (stream_number >= 16)
		return;
	device.streams[stream_number].data = stream_data ? stream_data->Data : 0;
	device.streams[stream_number].stride = stride;
}

void WINAPI D3DDevice_SetIndices(D3DIndexBuffer *index_data, UINT base_vertex_index)
{
	device.base_vertex_index = base_vertex_index;
	D3D__IndexData = index_data ? (WORD *)index_data->Data : NULL;
}

void WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT vertex_count)
{
	struct retro_vertex_program *program;

	if (!vertex_count || !(program = draw_begin(FALSE)))
		return;
	if (vertices_from_streams(program, start_vertex, vertex_count))
		vertices_draw(primitive_type, vertex_count, NULL, 0);
}

void WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE primitive_type, UINT vertex_count, CONST WORD *index_data)
{
	struct retro_vertex_program *program;
	unsigned long minimum = 0xffff, maximum = 0, index;

	if (!vertex_count || !index_data || !(program = draw_begin(FALSE)))
		return;
	for (index = 0; index < vertex_count; index++)
	{
		if (index_data[index] < minimum)
			minimum = index_data[index];
		if (index_data[index] > maximum)
			maximum = index_data[index];
	}
	if (!indices_reserve(vertex_count))
		return;
	/* (the vertices the indices reach, from the base vertex on: index i is
	vertex base + i) */
	for (index = 0; index < vertex_count; index++)
		device.indices[index] = (WORD)(index_data[index] - minimum);
	if (vertices_from_streams(program, device.base_vertex_index + minimum, maximum - minimum + 1))
		vertices_draw(primitive_type, maximum - minimum + 1, device.indices, vertex_count);
}

/* ---------- immediate mode */

void WINAPI D3DDevice_Begin(D3DPRIMITIVETYPE primitive_type)
{
	device.immediate_active = TRUE;
	device.immediate_type = primitive_type;
	device.immediate_count = 0;
}

static void immediate_emit(void)
{
	unsigned long floats = RETRO_ATTRIBUTE_COUNT * 4;

	if (device.immediate_count == device.immediate_capacity)
	{
		unsigned long capacity = device.immediate_capacity ? device.immediate_capacity * 2 : 256;
		float *vertices = realloc(device.immediate_vertices, capacity * floats * sizeof(float));

		if (!vertices)
			return;
		device.immediate_vertices = vertices;
		device.immediate_capacity = capacity;
	}
	memcpy(device.immediate_vertices + device.immediate_count * floats, device.attributes, floats * sizeof(float));
	device.immediate_count++;
}

/* the fixed-function pipeline's vertex (the loading screen's): its position
times the world, view and projection matrices, Direct3D's row vectors */
static void fixed_function_vertex(const float (*inputs)[4], struct retro_vertex *out)
{
	static const D3DTRANSFORMSTATETYPE order[3] = { D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION };
	float position[4];
	int which, column, stage;

	memcpy(position, inputs[0], sizeof(position));
	for (which = 0; which < 3; which++)
	{
		const D3DMATRIX *matrix = &device.transforms[order[which]];
		float in[4];

		memcpy(in, position, sizeof(in));
		for (column = 0; column < 4; column++)
		{
			position[column] = in[0] * matrix->m[0][column] + in[1] * matrix->m[1][column] +
				in[2] * matrix->m[2][column] + in[3] * matrix->m[3][column];
		}
	}
	memcpy(out->position, position, sizeof(position));
	out->position[2] = 2.0f * position[2] - position[3];
	for (column = 0; column < 4; column++)
	{
		float diffuse = inputs[3][column], specular = inputs[4][column];

		out->diffuse[column] = (unsigned char)((diffuse < 0.0f ? 0.0f : diffuse > 1.0f ? 1.0f : diffuse) * 255.0f + 0.5f);
		out->specular[column] = (unsigned char)((specular < 0.0f ? 0.0f : specular > 1.0f ? 1.0f : specular) * 255.0f + 0.5f);
	}
	for (stage = 0; stage < 4; stage++)
		memcpy(out->texture[stage], inputs[9 + stage], sizeof(out->texture[0]));
	out->fog = 1.0f;
}

void WINAPI D3DDevice_End(void)
{
	struct retro_vertex_program *program;
	struct retro_vertex_state state;
	unsigned long index, count = device.immediate_count;

	device.immediate_active = FALSE;
	if (!count || !(program = draw_begin(TRUE)) || !vertices_reserve(count))
		return;
	vertex_state(&state);
	for (index = 0; index < count; index++)
	{
		const float (*inputs)[4] = (const float (*)[4])(device.immediate_vertices + index * RETRO_ATTRIBUTE_COUNT * 4);

		if (device.fixed_function)
			fixed_function_vertex(inputs, &device.vertices[index]);
		else
			retro_vertex_run(program, &state, inputs, &device.vertices[index]);
	}
	statistics.vertices += count;
	vertices_draw(device.immediate_type, count, NULL, 0);
}

static void set_attribute(INT reg, float a, float b, float c, float d)
{
	BOOL emit = FALSE;

	if (reg == D3DVSDE_VERTEX)
	{
		reg = 0;
		emit = TRUE;
	}
	if (reg < 0 || reg >= RETRO_ATTRIBUTE_COUNT)
		return;
	device.attributes[reg][0] = a;
	device.attributes[reg][1] = b;
	device.attributes[reg][2] = c;
	device.attributes[reg][3] = d;
	/* like the hardware, writing register 0 completes a vertex */
	if (device.immediate_active && (emit || reg == 0))
		immediate_emit();
}

void WINAPI D3DDevice_SetVertexData2f(INT reg, FLOAT a, FLOAT b)
{
	set_attribute(reg, a, b, 0.0f, 1.0f);
}

void WINAPI D3DDevice_SetVertexData4f(INT reg, FLOAT a, FLOAT b, FLOAT c, FLOAT d)
{
	set_attribute(reg, a, b, c, d);
}

void WINAPI D3DDevice_SetVertexData2s(INT reg, SHORT a, SHORT b)
{
	set_attribute(reg, (float)a, (float)b, 0.0f, 1.0f);
}

void WINAPI D3DDevice_SetVertexData4ub(INT reg, BYTE a, BYTE b, BYTE c, BYTE d)
{
	set_attribute(reg, a / 255.0f, b / 255.0f, c / 255.0f, d / 255.0f);
}

void WINAPI D3DDevice_SetVertexDataColor(INT reg, D3DCOLOR color)
{
	float value[4];

	retro_color_to_floats(color, value);
	set_attribute(reg, value[0], value[1], value[2], value[3]);
}

/* ---------- clearing */

void WINAPI D3DDevice_Clear(DWORD count, CONST D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	float rgba[4];
	RGLbitfield mask = 0;
	RGLint scissor[4];
	DWORD index;

	if (trace_frame())
		platform_log("trace clear: flags %lx color %08lx, %lu rectangles, target %08lx", (unsigned long)flags,
			(unsigned long)color, (unsigned long)count, device.render_target ? (unsigned long)device.render_target->Data : 0);
	if (!drawing_to_screen())
		return;
	retro_color_to_floats(color, rgba);
	if (flags & D3DCLEAR_TARGET)
	{
		RGL.rgl_glColorMask((flags & D3DCLEAR_TARGET_R) != 0, (flags & D3DCLEAR_TARGET_G) != 0,
			(flags & D3DCLEAR_TARGET_B) != 0, (flags & D3DCLEAR_TARGET_A) != 0);
		RGL.rgl_glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
		mask |= RGL_COLOR_BUFFER_BIT;
	}
	if (device.depth_stencil && (flags & D3DCLEAR_ZBUFFER))
	{
		RGL.rgl_glDepthMask(RGL_TRUE);
		RGL.rgl_glClearDepth(z);
		mask |= RGL_DEPTH_BUFFER_BIT;
	}
	if (device.depth_stencil && (flags & D3DCLEAR_STENCIL) && retro_gl_capabilities.stencil_bits)
	{
		RGL.rgl_glStencilMask(0xff);
		RGL.rgl_glClearStencil((RGLint)stencil);
		mask |= RGL_STENCIL_BUFFER_BIT;
	}
	if (!mask)
		return;
	RGL.rgl_glEnable(RGL_SCISSOR_TEST);
	if (!count || !rectangles)
	{
		/* (the NV2A clips a clear to the viewport: a split screen's window) */
		screen_rectangle((long)device.viewport.X, (long)device.viewport.Y, (long)device.viewport.Width,
			(long)device.viewport.Height, scissor);
		RGL.rgl_glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
		RGL.rgl_glClear(mask);
		return;
	}
	for (index = 0; index < count; index++)
	{
		long left = rectangles[index].x1 > (long)device.viewport.X ? rectangles[index].x1 : (long)device.viewport.X;
		long top = rectangles[index].y1 > (long)device.viewport.Y ? rectangles[index].y1 : (long)device.viewport.Y;
		long right = rectangles[index].x2 < (long)(device.viewport.X + device.viewport.Width) ?
			rectangles[index].x2 : (long)(device.viewport.X + device.viewport.Width);
		long bottom = rectangles[index].y2 < (long)(device.viewport.Y + device.viewport.Height) ?
			rectangles[index].y2 : (long)(device.viewport.Y + device.viewport.Height);

		if (left >= right || top >= bottom)
			continue;
		screen_rectangle(left, top, right - left, bottom - top, scissor);
		RGL.rgl_glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
		RGL.rgl_glClear(mask);
	}
}

/* ---------- presentation */

void WINAPI D3DDevice_Present(CONST RECT *source_rectangle, CONST RECT *destination_rectangle,
	void *unused, void *unused2)
{
	(void)source_rectangle;
	(void)destination_rectangle;
	(void)unused;
	(void)unused2;
	if (device.gl_ready)
	{
		Uint64 swap_started = SDL_GetTicksNS(), now;

		platform_video_swap();
		now = SDL_GetTicksNS();
		statistics.swap_time += now - swap_started;
		if (statistics.frame_start)
		{
			Uint64 frame = now - statistics.frame_start;

			statistics.frames_time += frame;
			if (frame > statistics.slowest)
				statistics.slowest = frame;
			statistics.frames++;
		}
		statistics.frame_start = now;
		if (!statistics.logged)
			statistics.logged = now;
		/* (the window's letterbox bars, black, before the next frame draws
		inside them) */
		screen_area_update();
		RGL.rgl_glDisable(RGL_SCISSOR_TEST);
		RGL.rgl_glColorMask(RGL_TRUE, RGL_TRUE, RGL_TRUE, RGL_TRUE);
		RGL.rgl_glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		RGL.rgl_glClear(RGL_COLOR_BUFFER_BIT);
		/* (every 10 seconds) */
		if (statistics.frames && now - statistics.logged >= 10000000000ULL)
		{
			unsigned long frames = statistics.frames;
			unsigned long frame_ms = (unsigned long)(statistics.frames_time / frames / 1000000);

			platform_log("Direct3D: %lu draws (%lu left out), %lu vertices a frame", statistics.draws / frames,
				statistics.skipped / frames, statistics.vertices / frames);
			platform_log("Direct3D: %lu.%lu frames a second; a frame %lu ms (the slowest %lu): vertex programs %lu, "
				"pixel shaders and textures %lu, OpenGL %lu, swap %lu, the game the rest",
				(unsigned long)(frames * 10000000000ULL / statistics.frames_time) / 10,
				(unsigned long)(frames * 10000000000ULL / statistics.frames_time) % 10, frame_ms,
				(unsigned long)(statistics.slowest / 1000000),
				(unsigned long)(statistics.vertex_time / frames / 1000000),
				(unsigned long)(statistics.pixel_time / frames / 1000000),
				(unsigned long)(statistics.submit_time / frames / 1000000),
				(unsigned long)(statistics.swap_time / frames / 1000000));
			statistics.draws = statistics.skipped = statistics.vertices = statistics.frames = 0;
			statistics.vertex_time = statistics.pixel_time = statistics.submit_time = statistics.swap_time = 0;
			statistics.frames_time = statistics.slowest = 0;
			statistics.logged = now;
		}
	}
	device.frame++;
	retro_textures_begin_frame(device.frame);
	platform_pump_events();

	vertical_blank_start();
	if (vertical_blank_started)
	{
		SDL_LockMutex(vertical_blank_lock);
		/* the Xbox keeps at most two frames queued behind its 60 Hz display */
		while (pending_flips >= 2)
			SDL_WaitConditionTimeout(vertical_blank_condition, vertical_blank_lock, 100);
		pending_flips++;
		SDL_UnlockMutex(vertical_blank_lock);
	}
}

/* ---------- the menus' pointer (as d3d8_gl.c, through the letterbox) */

static void ui_point_from_window(float window_x, float window_y, short *x, short *y)
{
	int window_width = 0, window_height = 0, pixel_width = 0, pixel_height = 0;
	float pixel_x, pixel_y;

	*x = *y = -1;
	platform_video_window_size(&window_width, &window_height);
	platform_video_drawable_size(&pixel_width, &pixel_height);
	if (window_width <= 0 || window_height <= 0 || device.area[2] <= 0 || device.area[3] <= 0)
		return;
	pixel_x = window_x * pixel_width / window_width;
	pixel_y = window_y * pixel_height / window_height;
	/* (the area is from the bottom; the pointer from the top) */
	*x = (short)floorf((pixel_x - device.area[0]) * SCREEN_WIDTH / device.area[2]);
	*y = (short)floorf((pixel_y - (pixel_height - device.area[1] - device.area[3])) * SCREEN_HEIGHT / device.area[3]);
}

int halo_scoreboard_pointer_update(int offered, struct halo_ui_pointer *pointer)
{
	struct platform_ui_pointer state;

	memset(pointer, 0, sizeof(*pointer));
	if (!platform_scoreboard_pointer(offered != 0, &state) || !device.gl_ready)
		return 0;
	ui_point_from_window(state.x, state.y, &pointer->x, &pointer->y);
	ui_point_from_window(state.click_x, state.click_y, &pointer->click_x, &pointer->click_y);
	pointer->moved = state.moved != FALSE;
	pointer->left_clicks = (unsigned char)(state.left_clicks < 255 ? state.left_clicks : 255);
	return 1;
}

int halo_ui_pointer_update(int menus_active, struct halo_ui_pointer *pointer)
{
	struct platform_ui_pointer state;

	platform_menus_set_active(menus_active != 0);
	platform_ui_pointer_set_active(menus_active != 0);
	if (!menus_active || !device.gl_ready || !platform_ui_pointer_read(&state))
		return 0;
	memset(pointer, 0, sizeof(*pointer));
	ui_point_from_window(state.x, state.y, &pointer->x, &pointer->y);
	ui_point_from_window(state.click_x, state.click_y, &pointer->click_x, &pointer->click_y);
	pointer->moved = state.moved != FALSE;
	pointer->left_clicks = (unsigned char)(state.left_clicks < 255 ? state.left_clicks : 255);
	pointer->right_clicks = (unsigned char)(state.right_clicks < 255 ? state.right_clicks : 255);
	pointer->wheel_steps = (signed char)(state.wheel_steps < -8 ? -8 : state.wheel_steps > 8 ? 8 : state.wheel_steps);
	pointer->touch = state.touch != FALSE;
	pointer->downs = (unsigned char)(state.downs < 255 ? state.downs : 255);
	ui_point_from_window(state.down_x, state.down_y, &pointer->down_x, &pointer->down_y);
	return 1;
}
