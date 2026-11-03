/*
MAP_VALIDATE.C

The tag validator (port/linux/game/tag_validate.c) run alone on map files,
as the game runs it when a map loads: each map's tag data is read (and
inflated) to where the game reads it, its tags checked, then each of its
structure bsps where it loads. Prints every correction and refusal, then a
line for each map:
	<map>: <corrections> corrections
	<map>: refused
and exits 1 if any map was refused (or, with --strict, corrected: the retail
maps need no correction, which tools/test_linux_port.py checks).

Built by ninja linux as build/linux/map_validate, with the platform layer's
POSIX flags (it calls the validator's few functions, whose arguments are
pointers and longs alike in both ABIs).

With --fuzz, each map is instead checked iterations times with a few of its
words changed at random (from --seed on): the validator must never crash or
hang, and a map it lets through must be clean when checked again.

With --log, each run that was corrected is written to a file as the map's
name, the seed and the changed words (offset:value in the inflated file).

A Halo Custom Edition map is checked as the game checks one
(port/linux/game/custom_edition_cache.c): its loader reads it, with the
resource maps beside it (or in --maps), into its own tag cache, its bytes
are converted for this build, then the validator checks its tags and each
of its structure bsps. Fuzzing changes words of its file, so that the
loader and the conversion are tried too. (The game also decodes its Ogg
Vorbis sounds and converts its scripts and geometry, from tags the validator
passed: not here.) Resource maps given as maps are passed over; OpenSauce
maps are refused, as the game refuses them.

With --relocation, each Xbox map is instead read and checked twice: where
it is linked, and at another address with its tags relocated
(port/linux/game/tag_relocate.c), as the Windows 9x build must run it. The
two must be the same but for the pointers moved, and no pointer into the tag
cache may be left unmoved where the game reads it: the relocated tags must
need no other corrections than the linked ones. Each difference is printed,
and, as notes, words in tags' structures that still point into the tag
cache: pointers the game never reads, which the schemas leave out (the
editor's data in a scenario), or data that looks like an address.

usage: map_validate [--strict] [--quiet] [--maps folder] [--fuzz iterations [--seed n] [--log file]]
	[--relocation] map.map|folder...

A folder stands for every .map file in it. Under WSL a Windows path
(C:\Games\maps) is taken as /mnt/c/Games/maps.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include "zlib_prefixed.h"
#include "../port/linux/game/cache_file_formats.h"

/* ---------- constants */

enum
{
	/* where the game reads a map's tags, and how many bytes there are room
	for (physical_memory_map.c, physical_memory_map.h) */
	TAG_CACHE_BASE_ADDRESS = 0x803A6000,
	TAG_CACHE_SIZE = 0x1600000,
	/* where --relocation reads them instead: in the private arena, as on
	Windows 9x, with the same offset in a 256 MB window */
	RELOCATED_TAG_CACHE_ADDRESS = 0x503A6000,

	CACHE_FILE_HEADER_SIZE = 0x800,
	CACHE_FILE_HEADER_SIGNATURE = 'head',
	CACHE_FILE_FOOTER_SIGNATURE = 'foot',
	SCENARIO_GROUP_TAG = 'scnr',
	/* what the game rounds a read up to (cache_files.c) */
	CACHE_FILE_SECTOR_SIZE = 512,
	/* the scenario's structure bsps block (scenario_definitions.h) */
	SCENARIO_STRUCTURE_BSPS_OFFSET = 0x5A4,
	MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO = 32,
};

/* ---------- structures */

struct cache_file_header
{
	unsigned long header_signature;
	long version;
	long file_length;
	unsigned long reserved0C;
	long tag_data_offset;
	long tag_data_size;
	unsigned char reserved18[8];
	char name[0x20];
	char build[0x20];
	unsigned char reserved60[0x79C];
	unsigned long footer_signature;
};

struct tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

struct tag_header
{
	struct tag_instance *instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
};

struct tag_block
{
	long count;
	void *address;
	void *definition;
};

struct scenario_structure_bsp_reference
{
	long file_offset;
	long file_size;
	void *base_address;
	unsigned char unused[4];
	unsigned long group_tag;
	char *name;
	long name_length;
	long tag_index;
};

typedef char verify_cache_file_header_size[sizeof(struct cache_file_header) == CACHE_FILE_HEADER_SIZE ? 1 : -1];
typedef char verify_scenario_structure_bsp_reference_size[
	sizeof(struct scenario_structure_bsp_reference) == 0x20 ? 1 : -1];

/* ---------- the validator's (port/linux/game/tag_validate.c) */

unsigned char tag_validate_tags(void *tag_header, long tag_data_size, long file_length, char const *map_name);

/* the relocation's (port/linux/game/tag_relocate.h) */
struct tag_relocation
{
	unsigned char *cache;
	unsigned long link_address;
	unsigned long size;
	unsigned long delta;
	unsigned long *marks;
	unsigned long *elements;
	long moved;
	unsigned char failed;
};
unsigned char tag_relocate_tags(struct tag_relocation *relocation, long tag_data_size);
unsigned char tag_relocate_structure_bsp(struct tag_relocation *relocation, unsigned long link_address, long size);
unsigned char tag_relocate_marked(struct tag_relocation const *relocation, unsigned long offset);
unsigned char tag_relocate_in_element(struct tag_relocation const *relocation, unsigned long offset);
unsigned char tag_validate_structure_bsp(long tag_index, void *base, long size);
long tag_validate_corrections(void);
unsigned char tag_validate_claimed(void const *address);
struct tag_validate_file_range
{
	unsigned long offset;
	unsigned long size;
};
unsigned char tag_validate_custom_edition_tags(void *tag_header, long loaded_size, unsigned long tag_cache_size,
	struct tag_validate_file_range const *file_ranges, short file_range_count, char const *map_name);

static int quiet;
/* (fuzzing: each bsp is checked twice, and whether a second check of one
found more to correct) */
static int recheck;
static int recheck_failed;
static unsigned long fuzz_seed;

void tag_validate_report(char const *message)
{
	if (!quiet)
		printf("%s\n", message);
}

/* (the game's printf family, which the validator calls by these names:
port/linux/include/stdio.h) */
int halo_linux_vsnprintf(char *buffer, size_t count, char const *format, va_list arguments)
{
	return vsnprintf(buffer, count, format, arguments);
}

int halo_linux_snprintf(char *buffer, size_t count, char const *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vsnprintf(buffer, count, format, arguments);
	va_end(arguments);
	return result;
}

/* (the game's string functions, which its units call by these names:
cseries.h) */
void *csmemset(void *buffer, long c, unsigned long size)
{
	return memset(buffer, (int)c, size);
}

void *csmemcpy(void *destination, void const *source, unsigned long size)
{
	return memcpy(destination, source, size);
}

