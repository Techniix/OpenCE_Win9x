/*
TAG_RELOCATE.C

A map's tags moved to another address than the one they are linked to.

The game reads a map's tags into the tag cache at the address the map was
linked to (0x803A6000, physical_memory_map.c) and uses them as its
structures, pointers and all. Where that address cannot be had (the Windows
9x build: the Xbox's memory window is in Windows 9x's shared arena there,
port/win98/README.md), the tags are read elsewhere and every pointer into
the tag cache is moved by the same delta, before the validator checks them
(tag_validate.c).

The pointers are found as the validator finds them, through the tag groups'
schemas (tag_schema.h), which list every block, data and reference of every
tag, and in the places the schemas do not list: the tags' header and table,
the vertex and index buffers' data, a scenario's structure bsp references'
load addresses, and a structure bsp's header. The tags are not trusted yet:
a pointer is followed only where its bytes lie in the tag cache, none is
moved twice, and the walk goes no deeper than the validator's.

tools/map_validate.c --relocation checks it: every map is read and checked
where it is linked and, relocated, elsewhere, and the two must differ in the
pointers moved only.
*/

/* ---------- headers */

#include "cseries.h"
#include "tag_schema.h"
#include "tag_relocate.h"

#include <string.h>

/* ---------- constants */

enum
{
	TAG_HEADER_SIGNATURE = 'tags',
	STRUCTURE_BSP_HEADER_SIGNATURE = 'sbsp',
	SCENARIO_GROUP_TAG = 'scnr',
	STRUCTURE_BSP_GROUP_TAG = 'sbsp',
	/* a vertex or index buffer (a D3DVertexBuffer or D3DIndexBuffer), and
	where its Data is in it */
	BUFFER_SIZE = 12,
	BUFFER_DATA_OFFSET = 4,
	/* a scenario's structure bsp references (scenario_definitions.h: struct
	scenario's structure_bsp_references, struct
	scenario_structure_bsp_reference's base_address) */
	SCENARIO_STRUCTURE_BSPS_OFFSET = 0x5A4,
	STRUCTURE_BSP_REFERENCE_SIZE = 0x20,
	STRUCTURE_BSP_REFERENCE_BASE_ADDRESS_OFFSET = 0x8,
	/* as deep as the validator walks (tag_validate.c) */
	MAXIMUM_RELOCATION_DEPTH = 32,
	MARK_BITS = 32,
};

/* ---------- structures */

/* the tags' header and table (cache_files.c's struct cache_file_tag_header
and struct cache_file_tag_instance) */
struct relocate_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	long tag_index;
	unsigned long name;
	unsigned long base_address;
	unsigned long unused[2];
};

struct relocate_header
{
	unsigned long instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	unsigned long vertex_buffers;
	long index_buffer_count;
	unsigned long index_buffers;
	unsigned long signature;
};

/* a structure bsp's header (cache_files.c's struct
cache_file_structure_bsp_header) */
struct relocate_structure_bsp_header
{
	unsigned long base_address;
	long vertex_buffer_count;
	unsigned long vertex_buffers;
	long index_buffer_count;
	unsigned long index_buffers;
	unsigned long signature;
};

typedef char verify_relocate_instance_size[sizeof(struct relocate_instance) == 0x20 ? 1 : -1];
typedef char verify_relocate_header_size[sizeof(struct relocate_header) == 0x24 ? 1 : -1];
typedef char verify_relocate_structure_bsp_header_size[sizeof(struct relocate_structure_bsp_header) == 0x18 ? 1 : -1];

/* ---------- prototypes */

static void relocate_element(struct tag_relocation *relocation, byte *base,
	struct tag_schema_definition const *definition, short depth);

/* ---------- private code */

/* the bytes at a linked address, if size bytes of them lie in the tag
cache; else NULL */
static byte *relocation_bytes(
	struct tag_relocation const *relocation,
	unsigned long address,
	unsigned long size)
{
	unsigned long offset = address - relocation->link_address;

	if (offset >= relocation->size || size > relocation->size - offset)
		return NULL;

	return relocation->cache + offset;
}

static boolean marked(
	struct tag_relocation const *relocation,
	unsigned long offset)
{
	return (relocation->marks[offset / MARK_BITS] >> (offset % MARK_BITS)) & 1;
}

