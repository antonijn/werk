#include "configfile.h"
#include "config.h"
#include "sparsef.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void config_load_file(Config *cfg, const char *path);
static bool get_indentation(ConfigFile *cfile, const char *key, int *value);
static bool get_newline(ConfigFile *cfile, const char *key, const char **value);

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
	cfg->editor.tab_width = 8;
#ifdef _WIN32
	cfg->text.default_newline = "\r\n";
#else
	cfg->text.default_newline = "\n";
#endif
	cfg->text.indentation = 0;
}

static void
config_load_file(Config *conf, const char *path)
{
	ConfigFile *cfile = config_read_path(path);
	if (!cfile)
		return;

	int line;
	const char *msg;
	if (config_get_error(cfile, &line, &msg)) {
		fprintf(stderr, "`%s': %d: %s\n", path, line, msg);
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
	config_geti(cfile, "editor.tab-width", &conf->editor.tab_width);
	static const char *invs_names[] = { "tabs", "spaces", "newlines", NULL };
	bool *invs_vals[] = {
		&conf->editor.show_tabs,
		&conf->editor.show_spaces,
		&conf->editor.show_newlines
	};
	config_get_switches(cfile, "editor.show-invisibles", invs_names, invs_vals);
	get_indentation(cfile, "text.indentation", &conf->text.indentation);
	get_newline(cfile, "text.default-newline", &conf->text.default_newline);

	config_destroy(cfile);
}

void
config_load(Config *conf)
{
	int line;
	const char *msg;

	config_load_defaults(conf);

	/* PREFIX is set by Makefile */
	static const char *const sys_cfg_path = PREFIX "/share/werk/system.conf";

	config_load_file(conf, sys_cfg_path);

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

	config_load_file(conf, config_path);
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

static bool
get_newline(ConfigFile *cfile, const char *key, const char **value)
{
	const char *str_val;
	int line;
	if (!config_get(cfile, key, &str_val, &line))
		return false;

	if (!sparsef(str_val, "unix")) {
		*value = "\n";
		return true;
	}

	if (!sparsef(str_val, "dos")) {
		*value = "\r\n";
		return false;
	}

	int unival;
	if (!sparsef(str_val, "U+%x", &unival)) {
		switch (unival) {
		case 0x000A: /* LF */
			*value = "\n";
			break;

		case 0x000B: /* VT */
			*value = "\v";
			break;

		case 0x000C: /* FF */
			*value = "\f";
			break;

		case 0x000D: /* CR */
			*value = "\r";
			break;

		case 0x0085: /* NEL */
			*value = "\xC2\x85";
			break;

		case 0x2028: /* LS */
			*value = u8"\u2028";
			break;

		case 0x2029: /* PS */
			*value = u8"\u2029";
			break;

		default:
			fprintf(stderr,
				"%s line %d: unknown newline `U+%x'\n",
				config_get_filename(cfile),
				line,
				(unsigned)unival);
			return false;

		}

		return true;
	}

	fprintf(stderr,
		"%s line %d: expected `unix', `dos' or `U+XXXX'\n",
		config_get_filename(cfile),
		line);

	return false;
}
