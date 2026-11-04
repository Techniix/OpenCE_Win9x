/*
DETAIL_NOTICE.H

A line of text at the top left of the screen for a moment (the performance
level's changes: render_policy.c), like the console line of a game of the
era: "Detail: level 3 of 6".
*/

#ifndef DETAIL_NOTICE_H
#define DETAIL_NOTICE_H

void detail_notice_set(char const *text);
/* each frame, with the overlays (interface_draw_fullscreen_overlays) */
void detail_notice_draw(void);

#endif