unsigned long csstrlen(char const *s)
{
	return strlen(s);
}

char *csstrcat(char *s1, char const *s2)
{
	return strcat(s1, s2);
}

long csstrcmp(char const *s1, char const *s2)
{
	return strcmp(s1, s2);
}

/* ---------- code */

/* the map's bytes as the game sees them (its tag data and bsps at their file
offsets): the file, inflated after its header if it is compressed */
static unsigned char *map_read(char const *path, struct cache_file_header *header)
{
	FILE *file = fopen(path, "rb");
	unsigned char *bytes = NULL;
	unsigned char *compressed = NULL;
	long size;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < CACHE_FILE_HEADER_SIZE || fseek(file, 0, SEEK_SET) ||
		fread(header, CACHE_FILE_HEADER_SIZE, 1, file) != 1 ||
		header->header_signature != CACHE_FILE_HEADER_SIGNATURE ||
		header->footer_signature != CACHE_FILE_FOOTER_SIGNATURE ||
		header->file_length < CACHE_FILE_HEADER_SIZE)
	{
		fclose(file);
		return NULL;
	}
	bytes = malloc(header->file_length);
	compressed = malloc(size - CACHE_FILE_HEADER_SIZE);
	if (bytes && compressed && fread(compressed, size - CACHE_FILE_HEADER_SIZE, 1, file) == 1)
	{
		memcpy(bytes, header, CACHE_FILE_HEADER_SIZE);
		if (size == header->file_length)
		{
			memcpy(bytes + CACHE_FILE_HEADER_SIZE, compressed, size - CACHE_FILE_HEADER_SIZE);
		}
		else
		{
			uLongf inflated = header->file_length - CACHE_FILE_HEADER_SIZE;

			if (uncompress(bytes + CACHE_FILE_HEADER_SIZE, &inflated, compressed, size - CACHE_FILE_HEADER_SIZE) != Z_OK ||
				inflated != (uLongf)(header->file_length - CACHE_FILE_HEADER_SIZE))
			{
				free(bytes);
				bytes = NULL;
			}
		}
	}
	else
	{
		free(bytes);
		bytes = NULL;
	}
	free(compressed);
	fclose(file);

	return bytes;
}

/* the map's tags (bytes, as map_read gives them) checked where the game
reads them, then each of its structure bsps where it loads: 0 if the map
plays (its corrections counted), 1 if it is refused */
static int map_check(struct cache_file_header const *header, unsigned char const *bytes, unsigned char *tag_cache,
	char const *name, long *corrections)
{
	struct tag_header *tags = (struct tag_header *)tag_cache;
	struct tag_block *bsps;
	long bsp_count;
	long index;
	int refused = 0;

	*corrections = 0;
	memset(tag_cache, 0xCD, TAG_CACHE_SIZE);
	memcpy(tag_cache, bytes + header->tag_data_offset, header->tag_data_size);
	if (!tag_validate_tags(tag_cache, header->tag_data_size, header->file_length, name))
		return 1;
	*corrections = tag_validate_corrections();

	/* each bsp, where it loads (scenario_structure_bsp_load): the reference
	is the validated scenario's, which is a scenario (as
	cache_file_tag_header_verify checks) */
	{
		short scenario_index = (short)tags->scenario_tag_index;
		struct tag_instance *scenario;

		if (scenario_index < 0 || scenario_index >= tags->tag_count ||
			tags->instances[scenario_index].tag_index != tags->scenario_tag_index ||
			tags->instances[scenario_index].group_tag != SCENARIO_GROUP_TAG)
		{
			if (!quiet)
				printf("%s: its tag header names no scenario\n", name);
			return 1;
		}
		scenario = &tags->instances[scenario_index];

		bsps = (struct tag_block *)((unsigned char *)scenario->base_address + SCENARIO_STRUCTURE_BSPS_OFFSET);
		bsp_count = bsps->count < MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO ? bsps->count : MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO;
	}
	for (index = 0; index < bsp_count && !refused; index++)
	{
		struct scenario_structure_bsp_reference *reference =
			(struct scenario_structure_bsp_reference *)bsps->address + index;
		unsigned long load = (unsigned long)reference->base_address - TAG_CACHE_BASE_ADDRESS;
		/* (the game reads whole sectors) */
		unsigned long read_size = ((unsigned long)reference->file_size + CACHE_FILE_SECTOR_SIZE - 1) &
			~(unsigned long)(CACHE_FILE_SECTOR_SIZE - 1);

		/* (what cache_file_structure_bsp_reference_verify checks) */
		if (reference->file_offset < 0 || reference->file_size <= 0 ||
			reference->file_offset > header->file_length - reference->file_size ||
			load < (unsigned long)header->tag_data_size || load > TAG_CACHE_SIZE ||
			read_size > TAG_CACHE_SIZE - load)
		{
			if (!quiet)
				printf("%s: its structure bsp %ld is outside it\n", name, index);
			refused = 1;
			break;
		}
		memset(tag_cache + header->tag_data_size, 0xCD, TAG_CACHE_SIZE - header->tag_data_size);
		memcpy(reference->base_address, bytes + reference->file_offset, reference->file_size);
		if (!tag_validate_structure_bsp(reference->tag_index, reference->base_address, reference->file_size))
		{
			refused = 1;
		}
		/* (fuzzing: the bsp as corrected, checked again, needs no more) */
		else if (recheck)
		{
			long before = tag_validate_corrections();

			if (!tag_validate_structure_bsp(reference->tag_index, reference->base_address, reference->file_size) ||
				tag_validate_corrections() != before)
			{
				printf("%s: fuzz seed %lu: structure bsp %ld as corrected is not clean when checked again\n", name,
					fuzz_seed, index);
				recheck_failed = 1;
			}
		}
	}
	*corrections = tag_validate_corrections();

	return refused;
}

static int map_validate(char const *path, unsigned char *tag_cache, long *corrections)
{
	struct cache_file_header header;
	unsigned char *bytes = map_read(path, &header);
	char const *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	int refused;

	*corrections = 0;
	if (!bytes)
	{
		printf("%s: cannot be read\n", name);
		return 1;
	}
	if (header.tag_data_offset < 0 || header.tag_data_size <= 0 || header.tag_data_size > TAG_CACHE_SIZE ||
		header.tag_data_offset > header.file_length - header.tag_data_size)
	{
		printf("%s: its tag data is outside it\n", name);
		free(bytes);
		return 1;
	}
	refused = map_check(&header, bytes, tag_cache, name, corrections);
	free(bytes);

	return refused;
}

/* ---------- relocation */

enum
{
	MAXIMUM_RELOCATION_REPORTS = 12,
};

