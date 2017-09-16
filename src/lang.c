#include <werk/lang.h>
#include <assert.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*
 * Both functions return 0 on success, -1 if inconclusive
 */
static int lang_detect_basename(const char *basename, Lang *result);
static int lang_detect_shebang(const char *l1, Lang *result);

void
lang_detect(const char *file_name, const char *l1, Lang *result)
{
	memset(result, 0, sizeof(*result));

	char *fn_cpy = strdup(file_name);
	if (!fn_cpy)
		return;

	char *bname = basename(fn_cpy);

	lang_detect_basename(bname, result);
	free(fn_cpy);

	lang_detect_shebang(l1, result);
}

#ifndef NDEBUG
/* used in an assert() in lang_detect_basename() and lang_detect_shebang() */
static bool
map_sorted(const char *suffix_map[][2], int msize)
{
	for (int nxt = 1; nxt < msize; ++nxt)
		if (strcmp(suffix_map[nxt - 1][0], suffix_map[nxt][0]) >= 0)
			return false;

	return true;
}
#endif

static int
lang_detect_basename(const char *basename, Lang *result)
{
	/* Does not check anything but suffix as of yet */

	const char *suffix = strrchr(basename, '.');
	if (!suffix)
		return -1;

	++suffix;

	static const char *suffix_map[][2] = {
		{ "C", "cpp" },
		{ "H", "cpp" },
		{ "bash", "bash" },
		{ "c", "c" },
		{ "cpp", "cpp" },
		{ "cs", "cs" },
		{ "go", "go" },
		{ "h", "c" },
		{ "hpp", "cpp" },
		{ "hs", "hs" },
		{ "java", "java" },
		{ "lua", "lua" },
		{ "py", "py" },
		{ "run", "sh" },
		{ "sh", "sh" },
	};

	int nsuffices = sizeof(suffix_map) / sizeof(suffix_map[0]);

	assert(map_sorted(suffix_map, nsuffices));

	/* binary search */
	int min = 0;
	int max = nsuffices;
	while (min != max) {
		int pivot = (max + min) / 2;
		const char *map_sfx = suffix_map[pivot][0];
		int cmp = strcmp(suffix, map_sfx);
		if (cmp < 0) {
			max = pivot;
		} else if (cmp > 0) {
			min = pivot + 1;
		} else {
			result->name = suffix_map[pivot][1];
			return 0;
		}
	}

	return -1;
}

static int
lang_detect_shebang(const char *l1, Lang *result)
{
	static const char *shebang_map[][2] = {
		{ "#!/bin/bash", "bash" },
		{ "#!/bin/python", "py" },
		{ "#!/bin/sh", "sh" },
		{ "#!/usr/bin/env python", "py" },
		{ "#!/usr/bin/python", "py" },
	};

	int nshebangs = sizeof(shebang_map) / sizeof(shebang_map[0]);

	assert(map_sorted(shebang_map, nshebangs));

	/* binary search */
	int min = 0;
	int max = nshebangs;
	while (min != max) {
		int pivot = (max + min) / 2;
		const char *map_shb = shebang_map[pivot][0];
		int cmp = strncmp(l1, map_shb, strlen(map_shb));
		if (cmp < 0) {
			max = pivot;
		} else if (cmp > 0) {
			min = pivot + 1;
		} else {
			result->name = shebang_map[pivot][1];
			return 0;
		}
	}

	return -1;
}
