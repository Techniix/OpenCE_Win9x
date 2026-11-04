/*
RENDER_POLICY.C

What the picture may leave out or draw cheaper, for the machines that cannot
draw all of it (render.skip_passes, render.lod_scale in port_config.c). It is
read by the render code alone (source/render/render.c, render_objects.c),
once for each pass or object, and nothing it does reaches the game's state or
the network: the passes it skips only draw. With its defaults (nothing
skipped, a scale of 1) the frame is the one without it.

Only the passes named in skippable_passes can be skipped: those that draw
over what the passes before them did, and keep nothing that a later pass
reads. A name that is not there is ignored, not an error.
*/

#include <string.h>

#include "cseries.h"
#include "../src/port_config.h" /* port: port/linux/src/port_config.c */

/* ---------- constants */

#define MINIMUM_LOD_SCALE 0.05
#define MAXIMUM_LOD_SCALE 4.0

/* the names halo_render_pass gives in render_window (source/render/render.c) */
static const char *const skippable_passes[] =
{
	"object_shadows",
	"lights_specular",
	"bsp_specular_lightmaps",
	"bsp_reflection_masks",
	"bsp_reflection_mirrors",
	"bsp_reflections",
	"bsp_fog",
	"detail_objects",
	"fog_screen",
	"decals_light",
	"decals_alpha_tested",
	"decals_primary",
	"decals_secondary",
	"decals_water",
};

/* ---------- private code */

static boolean pass_is_skippable(const char *name, size_t length)
{
	size_t index;

	for (index = 0; index < sizeof(skippable_passes) / sizeof(skippable_passes[0]); index++)
	{
		if (strlen(skippable_passes[index]) == length && !memcmp(skippable_passes[index], name, length))
			return TRUE;
	}
	return FALSE;
}

/* ---------- public code */

/* whether the pass named is to be drawn: FALSE if render.skip_passes (a
comma-separated list) has it and it may be skipped */
unsigned char halo_render_pass_enabled(const char *name)
{
	const char *list = config_string("render.skip_passes");
	size_t length = name ? strlen(name) : 0;

	if (!list || !*list || !length)
		return TRUE;
	while (*list)
	{
		const char *end;
		size_t token;

		while (*list == ',' || *list == ' ')
			list++;
		end = list;
		while (*end && *end != ',' && *end != ' ')
			end++;
		token = (size_t)(end - list);
		if (token == length && !memcmp(list, name, length) && pass_is_skippable(name, length))
			return FALSE;
		list = end;
	}
	return TRUE;
}

/* whether the clusters in view are the cluster's stored potentially visible
set (render.visibility = "pvs") rather than those the portals show: the
game's own structures_use_pvs_for_vs (structure_visibility.c), which draws
more clusters for a lighter traversal. Only the picture changes: the stored
set is read by the game's other systems too (AI, sound, objects), unchanged */
unsigned char halo_render_visibility_pvs(void)
{
	const char *mode = config_string("render.visibility");

	return mode && !strcmp(mode, "pvs");
}

/* the factor an object's size on the screen is multiplied by to choose its
level of detail (render_objects.c): below 1 the objects use the coarser levels
sooner */
float halo_render_lod_scale(void)
{
	double scale = config_real("render.lod_scale");

	if (!(scale >= MINIMUM_LOD_SCALE))
		return scale == 0.0 ? 1.0f : (float)MINIMUM_LOD_SCALE;
	return (float)(scale > MAXIMUM_LOD_SCALE ? MAXIMUM_LOD_SCALE : scale);
}