/* where the bsp being compared starts in the tag cache (0: the tags) */
static unsigned long relocation_bsp_offset;
/* words left pointing into the tag cache (notes, not problems) */
static long relocation_notes;

struct relocation_snapshot
{
	unsigned char *bytes;
	unsigned long offset;
	unsigned long size;
};

/* the tag (of the tags at tag_cache) whose root is nearest below offset, to
say roughly where something is */
static void relocation_where(unsigned char const *tag_cache, unsigned long link, unsigned long offset,
	char *text, size_t text_size)
{
	if (relocation_bsp_offset)
	{
		snprintf(text, text_size, "structure bsp +%lx", offset - relocation_bsp_offset);
		return;
	}
	{
	struct tag_header const *tags = (struct tag_header const *)tag_cache;
	struct tag_instance const *instances;
	unsigned long best = 0;
	long best_index = -1;
	long index;

	snprintf(text, text_size, "?");
	instances = (struct tag_instance const *)(tag_cache + ((unsigned long)tags->instances - link));
	for (index = 0; index < tags->tag_count; index++)
	{
		unsigned long root = (unsigned long)instances[index].base_address - link;

		if (instances[index].base_address && root <= offset && root >= best)
		{
			best = root;
			best_index = index;
		}
	}
	if (best_index >= 0)
	{
		unsigned long group = instances[best_index].group_tag;
		unsigned long name = (unsigned long)instances[best_index].name - link;

		snprintf(text, text_size, "%c%c%c%c %s +%lx", (int)(group >> 24), (int)(group >> 16 & 0xFF),
			(int)(group >> 8 & 0xFF), (int)(group & 0xFF),
			name < TAG_CACHE_SIZE ? (char const *)tag_cache + name : "?", offset - best);
	}
	}
}

/* the relocated bytes (relocated, at the snapshot's offset of the tag cache)
against the snapshot of the bytes checked where they are linked: the same
but for the pointers moved (moved by delta, or the same where the validator
replaced them), and no unmoved word pointing into the tag cache */
static long relocation_compare(struct relocation_snapshot const *snapshot, unsigned char const *relocated,
	struct tag_relocation const *relocation, unsigned char const *linked_cache, char const *name, char const *what)
{
	unsigned long index = 0;
	long problems = 0;
	char where[128];

	relocation_bsp_offset = snapshot->offset;
	while (index < snapshot->size)
	{
		unsigned long offset = snapshot->offset + index;

		if (tag_relocate_marked(relocation, offset) && snapshot->size - index >= 4)
		{
			unsigned long a, b;

			memcpy(&a, snapshot->bytes + index, 4);
			memcpy(&b, relocated + index, 4);
			if (b != a && b != a + relocation->delta)
			{
				if (problems++ < MAXIMUM_RELOCATION_REPORTS)
				{
					relocation_where(linked_cache, TAG_CACHE_BASE_ADDRESS, offset, where, sizeof(where));
					printf("%s: %s: a pointer moved is %08lx, %08lx where linked (%08lx; %s)\n", name, what, b, a,
						TAG_CACHE_BASE_ADDRESS + offset, where);
				}
			}
			index += 4;
			continue;
		}
		if (snapshot->bytes[index] != relocated[index])
		{
			if (problems++ < MAXIMUM_RELOCATION_REPORTS)
			{
				relocation_where(linked_cache, TAG_CACHE_BASE_ADDRESS, offset, where, sizeof(where));
				printf("%s: %s: byte at %08lx differs (%02x, %02x where linked; %s)\n", name, what,
					TAG_CACHE_BASE_ADDRESS + offset, relocated[index], snapshot->bytes[index], where);
			}
		}
		if ((offset & 3) == 0 && snapshot->size - index >= 4)
		{
			unsigned long a;

			memcpy(&a, snapshot->bytes + index, 4);
			/* (in tags' roots and blocks' elements only: vertices, pixels and
			the like in data have words that look like addresses) */
			if (a - TAG_CACHE_BASE_ADDRESS < TAG_CACHE_SIZE && tag_relocate_in_element(relocation, offset) &&
				!tag_relocate_marked(relocation, offset + 1) && !tag_relocate_marked(relocation, offset + 2) &&
				!tag_relocate_marked(relocation, offset + 3))
			{
				if (relocation_notes++ < MAXIMUM_RELOCATION_REPORTS)
				{
					relocation_where(linked_cache, TAG_CACHE_BASE_ADDRESS, offset, where, sizeof(where));
					printf("%s: %s: note: %08lx at %08lx looks like an address in the tag cache, not moved (%s)\n",
						name, what, a, TAG_CACHE_BASE_ADDRESS + offset, where);
				}
			}
		}
		index++;
	}

	return problems;
}

