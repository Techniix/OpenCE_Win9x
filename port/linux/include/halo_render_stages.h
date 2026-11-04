/*
HALO_RENDER_STAGES.H

The lists of passes render.skip_passes names in steps, shared by what chooses
among them: port/linux/game/render_policy.c (the performance level),
port/win98/launcher (the setup window) and, as text, Video Setup,
Performance (tools/port_settings.py _SKIP_*; launcher_test.py checks that
they agree). Each step takes the ones before it.
*/

#ifndef HALO_RENDER_STAGES_H
#define HALO_RENDER_STAGES_H

#define HALO_SKIP_SHINY \
	"lights_specular,bsp_specular_lightmaps,bsp_reflection_masks,bsp_reflection_mirrors,bsp_reflections"
#define HALO_SKIP_SOME HALO_SKIP_SHINY ",object_shadows,decals_light,decals_alpha_tested,detail_objects"
#define HALO_SKIP_MOST HALO_SKIP_SOME ",decals_primary,decals_secondary,decals_water,bsp_fog,fog_screen"

#endif
