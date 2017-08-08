#ifndef CONFIGFILE_H
#define CONFIGFILE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "win.h"

typedef struct config_file ConfigFile;

ConfigFile *config_read_path(const char *path);
ConfigFile *config_read(FILE *file, const char *filename);

const char *config_get_filename(ConfigFile *conf);

bool config_get_error(ConfigFile *conf, int *line, const char **msg);

bool config_get(ConfigFile *conf, const char *key, const char **value, int *line);
bool config_gets(ConfigFile *conf, const char *key, const char **value);
bool config_geti(ConfigFile *conf, const char *key, int *value);
bool config_geti64(ConfigFile *conf, const char *key, int64_t *value);
bool config_getf32(ConfigFile *conf, const char *key, float *value);
bool config_getf64(ConfigFile *conf, const char *key, double *value);
bool config_getb(ConfigFile *conf, const char *key, bool *value);
/*bool config_getd(ConfigFile *conf, const char *key, _Decimal128 *value);*/

/*
 * key = rgb(num, num, num)
 * TODO: key = hsv(num, num, num)
 */
bool config_get_color(ConfigFile *conf, const char *key, RGB *value);
bool config_get_switches(ConfigFile *conf,
                         const char *key,
                         const char *names[],
                         bool *values[]);

void config_destroy(ConfigFile *conf);

#endif