/* the map's tags and bsps, checked where linked (in tag_cache) then
relocated (in relocated_cache): 0 if the two agree */
static int map_relocation_check(char const *path, unsigned char *tag_cache, unsigned char *relocated_cache)
{
	static unsigned long marks[TAG_CACHE_SIZE / 32];
	static unsigned long elements[TAG_CACHE_SIZE / 32];
	struct relocation_snapshot snapshots[1 + MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO];
	struct cache_file_header header;
	struct tag_relocation relocation;
	unsigned char *bytes = map_read(path, &header);
	char const *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	struct tag_header *tags;
	struct tag_block *bsps;
	long bsp_count = 0;
	long index;
	long moved = 0;
	long problems = 0;
	long corrections[2] = { 0, 0 };
	int pass;

	memset(snapshots, 0, sizeof(snapshots));
	relocation_notes = 0;
	if (!bytes || header.tag_data_offset < 0 || header.tag_data_size <= 0 || header.tag_data_size > TAG_CACHE_SIZE ||
		header.tag_data_offset > header.file_length - header.tag_data_size)
	{
		printf("%s: cannot be read\n", name);
		free(bytes);
		return 1;
	}

	/* where linked (pass 0), then relocated (pass 1) */
	for (pass = 0; pass < 2; pass++)
	{
		unsigned char *cache = pass ? relocated_cache : tag_cache;

		memset(&relocation, 0, sizeof(relocation));
		relocation.cache = cache;
		relocation.link_address = TAG_CACHE_BASE_ADDRESS;
		relocation.size = TAG_CACHE_SIZE;
		relocation.delta = (unsigned long)cache - TAG_CACHE_BASE_ADDRESS;
		relocation.marks = marks;
		relocation.elements = elements;
		memset(marks, 0, sizeof(marks));
		memset(elements, 0, sizeof(elements));

		memset(cache, 0xCD, TAG_CACHE_SIZE);
		memcpy(cache, bytes + header.tag_data_offset, header.tag_data_size);
		if (pass && !tag_relocate_tags(&relocation, header.tag_data_size))
			printf("%s: its tags could not all be walked to relocate them\n", name);
		moved += relocation.moved;
		if (!tag_validate_tags(cache, header.tag_data_size, header.file_length, name))
		{
			printf("%s: refused %s\n", name, pass ? "relocated" : "where linked");
			problems++;
			break;
		}
		if (!pass)
		{
			snapshots[0].bytes = malloc(header.tag_data_size);
			snapshots[0].size = header.tag_data_size;
			memcpy(snapshots[0].bytes, cache, header.tag_data_size);
		}
		else
		{
			problems += relocation_compare(&snapshots[0], cache, &relocation, tag_cache, name, "tags");
		}

		tags = (struct tag_header *)cache;
		{
			short scenario_index = (short)tags->scenario_tag_index;
			struct tag_instance *scenario = &tags->instances[scenario_index];

			bsps = (struct tag_block *)((unsigned char *)scenario->base_address + SCENARIO_STRUCTURE_BSPS_OFFSET);
			bsp_count = bsps->count < MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO ? bsps->count : MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO;
		}
		for (index = 0; index < bsp_count; index++)
		{
			struct scenario_structure_bsp_reference *reference =
				(struct scenario_structure_bsp_reference *)bsps->address + index;
			unsigned long load = (unsigned long)reference->base_address - (unsigned long)cache;
			struct relocation_snapshot *snapshot = &snapshots[1 + index];
			char what[32];

			if (reference->file_offset < 0 || reference->file_size <= 0 ||
				reference->file_offset > header.file_length - reference->file_size ||
				load < (unsigned long)header.tag_data_size || load >= TAG_CACHE_SIZE ||
				(unsigned long)reference->file_size > TAG_CACHE_SIZE - load)
			{
				printf("%s: structure bsp %ld is outside the tag cache %s\n", name, index,
					pass ? "relocated" : "where linked");
				problems++;
				continue;
			}
			/* (a bsp's marks are its own: the last one's are cleared) */
			memset((unsigned char *)marks + header.tag_data_size / 8, 0,
				sizeof(marks) - header.tag_data_size / 8);
			memset((unsigned char *)elements + header.tag_data_size / 8, 0,
				sizeof(elements) - header.tag_data_size / 8);
			relocation.moved = 0;
			memset(cache + header.tag_data_size, 0xCD, TAG_CACHE_SIZE - header.tag_data_size);
			memcpy(cache + load, bytes + reference->file_offset, reference->file_size);
			if (pass && !tag_relocate_structure_bsp(&relocation, TAG_CACHE_BASE_ADDRESS + load, reference->file_size))
				printf("%s: structure bsp %ld could not all be walked to relocate it\n", name, index);
			moved += relocation.moved;
			if (!tag_validate_structure_bsp(reference->tag_index, cache + load, reference->file_size))
			{
				printf("%s: structure bsp %ld refused %s\n", name, index, pass ? "relocated" : "where linked");
				problems++;
				continue;
			}
			snprintf(what, sizeof(what), "structure bsp %ld", index);
			if (!pass)
			{
				snapshot->bytes = malloc(reference->file_size);
				snapshot->offset = load;
				snapshot->size = reference->file_size;
				memcpy(snapshot->bytes, cache + load, reference->file_size);
			}
			else if (snapshot->bytes)
			{
				problems += relocation_compare(snapshot, cache + load, &relocation, tag_cache, name, what);
			}
		}
		corrections[pass] = tag_validate_corrections();
	}
	/* (the corrections a map needs where it is linked, and no more where it
	is relocated: one more is a pointer the validator found unmoved) */
	if (corrections[1] != corrections[0])
	{
		printf("%s: %ld corrections relocated, %ld where linked\n", name, corrections[1], corrections[0]);
		problems++;
	}

	for (index = 0; index <= MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO; index++)
		free(snapshots[index].bytes);
	free(bytes);
	printf("%s: relocated: %ld pointers moved, %ld bsps, %ld problems, %ld notes\n", name, moved, bsp_count, problems,
		relocation_notes);

	return problems != 0;
}

/* ---------- fuzzing */

/* the run being fuzzed, for the signal handlers' message */
static char const *fuzz_name = "";

static unsigned long fuzz_state;
static FILE *fuzz_log;

static unsigned long fuzz_random(void)
{
	/* (xorshift32) */
	fuzz_state ^= fuzz_state << 13;
	fuzz_state ^= fuzz_state >> 17;
	fuzz_state ^= fuzz_state << 5;
	return fuzz_state;
}

static void fuzz_signal(int signal_number)
{
	char message[160];
	int length = snprintf(message, sizeof(message), "map_validate: %s, fuzz seed %lu: %s\n", fuzz_name, fuzz_seed,
		signal_number == SIGALRM ? "hangs" : "crashes");

	if (write(2, message, length) < 0)
		_exit(3);
	_exit(3);
}

/* a value to put in a field: one of those that break a reader of tags most */
static unsigned long fuzz_value(struct cache_file_header const *header)
{
	switch (fuzz_random() % 9)
	{
	case 0: return fuzz_random();
	case 1: return fuzz_random() % 8;
	case 2: return fuzz_random() % 300;
	case 3: return 0xFFFFFFFFUL;
	case 4: return 0x80000000UL;
	case 5: return 0x7FFFFFFFUL;
	case 6: return 0xFFFF;
	case 7: return TAG_CACHE_BASE_ADDRESS + fuzz_random() % (unsigned long)header->tag_data_size;
	default: return TAG_CACHE_BASE_ADDRESS + fuzz_random() % TAG_CACHE_SIZE;
	}
}

