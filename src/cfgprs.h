#ifndef CFGPRS_H
#define CFGPRS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "win.h"

typedef struct config_reader ConfigReader;
typedef void (*option_callback)(ConfigReader *conf, const char *value, void *udata);

ConfigReader *config_init(void);

void config_report(ConfigReader *conf, const char *fmt, ...);

void *config_alloc(ConfigReader *conf, size_t size);

/*
 * The following functions all expect the `key' argument to be of static
 * storage duration.
 */

void config_add_opt(ConfigReader *conf, const char *key, option_callback opt, void *udata);

/*
 * value must be freed
 */
void config_add_opt_s(ConfigReader *conf, const char *key, const char **value);

void config_add_opt_i(ConfigReader *conf, const char *key, int *value);

void config_add_opt_i32(ConfigReader *conf, const char *key, int32_t *value);

void config_add_opt_i64(ConfigReader *conf, const char *key, int64_t *value);

void config_add_opt_f32(ConfigReader *conf, const char *key, float *value);

void config_add_opt_f64(ConfigReader *conf, const char *key, double *value);

void config_add_opt_b(ConfigReader *conf, const char *key, bool *value);

void config_add_opt_color(ConfigReader *conf, const char *key, RGB *value);

void config_add_opt_flags(ConfigReader *conf,
                          const char *key,
                          const char *names[],
                          bool *values[]);

void config_read_file(ConfigReader *conf, const char *path);

void config_destroy(ConfigReader *conf);

#endif