/* the pointer at field: its linked value (to follow) in *linked, and moved
if it points into the tag cache. FALSE if it was moved before (not to be
followed again) or is not in the tag cache's bytes */
static boolean pointer_move(
	struct tag_relocation *relocation,
	byte *field,
	unsigned long *linked)
{
	unsigned long offset = (unsigned long)(field - relocation->cache);
	unsigned long value;

	*linked = 0;
	if (field < relocation->cache || offset >= relocation->size || relocation->size - offset < sizeof(value))
	{
		relocation->failed = TRUE;
		return FALSE;
	}
	if (marked(relocation, offset))
		return FALSE;
	memcpy(&value, field, sizeof(value));
	*linked = value;
	if (value - relocation->link_address < relocation->size)
	{
		value += relocation->delta;
		memcpy(field, &value, sizeof(value));
		relocation->marks[offset / MARK_BITS] |= 1UL << (offset % MARK_BITS);
		relocation->moved++;
	}

	return TRUE;
}

static struct tag_schema_group const *schema_group_get(
	unsigned long group_tag)
{
	struct tag_schema_group const *const *list;

	for (list = tag_schema_group_lists; *list; list++)
	{
		struct tag_schema_group const *group;

		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group;
		}
	}

	return NULL;
}

/* an element's fields (those of the structures in it too), and its blocks'
elements */
static void relocate_fields(
	struct tag_relocation *relocation,
	byte *base,
	struct tag_schema_definition const *definition,
	short depth)
{
	struct tag_schema_field const *field;

	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		short index;

		for (index = 0; index < field->count; index++)
		{
			byte *address = base + field->offset + index * field->size;
			unsigned long linked;

			switch (field->type)
			{
			case _tag_schema_struct:
				relocate_fields(relocation, address, field->definition, depth);
				break;

			case _tag_schema_block:
			{
				struct tag_schema_definition const *element = field->definition;
				long count;
				byte *elements;
				long element_index;

				memcpy(&count, address + offsetof(struct tag_block, count), sizeof(count));
				if (!pointer_move(relocation, address + offsetof(struct tag_block, address), &linked) ||
					count <= 0 || (unsigned long)count > relocation->size / (unsigned long)element->size)
				{
					break;
				}
				elements = relocation_bytes(relocation, linked, (unsigned long)count * element->size);
				for (element_index = 0; elements && element_index < count; element_index++)
					relocate_element(relocation, elements + element_index * element->size, element, depth + 1);
				break;
			}

			case _tag_schema_data:
				pointer_move(relocation, address + offsetof(struct tag_data, address), &linked);
				break;

			case _tag_schema_reference:
				pointer_move(relocation, address + offsetof(struct tag_reference, name), &linked);
				break;
			}
		}
	}

	return;
}

static void relocate_element(
	struct tag_relocation *relocation,
	byte *base,
	struct tag_schema_definition const *definition,
	short depth)
{
	if (depth >= MAXIMUM_RELOCATION_DEPTH)
	{
		relocation->failed = TRUE;
		return;
	}
	relocate_fields(relocation, base, definition, depth);

	return;
}

/* the Data of each of count buffers, BUFFER_SIZE bytes each, at buffers */
static void relocate_buffers(
	struct tag_relocation *relocation,
	byte *field,
	long count)
{
	unsigned long linked;
	byte *buffers;
	long index;

	if (!pointer_move(relocation, field, &linked) || count <= 0 ||
		!(buffers = relocation_bytes(relocation, linked, (unsigned long)count * BUFFER_SIZE)))
	{
		return;
	}
	for (index = 0; index < count; index++)
		pointer_move(relocation, buffers + index * BUFFER_SIZE + BUFFER_DATA_OFFSET, &linked);

	return;
}

