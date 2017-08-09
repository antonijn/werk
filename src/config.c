#include "configfile.h"
#include "config.h"
#include "sparsef.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool get_indentation(ConfigFile *cfile, const char *key, int *value);

void
config_load_defaults(Config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->colors.insert.fg = (RGB){ 74, 74, 74 },
	cfg->colors.insert.inv = (RGB){ 150, 150, 150 },
	cfg->colors.insert.bg = (RGB){ 255, 250, 210 },
	cfg->colors.insert.sel = (RGB){ 255, 253, 239 };
	cfg->colors.insert.line_numbers_bg = (RGB){ 230, 225, 188 };
	cfg->colors.select.fg = (RGB){ 74, 74, 74 },
	cfg->colors.select.bg = (RGB){ 209, 252, 255 },
	cfg->colors.select.inv = (RGB){ 150, 150, 150 },
	cfg->colors.select.sel = (RGB){ 239, 254, 255 };
	cfg->colors.select.line_numbers_bg = (RGB){ 188, 227, 230 };
	cfg->editor.line_numbers = true;
	cfg->editor.show_tabs = cfg->editor.show_spaces = cfg->editor.show_newlines = false;
	cfg->text.indentation = 0;
	cfg->text.tab_width = 8;
}

void
config_load(Config *conf)
{
	config_load_defaults(conf);

	char *home_dir = getenv("HOME");
	if (!home_dir) {
		fprintf(stderr, "warning: could not load configuration file: no $HOME!\n");
		return;
	}
	size_t home_dir_len = strlen(home_dir);

	char *config_rel = "/.config/werk/user.conf";
	size_t config_rel_len = strlen(config_rel);

	char config_path[home_dir_len + config_rel_len + 1];
	memcpy(config_path, home_dir, home_dir_len);
	memcpy(config_path + home_dir_len, config_rel, config_rel_len + 1);

	ConfigFile *cfile = config_read_path(config_path);
	if (!cfile) {
		fprintf(stderr,
		        "warning: could not find configuration file at ``%s''\n",
		        config_path);
		return;
	}

	int line;
	const char *msg;
	if (config_get_error(cfile, &line, &msg)) {
		fprintf(stderr, "error reading user.conf: %d: %s\n", line, msg);
		return;
	}

	const char *val;

	config_get_color(cfile, "editor.colors.select.foreground", &conf->colors.select.fg);
	config_get_color(cfile, "editor.colors.select.invisibles", &conf->colors.select.inv);
	config_get_color(cfile, "editor.colors.select.background", &conf->colors.select.bg);
	config_get_color(cfile, "editor.colors.select.selection", &conf->colors.select.sel);
	config_get_color(cfile,
	                 "editor.colors.select.line-numbers.background",
	                 &conf->colors.select.line_numbers_bg);
	config_get_color(cfile, "editor.colors.insert.foreground", &conf->colors.insert.fg);
	config_get_color(cfile, "editor.colors.insert.invisibles", &conf->colors.insert.inv);
	config_get_color(cfile, "editor.colors.insert.background", &conf->colors.insert.bg);
	config_get_color(cfile, "editor.colors.insert.selection", &conf->colors.insert.sel);
	config_get_color(cfile,
	                 "editor.colors.insert.line-numbers.background",
	                 &conf->colors.insert.line_numbers_bg);
	config_getb(cfile, "editor.line-numbers", &conf->editor.line_numbers);
	static const char *invs_names[] = { "tabs", "spaces", "newlines", NULL };
	bool *invs_vals[] = { &conf->editor.show_tabs, &conf->editor.show_spaces, &conf->editor.show_newlines };
	config_get_switches(cfile, "editor.show-invisibles", invs_names, invs_vals);
	config_geti(cfile, "text.tab-width", &conf->text.tab_width);
	get_indentation(cfile, "text.indentation", &conf->text.indentation);
}

static bool
get_indentation(ConfigFile *cfile, const char *key, int *value)
{
	const char *str_val;
	int line;
	if (!config_get(cfile, key, &str_val, &line))
		return false;

	if (!sparsef(str_val, "%d spaces", value)) {
		if (*value <= 0) {
			fprintf(stderr,
				"%s line %d: invalid: `%s'\n",
				config_get_filename(cfile),
				line,
				str_val);

			return false;
		}
		return true;
	}

	if (sparsef(str_val, "tabs")) {
		fprintf(stderr,
		        "%s line %d: expected `ǸUM spaces' or `tabs', not ``%s''\n",
		        config_get_filename(cfile),
			line,
		        str_val);

		return false;
	}

	*value = 0;
	return true;
}
