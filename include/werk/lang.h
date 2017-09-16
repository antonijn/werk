#ifndef WERK_LANG_H
#define WERK_LANG_H

#include <stddef.h>

typedef struct lang {
	const char *name;
	const char *version;
} Lang;

/*
 * Detect language based on file name and first line (possible shebang)
 */
void lang_detect(const char *file_name, const char *l1, Lang *result);

#endif