/* iterations runs of the map, each with a few of its tags' and bsps' words
changed: the validator must never crash or hang, and the tags it lets
through, checked again, must need no more corrections */
static int map_fuzz(char const *path, unsigned char *tag_cache, unsigned long seed, long iterations)
{
	struct cache_file_header header;
	unsigned char *bytes = map_read(path, &header);
	char const *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	long iteration;
	long refusals = 0;
	long corrected = 0;
	int failed = 0;
	unsigned long *claimed = NULL;
	long claimed_count = 0;

	if (!bytes)
	{
		printf("%s: cannot be read\n", name);
		return 1;
	}
	if (header.tag_data_offset < 0 || header.tag_data_size <= 0 || header.tag_data_size > TAG_CACHE_SIZE ||
		header.tag_data_offset > header.file_length - header.tag_data_size)
	{
		printf("%s: its tag data is outside it\n", name);
		free(bytes);
		return 1;
	}
	fuzz_name = name;
	recheck = 1;
	/* (the words of the tags' roots, blocks and data, where nearly every
	field the game trusts is: most changes go there) */
	{
		long corrections;
		long offset;

		map_check(&header, bytes, tag_cache, name, &corrections);
		claimed = malloc(sizeof(*claimed) * (header.tag_data_size / 4 + 1));
		for (offset = 0; claimed && offset + 4 <= header.tag_data_size; offset += 4)
		{
			if (tag_validate_claimed(tag_cache + offset))
				claimed[claimed_count++] = header.tag_data_offset + offset;
		}
	}
	for (iteration = 0; iteration < iterations; iteration++)
	{
		enum { MAXIMUM_CHANGES = 8 };
		unsigned long offsets[MAXIMUM_CHANGES];
		unsigned long values[MAXIMUM_CHANGES];
		long changes = 1 + fuzz_random() % MAXIMUM_CHANGES;
		long corrections;
		long change;

		fuzz_seed = seed + iteration;
		fuzz_state = fuzz_seed * 2654435761UL + 1;
		changes = 1 + fuzz_random() % MAXIMUM_CHANGES;
		for (change = 0; change < changes; change++)
		{
			/* (most in the tags' roots, blocks and data, some anywhere in the
			tags, some anywhere in the file: its bsps) */
			unsigned long choice = fuzz_random() % 8;
			unsigned long offset = choice < 5 && claimed_count ? claimed[fuzz_random() % claimed_count] :
				choice < 7 ? header.tag_data_offset + (fuzz_random() % (unsigned long)header.tag_data_size) :
				fuzz_random() % (unsigned long)header.file_length;

			offset &= ~3UL;
			if (offset + 4 > (unsigned long)header.file_length)
				offset = header.tag_data_offset;
			offsets[change] = offset;
			memcpy(&values[change], bytes + offset, 4);
			{
				unsigned long value = fuzz_value(&header);

				memcpy(bytes + offset, &value, 4);
			}
		}

		alarm(60);
		if (map_check(&header, bytes, tag_cache, name, &corrections))
		{
			refusals++;
		}
		else
		{
			long again;

			if (corrections)
			{
				corrected++;
				/* (with --log, the changes of a run that was corrected, for
				playing it in the game: "offset value" in the file) */
				if (fuzz_log)
				{
					fprintf(fuzz_log, "%s %lu", name, fuzz_seed);
					for (change = 0; change < changes; change++)
					{
						unsigned long value;

						memcpy(&value, bytes + offsets[change], 4);
						fprintf(fuzz_log, " %lx:%lx", offsets[change], value);
					}
					fprintf(fuzz_log, "\n");
				}
			}
			/* (the tags as corrected, checked again: nothing more to correct) */
			if (!tag_validate_tags(tag_cache, header.tag_data_size, header.file_length, name) ||
				(again = tag_validate_corrections()) != 0)
			{
				printf("%s: fuzz seed %lu: the corrected tags are not clean when checked again:\n", name, fuzz_seed);
				failed = 1;
				/* (what was corrected, then what is left) */
				quiet = 0;
				map_check(&header, bytes, tag_cache, name, &corrections);
				printf("checked again:\n");
				tag_validate_tags(tag_cache, header.tag_data_size, header.file_length, name);
				quiet = 1;
			}
		}
		alarm(0);

		/* (undone, last first) */
		for (change = changes - 1; change >= 0; change--)
			memcpy(bytes + offsets[change], &values[change], 4);
	}
	recheck = 0;
	if (recheck_failed)
		failed = 1;
	printf("%s: %ld runs: %ld refused, %ld corrected, %ld unchanged\n", name, iterations, refusals, corrected,
		iterations - refusals - corrected);
	free(claimed);
	free(bytes);

	return failed;
}

/* ---------- Custom Edition maps */

enum
{
	/* where a Custom Edition map's bitmaps.map and sounds.map count in the
	offsets of its tags, after the map itself (as the game has them,
	custom_edition_cache.c) */
	CUSTOM_EDITION_BITMAPS_OFFSET = 0x40000000,
	CUSTOM_EDITION_SOUNDS_OFFSET = 0x60000000,
};

/* a file read whole */
struct memory_file
{
	unsigned char *bytes;
	unsigned long size;
	struct cache_file_source source;
};

/* the folder resource maps are looked in, when not beside the map (--maps) */
static char const *resource_maps_folder;

static int memory_file_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	struct memory_file const *file = context;

	if (offset > file->size || size > file->size - offset)
		return 0;
	memcpy(buffer, file->bytes + offset, size);
	return 1;
}

static int memory_file_open(struct memory_file *file, char const *path)
{
	FILE *stream = fopen(path, "rb");
	long size;

	memset(file, 0, sizeof(*file));
	if (!stream)
		return 0;
	if (fseek(stream, 0, SEEK_END) || (size = ftell(stream)) < 0 || fseek(stream, 0, SEEK_SET) ||
		!(file->bytes = malloc((size_t)size + 1)) || fread(file->bytes, 1, (size_t)size, stream) != (size_t)size)
	{
		free(file->bytes);
		file->bytes = NULL;
		fclose(stream);
		return 0;
	}
	fclose(stream);
	file->size = (unsigned long)size;
	file->source.context = file;
	file->source.read = memory_file_read;
	file->source.size = (uint32_t)size;
	return 1;
}

static void memory_file_close(struct memory_file *file)
{
	free(file->bytes);
	memset(file, 0, sizeof(*file));
}

/* a map's resource maps (bitmaps.map, sounds.map, loc.map), beside it or in
--maps; those missing are left closed, and the map refused if it needs them */
struct custom_edition_resources
{
	struct memory_file files[NUMBER_OF_RESOURCE_MAP_TYPES];
	struct resource_map storage[NUMBER_OF_RESOURCE_MAP_TYPES];
	struct resource_map *maps[NUMBER_OF_RESOURCE_MAP_TYPES];
};

static void custom_edition_resources_open(struct custom_edition_resources *resources, char const *map_path)
{
	short type;

	memset(resources, 0, sizeof(*resources));
	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		char path[1024];
		char const *slash = strrchr(map_path, '/');
		int folder_length = resource_maps_folder ? (int)strlen(resource_maps_folder) : slash ? (int)(slash - map_path) : 1;
		char const *folder = resource_maps_folder ? resource_maps_folder : slash ? map_path : ".";

		snprintf(path, sizeof(path), "%.*s/%s.map", folder_length, folder,
			resource_map_type_describe((enum resource_map_type)type));
		if (memory_file_open(&resources->files[type], path) &&
			resource_map_open(&resources->files[type].source, (enum resource_map_type)type, &resources->storage[type]) ==
				_cache_file_status_ok)
		{
			resources->maps[type] = &resources->storage[type];
		}
	}
}

static void custom_edition_resources_close(struct custom_edition_resources *resources)
{
	short type;

	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		if (resources->maps[type])
			resource_map_close(resources->maps[type]);
		memory_file_close(&resources->files[type]);
	}
}

