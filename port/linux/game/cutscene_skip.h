/*
CUTSCENE_SKIP.H

game.skip_cutscenes: skip a level's cutscenes by pressing the skip for the
player (cutscene_skip.c).
*/

#ifndef __CUTSCENE_SKIP_H
#define __CUTSCENE_SKIP_H

/* ---------- prototypes */

/* once for each main loop pass: whether main.c should now skip the cutscene
as the player's Accept would (main_skip_cinematic) */
boolean cutscene_skip_due(
	void);

#endif
