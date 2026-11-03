/*
TAG_RELOCATE.H

A map's tags moved to another address than the one they are linked to
(tag_relocate.c).
*/

#ifndef __TAG_RELOCATE_H
#define __TAG_RELOCATE_H
#pragma once

/* ---------- structures */

/* a tag cache's bytes, as they are in memory at cache, but linked to
link_address: its size bytes hold the tags and the structure bsp, whose
pointers point into them at their linked addresses */
struct tag_relocation
{
	byte *cache;
	unsigned long link_address;
	unsigned long size;
	/* what every pointer into the tag cache is moved by */
	unsigned long delta;
	/* a bit a byte of the tag cache: where a pointer moved starts (none
	is moved twice); the caller's, cleared, of size/8 bytes */
	unsigned long *marks;
	/* (optional, for checks: a bit a byte of the tag cache, set where a tag's
	root or a block's element is) */
	unsigned long *elements;
	long moved;
	boolean failed;
};

/* ---------- prototypes */

/* the pointers of the tags at the start of the tag cache, moved by delta:
FALSE if the tags could not be walked (they are left part moved: the
validator refuses them) */
boolean tag_relocate_tags(struct tag_relocation *relocation, long tag_data_size);

/* the pointers of the structure bsp in the tag cache at its linked address
link_address (its header's), of size bytes, moved by delta */
boolean tag_relocate_structure_bsp(struct tag_relocation *relocation, unsigned long link_address, long size);

/* whether a pointer starting at the tag cache's byte offset was moved */
boolean tag_relocate_marked(struct tag_relocation const *relocation, unsigned long offset);

/* whether the tag cache's byte offset is in a tag's root or a block's element
(with elements given) */
boolean tag_relocate_in_element(struct tag_relocation const *relocation, unsigned long offset);

#endif