/* a Custom Edition map (map, with its resources) loaded, converted and
checked where the game does it, then each of its structure bsps: 0 if it
plays (its corrections counted), 1 if it is refused. *loaded_bytes is how
many bytes of the tag cache its tags take. */
static int custom_edition_check(struct memory_file *map, struct custom_edition_resources *resources,
	unsigned char *tag_cache, char const *name, long *corrections, unsigned long *loaded_bytes)
{
	struct custom_edition_load_report report;
	struct custom_edition_conversion_report conversion;
	struct tag_validate_file_range ranges[3];
	short range_count = 0;
	enum cache_file_status status;
	struct tag_header *tags = (struct tag_header *)tag_cache;
	struct tag_block *bsps;
	long bsp_count;
	long index;
	int refused = 0;

	*corrections = 0;
	*loaded_bytes = 0;
	memset(tag_cache, 0xCD, CUSTOM_EDITION_TAG_CACHE_BYTES);
	status = custom_edition_cache_load(&map->source, resources->maps, tag_cache, CUSTOM_EDITION_TAG_CACHE_BYTES,
		&report);
	if (status != _cache_file_status_ok)
	{
		if (!quiet)
		{
			printf("%s: its loader refuses it: %s (tag %ld, %08lx)\n", name, cache_file_status_describe(status),
				(long)report.problem_tag_index, (unsigned long)report.problem_location);
		}
		return 1;
	}
	*loaded_bytes = report.tag_data_bytes + report.resource_tag_bytes;
	custom_edition_cache_combine_resource_offsets(tag_cache, *loaded_bytes, CUSTOM_EDITION_BITMAPS_OFFSET,
		CUSTOM_EDITION_SOUNDS_OFFSET);
	status = custom_edition_cache_convert(tag_cache, *loaded_bytes, report.identity.name, &conversion);
	if (status != _cache_file_status_ok)
	{
		if (!quiet)
			printf("%s: its conversion refuses it: %s\n", name, cache_file_status_describe(status));
		return 1;
	}
	ranges[range_count].offset = 0;
	ranges[range_count++].size = report.identity.file_length;
	if (resources->maps[_resource_map_bitmaps])
	{
		ranges[range_count].offset = CUSTOM_EDITION_BITMAPS_OFFSET;
		ranges[range_count++].size = resources->files[_resource_map_bitmaps].size;
	}
	if (resources->maps[_resource_map_sounds])
	{
		ranges[range_count].offset = CUSTOM_EDITION_SOUNDS_OFFSET;
		ranges[range_count++].size = resources->files[_resource_map_sounds].size;
	}
	if (!tag_validate_custom_edition_tags(tag_cache, (long)*loaded_bytes, report.tag_cache_bytes, ranges, range_count,
		name))
	{
		return 1;
	}
	*corrections = tag_validate_corrections();

	/* each bsp, where it loads: the scenario's references, as its loader
	found them (custom_edition_structure_bsp_reference_valid) */
	{
		short scenario_index = (short)tags->scenario_tag_index;

		if (scenario_index < 0 || scenario_index >= tags->tag_count ||
			tags->instances[scenario_index].group_tag != SCENARIO_GROUP_TAG)
		{
			if (!quiet)
				printf("%s: its tag header names no scenario\n", name);
			return 1;
		}
		bsps = (struct tag_block *)((unsigned char *)tags->instances[scenario_index].base_address +
			SCENARIO_STRUCTURE_BSPS_OFFSET);
		bsp_count = bsps->count < MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO ? bsps->count : MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO;
	}
	for (index = 0; index < bsp_count && !refused; index++)
	{
		struct scenario_structure_bsp_reference *reference =
			(struct scenario_structure_bsp_reference *)bsps->address + index;
		unsigned long address = (unsigned long)reference->base_address;
		unsigned long top = CUSTOM_EDITION_TAG_CACHE_ADDRESS + report.tag_cache_bytes;

		if (reference->file_offset < CACHE_FILE_HEADER_SIZE || reference->file_size < 0x18 ||
			(unsigned long)reference->file_offset > report.identity.file_length ||
			(unsigned long)reference->file_size > report.identity.file_length - (unsigned long)reference->file_offset ||
			address < CUSTOM_EDITION_TAG_CACHE_ADDRESS + *loaded_bytes || address > top ||
			(unsigned long)reference->file_size > top - address)
		{
			if (!quiet)
				printf("%s: its structure bsp %ld is outside it\n", name, index);
			refused = 1;
			break;
		}
		memset(tag_cache + *loaded_bytes, 0xCD, report.tag_cache_bytes - *loaded_bytes);
		memcpy(reference->base_address, map->bytes + reference->file_offset, reference->file_size);
		if (!tag_validate_structure_bsp(reference->tag_index, reference->base_address, reference->file_size))
		{
			refused = 1;
		}
		else if (recheck)
		{
			long before = tag_validate_corrections();

			if (!tag_validate_structure_bsp(reference->tag_index, reference->base_address, reference->file_size) ||
				tag_validate_corrections() != before)
			{
				printf("%s: fuzz seed %lu: structure bsp %ld as corrected is not clean when checked again\n", name,
					fuzz_seed, index);
				recheck_failed = 1;
			}
		}
	}
	*corrections = tag_validate_corrections();

	return refused;
}

