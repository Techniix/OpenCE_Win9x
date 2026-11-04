/*
RENDER_POLICY.H

What the platform layer calls of the render policy (render_policy.c): the
hot keys' steps of the performance level.
*/

#ifndef RENDER_POLICY_H
#define RENDER_POLICY_H

/* Ctrl+Minus (delta 1) and Ctrl+Equals (delta -1) in a game: a step less or
more detail. Not with render.hotkeys false. */
void halo_render_level_adjust(int delta);

#endif
