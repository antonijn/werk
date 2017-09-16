#ifndef CONF_FILE_H
#define CONF_FILE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <werk/ui/win.h>

typedef struct config_reader ConfigReader;
typedef void (*option_callback)(ConfigReader *conf, const char *value, void *udata);

ConfigReader *config_init(void);

/*
 * Report errors to stderr in uniform format.
 */
void config_report(ConfigReader *conf, const char *fmt, ...);

/*
 * Allocate memory which is alive until `conf' is destroyed.
 */
void *config_alloc(ConfigReader *conf, size_t size);

/*
 * The conf_add_opt*() functions all expect the `key' argument to be of
 * static storage duration.
 */

/*
 * Add an option with name `key' to `conf', and bind callback `opt' to
 * it. This callback is called when the option is read in a config
 * file.
 *
 * The `udata' parameter is passed as the last argument when invoking
 * the callback.
 */
void config_add_opt(ConfigReader *conf, const char *key, option_callback opt, void *udata);

/*
 * The config_add_opt_*() functions are helper functions around
 * config_add_opt().
 */

/*
 * Add string option.
 * NOTE: `*value' must be free()'d manually.
 */
void config_add_opt_s(ConfigReader *conf, const char *key, const char **value);

void config_add_opt_i(ConfigReader *conf, const char *key, int *value);

void config_add_opt_i32(ConfigReader *conf, const char *key, int32_t *value);

void config_add_opt_i64(ConfigReader *conf, const char *key, int64_t *value);

void config_add_opt_f32(ConfigReader *conf, const char *key, float *value);

void config_add_opt_f64(ConfigReader *conf, const char *key, double *value);

void config_add_opt_b(ConfigReader *conf, const char *key, bool *value);

/*
 * Accepts hsv and rgb strings in the configuration.
 */
void config_add_opt_color(ConfigReader *conf, const char *key, RGB *value);

/*
 * Add an option whose value can be a set of flags. It can also be
 * thought of as being able to specify individual members of an
 * unordered set.
 *
 * An example is the "editor.show-invisibles" option, which allows
 * you to select multiple categories: "tabs", "spaces", "newlines".
 *
 * `names' is expected to be an array of static storage duration,
 * whereas `values' may have automatic storage duration. `names' must be
 * null-terminated.
 *
 * Example:

	static const char * const invs_names[] = { "tabs", "spaces", "newlines", NULL };
	bool *invs_vals[] = { &config.show_tabs, &config.show_spaces, &config.show_newlines };
	config_add_opt_flags(conf, "editor.show-invisibles", invs_names, invs_vals);

 */
void config_add_opt_flags(ConfigReader *conf,
                          const char *key,
                          const char *names[],
                          bool *values[]);

/*
 * Read configuration file at `path'.
 */
void config_read_file(ConfigReader *conf, const char *path);

/*
 * Release all resources used by `conf'.
 */
void config_destroy(ConfigReader *conf);

#endif