/* the Custom Edition map at path checked (custom_edition_check), or, with
iterations, fuzzed as map_fuzz fuzzes an Xbox map, its file's words changed */
static int custom_edition_validate(char const *path, unsigned char *tag_cache, long *corrections, unsigned long seed,
	long iterations)
{
	char const *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	struct memory_file map;
	struct custom_edition_resources resources;
	struct cache_file_identity identity;
	enum cache_file_status status;
	unsigned long loaded_bytes;
	unsigned long *claimed = NULL;
	long claimed_count = 0;
	long refusals = 0;
	long corrected = 0;
	long iteration;
	int failed = 0;

	*corrections = 0;
	if (!memory_file_open(&map, path))
	{
		printf("%s: cannot be read\n", name);
		return 1;
	}
	status = cache_file_identify(&map.source, &identity);
	if (status != _cache_file_status_ok)
	{
		printf("%s: its loader refuses it: %s\n", name, cache_file_status_describe(status));
		memory_file_close(&map);
		return 1;
	}
	custom_edition_resources_open(&resources, path);
	if (!iterations)
	{
		failed = custom_edition_check(&map, &resources, tag_cache, name, corrections, &loaded_bytes);
		custom_edition_resources_close(&resources);
		memory_file_close(&map);
		return failed;
	}

	fuzz_name = name;
	recheck = 1;
	/* (the words of its tag data that the tags' roots, blocks and data
	hold, where most changes go) */
	if (!custom_edition_check(&map, &resources, tag_cache, name, corrections, &loaded_bytes))
	{
		unsigned long offset;

		claimed = malloc(sizeof(*claimed) * (identity.tag_data_size / 4 + 1));
		for (offset = 0; claimed && offset + 4 <= identity.tag_data_size; offset += 4)
		{
			if (tag_validate_claimed(tag_cache + offset))
				claimed[claimed_count++] = identity.tag_data_offset + offset;
		}
	}
	for (iteration = 0; iteration < iterations; iteration++)
	{
		enum { MAXIMUM_CHANGES = 8 };
		unsigned long offsets[MAXIMUM_CHANGES];
		unsigned long values[MAXIMUM_CHANGES];
		long changes;
		long change;

		fuzz_seed = seed + iteration;
		fuzz_state = fuzz_seed * 2654435761UL + 1;
		changes = 1 + fuzz_random() % MAXIMUM_CHANGES;
		for (change = 0; change < changes; change++)
		{
			/* (most in the tags' roots, blocks and data, some anywhere in the
			tag data, some anywhere in the file: its bsps, its model data,
			its header) */
			unsigned long choice = fuzz_random() % 8;
			unsigned long offset = choice < 5 && claimed_count ? claimed[fuzz_random() % claimed_count] :
				choice < 7 ? identity.tag_data_offset + fuzz_random() % identity.tag_data_size :
				fuzz_random() % map.size;
			unsigned long value;

			offset &= ~3UL;
			if (offset + 4 > map.size)
				offset = identity.tag_data_offset;
			offsets[change] = offset;
			memcpy(&values[change], map.bytes + offset, 4);
			switch (fuzz_random() % 9)
			{
			case 0: value = fuzz_random(); break;
			case 1: value = fuzz_random() % 8; break;
			case 2: value = fuzz_random() % 300; break;
			case 3: value = 0xFFFFFFFFUL; break;
			case 4: value = 0x80000000UL; break;
			case 5: value = 0x7FFFFFFFUL; break;
			case 6: value = 0xFFFF; break;
			case 7: value = CUSTOM_EDITION_TAG_CACHE_ADDRESS + fuzz_random() % identity.tag_data_size; break;
			default: value = CUSTOM_EDITION_TAG_CACHE_ADDRESS + fuzz_random() % CUSTOM_EDITION_TAG_CACHE_BYTES; break;
			}
			memcpy(map.bytes + offset, &value, 4);
		}

		alarm(120);
		if (custom_edition_check(&map, &resources, tag_cache, name, corrections, &loaded_bytes))
		{
			refusals++;
		}
		else
		{
			if (*corrections)
			{
				corrected++;
				if (fuzz_log)
				{
					fprintf(fuzz_log, "%s %lu", name, fuzz_seed);
					for (change = 0; change < changes; change++)
					{
						unsigned long value;

						memcpy(&value, map.bytes + offsets[change], 4);
						fprintf(fuzz_log, " %lx:%lx", offsets[change], value);
					}
					fprintf(fuzz_log, "\n");
				}
			}
			/* (the tags as corrected, checked again: nothing more to correct;
			the files' ranges do not matter to the tags' own) */
			{
				struct tag_validate_file_range everything = { 0, 0xFFFFFFFFUL };

				if (!tag_validate_custom_edition_tags(tag_cache, (long)loaded_bytes, CUSTOM_EDITION_TAG_CACHE_BYTES,
						&everything, 1, name) ||
					tag_validate_corrections() != 0)
				{
					printf("%s: fuzz seed %lu: the corrected tags are not clean when checked again:\n", name, fuzz_seed);
					failed = 1;
					/* (what was corrected, then what is left) */
					quiet = 0;
					custom_edition_check(&map, &resources, tag_cache, name, corrections, &loaded_bytes);
					printf("checked again:\n");
					tag_validate_custom_edition_tags(tag_cache, (long)loaded_bytes, CUSTOM_EDITION_TAG_CACHE_BYTES,
						&everything, 1, name);
					quiet = 1;
				}
			}
		}
		alarm(0);
		for (change = changes - 1; change >= 0; change--)
			memcpy(map.bytes + offsets[change], &values[change], 4);
	}
	recheck = 0;
	if (recheck_failed)
		failed = 1;
	printf("%s: %ld runs: %ld refused, %ld corrected, %ld unchanged\n", name, iterations, refusals, corrected,
		iterations - refusals - corrected);
	free(claimed);
	custom_edition_resources_close(&resources);
	memory_file_close(&map);

	return failed;
}

/* the format of the file at path, from its first bytes */
static enum cache_file_format file_format(char const *path)
{
	unsigned char header[CACHE_FILE_HEADER_SIZE];
	FILE *file = fopen(path, "rb");
	size_t read;

	if (!file)
		return _cache_file_format_unrecognized;
	read = fread(header, 1, sizeof(header), file);
	fclose(file);
	if (read < 16)
		return _cache_file_format_unrecognized;
	if (read == sizeof(header) && cache_file_header_format(header) != _cache_file_format_unrecognized)
		return cache_file_header_format(header);
	{
		unsigned long type = header[0] | header[1] << 8 | (unsigned long)header[2] << 16 | (unsigned long)header[3] << 24;

		return type > _resource_map_none && type < NUMBER_OF_RESOURCE_MAP_TYPES ?
			_cache_file_format_resource_map : _cache_file_format_unrecognized;
	}
}

/* ---------- what to check */

