/*
CUTSCENE_SKIP.C

game.skip_cutscenes, for a machine too slow to sit through the levels'
opening cutscenes. A cutscene can be skipped once its script says so
(cinematic_skip_start, which also saves the game): the player's Accept then
calls main_skip_cinematic, which reverts to that save and lets the script
carry on past the cutscene (player_control.c, main.c). This does that for the
player, as soon as the save is written.

Only a local game: network co-op has its votes (network_coop.c) and the rest
of a network game's machines would be left out of step. It does what a
player could, with the same script and state, so a level plays as it does
after a skip by hand. Only the first skippable moment of a cutscene is used,
so that a state that is still skippable after the revert cannot skip again
in a loop.
*/

#include "cseries.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "main/main.h"
#include "cutscene_skip.h"
#include "../src/port_config.h" /* port: port/linux/src/port_config.c */

/* ---------- code */

boolean cutscene_skip_due(
	void)
{
	/* true from the skip until the cinematic has ended */
	static boolean already_skipped = FALSE;
	boolean due = FALSE;

	if (!cinematic_in_progress())
	{
		already_skipped = FALSE;
	}
	else if (!already_skipped && config_boolean("game.skip_cutscenes") &&
		game_connection() == _game_connection_local && cinematic_can_be_skipped() && !main_saving_map())
	{
		already_skipped = TRUE;
		due = TRUE;
	}

	return due;
}