/* a scenario's structure bsp references' load addresses, which its schema
does not list */
static void relocate_structure_bsp_references(
	struct tag_relocation *relocation,
	byte *scenario)
{
	byte *block = scenario + SCENARIO_STRUCTURE_BSPS_OFFSET;
	long count;
	unsigned long address;
	byte *references;
	long index;

	memcpy(&count, block + offsetof(struct tag_block, count), sizeof(count));
	/* (the block's address is the schema's, moved already if it points
	into the tag cache) */
	memcpy(&address, block + offsetof(struct tag_block, address), sizeof(address));
	if (marked(relocation, (unsigned long)(block + offsetof(struct tag_block, address) - relocation->cache)))
		address -= relocation->delta;
	if (count <= 0 || (unsigned long)count > relocation->size / STRUCTURE_BSP_REFERENCE_SIZE ||
		!(references = relocation_bytes(relocation, address, (unsigned long)count * STRUCTURE_BSP_REFERENCE_SIZE)))
	{
		return;
	}
	for (index = 0; index < count; index++)
	{
		unsigned long linked;

		pointer_move(relocation, references + index * STRUCTURE_BSP_REFERENCE_SIZE +
			STRUCTURE_BSP_REFERENCE_BASE_ADDRESS_OFFSET, &linked);
	}

	return;
}

/* ---------- public code */

boolean tag_relocate_tags(
	struct tag_relocation *relocation,
	long tag_data_size)
{
	struct relocate_header *header = (struct relocate_header *)relocation->cache;
	long tag_count = header->tag_count;
	unsigned long linked;
	byte *instances;
	long index;

	relocation->moved = 0;
	relocation->failed = FALSE;
	if (tag_data_size < (long)sizeof(*header) || (unsigned long)tag_data_size > relocation->size ||
		header->signature != TAG_HEADER_SIGNATURE || tag_count <= 0 || tag_count > UNSIGNED_SHORT_MAX)
	{
		return FALSE;
	}
	relocate_buffers(relocation, (byte *)&header->vertex_buffers, header->vertex_buffer_count);
	relocate_buffers(relocation, (byte *)&header->index_buffers, header->index_buffer_count);
	if (!pointer_move(relocation, (byte *)&header->instances, &linked) ||
		!(instances = relocation_bytes(relocation, linked, (unsigned long)tag_count * sizeof(struct relocate_instance))))
	{
		return FALSE;
	}

	for (index = 0; index < tag_count; index++)
	{
		struct relocate_instance *instance = (struct relocate_instance *)instances + index;
		struct tag_schema_group const *group = schema_group_get(instance->group_tag);
		byte *root;

		pointer_move(relocation, (byte *)&instance->name, &linked);
		/* (a structure bsp's root is where it loads, set as it does) */
		if (instance->group_tag == STRUCTURE_BSP_GROUP_TAG ||
			!pointer_move(relocation, (byte *)&instance->base_address, &linked) ||
			!group || !group->definition ||
			!(root = relocation_bytes(relocation, linked, (unsigned long)group->definition->size)))
		{
			continue;
		}
		relocate_element(relocation, root, group->definition, 0);
		if (instance->group_tag == SCENARIO_GROUP_TAG)
			relocate_structure_bsp_references(relocation, root);
	}

	return !relocation->failed;
}

boolean tag_relocate_structure_bsp(
	struct tag_relocation *relocation,
	unsigned long link_address,
	long size)
{
	struct relocate_structure_bsp_header *header;
	struct tag_schema_group const *group = schema_group_get(STRUCTURE_BSP_GROUP_TAG);
	unsigned long linked;
	byte *root;

	relocation->failed = FALSE;
	if (size < (long)sizeof(*header) ||
		!(header = (struct relocate_structure_bsp_header *)relocation_bytes(relocation, link_address, (unsigned long)size)) ||
		header->signature != STRUCTURE_BSP_HEADER_SIGNATURE)
	{
		return FALSE;
	}
	relocate_buffers(relocation, (byte *)&header->vertex_buffers, header->vertex_buffer_count);
	relocate_buffers(relocation, (byte *)&header->index_buffers, header->index_buffer_count);
	if (pointer_move(relocation, (byte *)&header->base_address, &linked) && group && group->definition &&
		(root = relocation_bytes(relocation, linked, (unsigned long)group->definition->size)) != NULL)
	{
		relocate_element(relocation, root, group->definition, 0);
	}

	return !relocation->failed;
}

boolean tag_relocate_marked(
	struct tag_relocation const *relocation,
	unsigned long offset)
{
	return offset < relocation->size && marked(relocation, offset);
}
