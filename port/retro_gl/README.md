# retro_gl

The Xbox's Direct3D 8, which the game draws through, on OpenGL 1.1: the
cards of 1998 to 2001 (a 3dfx Voodoo3 first), for the Windows 9x build and
later the Mac OS 9 and DOS ones. It takes the place of the OpenGL 4.5
renderer's files (`port/linux/src/d3d8_gl.c` and the files listed in
`port/win98/build.py`'s `RETRO_GL_REPLACED`), implementing the same
functions, so the game's renderer (`source/rasterizer`) is unchanged on top.

| File | What |
| --- | --- |
| `src/retro_device.c` | The device: state, the screen (the Xbox's 640x480, letterboxed in the window), draws, clears, presentation, the 60 Hz vertical blank, the menus' pointer. |
| `src/retro_vertex.c` | The game's vertex programs (NV2A microcode) run on the processor, each decoded once; their positions handed to OpenGL in clip space. |
| `src/retro_pixel.c` | The pixel shaders (register combiners) on the texture units. |
| `src/retro_affine.c` | Finds the draws whose combiners are a scale and an offset of the vertex color, so that their vertices skip the combiners on the processor (checked by `tests/retro_affine_test.py`; each new shader is logged as "by a scale and an offset" or "by the combiners"). |
| `src/retro_textures.c` | Textures: the Xbox formats decoded, made a power of two each way and no larger than the card takes, 16 bits a texel, mip levels; the menus' PNG art. |
| `src/retro_gl.c`, `include/retro_gl.h` | OpenGL 1.1 and its extensions, looked up through SDL. |
| `src/retro_shim.c` | What the platform layer calls of the replaced files besides the device. |

What it does not do yet:

- The pixel shaders are approximate: the textures a shader reads are
  multiplied together and by the vertex color where it reads that; each new
  shader is logged (`pixel shader ...` in halo.log) so that the common ones
  can be mapped exactly.
- Only the window is drawn into: draws into other targets (shadow maps,
  screen effects, water reflections) are left out.
- No fog, no visibility tests (lens flares always show), no wide screen, no
  high-res HUD or text (their textures are larger than these cards hold).