/* a Windows path (C:\Games\maps) as WSL sees it (/mnt/c/Games/maps), so
that a folder can be given as Explorer shows it; any other path as it is */
static char const *wsl_path(char const *path, char *buffer, size_t size)
{
	size_t index;
	size_t length;

	if (!((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) || path[1] != ':' ||
		(path[2] != '\\' && path[2] != '/' && path[2] != 0))
	{
		return path;
	}
	snprintf(buffer, size, "/mnt/%c%s", path[0] | 0x20, path + 2);
	length = strlen(buffer);
	for (index = 0; index < length; index++)
	{
		if (buffer[index] == '\\')
			buffer[index] = '/';
	}

	return buffer;
}

/* the options and the tag caches main sets up */
static unsigned char *check_tag_cache;
static unsigned char *check_custom_edition_tag_cache;
static unsigned char *check_relocated_cache;
static int check_relocation;
static int check_strict;
static long check_fuzz_iterations;
static unsigned long check_seed = 1;
/* (--relocation's summary) */
static long relocation_maps;
static long relocation_failed_maps;

/* a map file, checked as the options say: 1 if it failed */
static int map_file_check(char const *path)
{
	unsigned char *tag_cache = check_tag_cache;
	unsigned char *custom_edition_tag_cache = check_custom_edition_tag_cache;
	unsigned char *relocated_cache = check_relocated_cache;
	int relocation = check_relocation;
	int strict = check_strict;
	long fuzz_iterations = check_fuzz_iterations;
	unsigned long seed = check_seed;
	long corrections;
	int failed = 0;

	switch (file_format(path))
	{
	case _cache_file_format_resource_map:
		if (!quiet)
			printf("%s: a resource map, passed over\n", path);
		return failed;
	case _cache_file_format_custom_edition_cache:
		if (custom_edition_validate(path, custom_edition_tag_cache, &corrections, seed, fuzz_iterations))
		{
			if (!fuzz_iterations)
				printf("%s: refused\n", path);
			failed = 1;
		}
		else if (!fuzz_iterations)
		{
			printf("%s: %ld corrections\n", path, corrections);
			if (strict && corrections)
				failed = 1;
		}
		fflush(stdout);
		return failed;
	default:
		break;
	}
	if (relocation)
	{
		relocation_maps++;
		if (map_relocation_check(path, tag_cache, relocated_cache))
		{
			relocation_failed_maps++;
			failed = 1;
		}
		fflush(stdout);
		return failed;
	}
	if (fuzz_iterations)
	{
		if (map_fuzz(path, tag_cache, seed, fuzz_iterations))
			failed = 1;
		fflush(stdout);
		return failed;
	}
	if (map_validate(path, tag_cache, &corrections))
	{
		printf("%s: refused\n", path);
		failed = 1;
	}
	else
	{
		printf("%s: %ld corrections\n", path, corrections);
		if (strict && corrections)
			failed = 1;
	}
	fflush(stdout);

	return failed;
}

static int map_name_compare(void const *a, void const *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* every .map file in a folder (not its subfolders), in name order: 1 if any
failed */
static int map_folder_check(char const *folder)
{
	DIR *directory = opendir(folder);
	struct dirent *entry;
	char **paths = NULL;
	long count = 0;
	long capacity = 0;
	long index;
	int failed = 0;

	if (!directory)
	{
		printf("%s: cannot be read\n", folder);
		return 1;
	}
	while ((entry = readdir(directory)) != NULL)
	{
		size_t length = strlen(entry->d_name);
		struct stat status;
		char *path;

		if (length < 5 || strcasecmp(entry->d_name + length - 4, ".map"))
			continue;
		path = malloc(strlen(folder) + length + 2);
		sprintf(path, "%s/%s", folder, entry->d_name);
		if (stat(path, &status) || !S_ISREG(status.st_mode))
		{
			free(path);
			continue;
		}
		if (count == capacity)
		{
			capacity = capacity ? capacity * 2 : 64;
			paths = realloc(paths, capacity * sizeof(*paths));
		}
		paths[count++] = path;
	}
	closedir(directory);
	if (!count)
		printf("%s: no .map files in it\n", folder);
	qsort(paths, count, sizeof(*paths), map_name_compare);
	for (index = 0; index < count; index++)
	{
		if (map_file_check(paths[index]))
			failed = 1;
		free(paths[index]);
	}
	free(paths);

	return failed;
}

int main(int argc, char **argv)
{
	unsigned char *tag_cache;
	unsigned char *custom_edition_tag_cache;
	unsigned char *relocated_cache = NULL;
	int relocation = 0;
	int failed = 0;
	long fuzz_iterations = 0;
	unsigned long seed = 1;
	int argument;

	tag_cache = mmap((void *)TAG_CACHE_BASE_ADDRESS, TAG_CACHE_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (tag_cache != (unsigned char *)TAG_CACHE_BASE_ADDRESS)
	{
		fprintf(stderr, "map_validate: the tag cache's address (%08x) is taken\n", TAG_CACHE_BASE_ADDRESS);
		return 2;
	}
	custom_edition_tag_cache = mmap((void *)CUSTOM_EDITION_TAG_CACHE_ADDRESS, CUSTOM_EDITION_TAG_CACHE_BYTES,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
	if (custom_edition_tag_cache != (unsigned char *)CUSTOM_EDITION_TAG_CACHE_ADDRESS)
	{
		fprintf(stderr, "map_validate: the Custom Edition tag cache's address (%08lx) is taken\n",
			(unsigned long)CUSTOM_EDITION_TAG_CACHE_ADDRESS);
		return 2;
	}
	check_tag_cache = tag_cache;
	check_custom_edition_tag_cache = custom_edition_tag_cache;
	for (argument = 1; argument < argc; argument++)
	{

		if (!strcmp(argv[argument], "--strict"))
		{
			check_strict = 1;
			continue;
		}
		if (!strcmp(argv[argument], "--relocation"))
		{
			relocation = 1;
			check_relocation = 1;
			relocated_cache = mmap((void *)RELOCATED_TAG_CACHE_ADDRESS, TAG_CACHE_SIZE, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
			if (relocated_cache != (unsigned char *)RELOCATED_TAG_CACHE_ADDRESS)
			{
				fprintf(stderr, "map_validate: the relocated tag cache's address (%08x) is taken\n",
					RELOCATED_TAG_CACHE_ADDRESS);
				return 2;
			}
			check_relocated_cache = relocated_cache;
			continue;
		}
		if (!strcmp(argv[argument], "--quiet"))
		{
			quiet = 1;
			continue;
		}
		if (!strcmp(argv[argument], "--fuzz") && argument + 1 < argc)
		{
			fuzz_iterations = strtol(argv[++argument], NULL, 10);
			check_fuzz_iterations = fuzz_iterations;
			quiet = 1;
			signal(SIGSEGV, fuzz_signal);
			signal(SIGBUS, fuzz_signal);
			signal(SIGFPE, fuzz_signal);
			signal(SIGALRM, fuzz_signal);
			continue;
		}
		if (!strcmp(argv[argument], "--log") && argument + 1 < argc)
		{
			fuzz_log = fopen(argv[++argument], "w");
			continue;
		}
		if (!strcmp(argv[argument], "--seed") && argument + 1 < argc)
		{
			seed = strtoul(argv[++argument], NULL, 10);
			check_seed = seed;
			continue;
		}
		if (!strcmp(argv[argument], "--maps") && argument + 1 < argc)
		{
			resource_maps_folder = argv[++argument];
			continue;
		}
		{
			char translated[4096];
			char const *path = wsl_path(argv[argument], translated, sizeof(translated));
			struct stat status;

			if (stat(path, &status))
			{
				printf("%s: not found\n", path);
				failed = 1;
			}
			/* (a folder: every map in it) */
			else if (S_ISDIR(status.st_mode))
			{
				if (map_folder_check(path))
					failed = 1;
			}
			else if (map_file_check(path))
			{
				failed = 1;
			}
		}
	}

	if (relocation)
	{
		printf("%ld maps relocated: %ld passed, %ld with problems\n", relocation_maps,
			relocation_maps - relocation_failed_maps, relocation_failed_maps);
	}

	return failed;
}
