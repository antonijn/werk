#include "configfile.h"
#include "config.h"
#include "sparsef.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void indentation_callback(ConfigReader *rdr, const char *str, void *udata);
static void newline_callback(ConfigReader *rdr, const char *str, void *udata);

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
config_setup_reader(Config *conf, ConfigReader *rdr)
{
	config_add_opt_color(rdr, "editor.colors.select.foreground", &conf->colors.select.fg);
	config_add_opt_color(rdr, "editor.colors.select.invisibles", &conf->colors.select.inv);
	config_add_opt_color(rdr, "editor.colors.select.background", &conf->colors.select.bg);
	config_add_opt_color(rdr, "editor.colors.select.selection", &conf->colors.select.sel);
	config_add_opt_color(rdr,
	                     "editor.colors.select.line-numbers.background",
	                     &conf->colors.select.line_numbers_bg);
	config_add_opt_color(rdr, "editor.colors.insert.foreground", &conf->colors.insert.fg);
	config_add_opt_color(rdr, "editor.colors.insert.invisibles", &conf->colors.insert.inv);
	config_add_opt_color(rdr, "editor.colors.insert.background", &conf->colors.insert.bg);
	config_add_opt_color(rdr, "editor.colors.insert.selection", &conf->colors.insert.sel);
	config_add_opt_color(rdr,
	                     "editor.colors.insert.line-numbers.background",
	                     &conf->colors.insert.line_numbers_bg);
	config_add_opt_b(rdr, "editor.line-numbers", &conf->editor.line_numbers);
	config_add_opt_i(rdr, "editor.tab-width", &conf->editor.tab_width);
	static const char *invs_names[] = { "tabs", "spaces", "newlines", NULL };
	bool *invs_vals[] = {
		&conf->editor.show_tabs,
		&conf->editor.show_spaces,
		&conf->editor.show_newlines
	};
	config_add_opt_flags(rdr, "editor.show-invisibles", invs_names, invs_vals);
	config_add_opt(rdr, "text.indentation", indentation_callback, &conf->text.indentation);
	config_add_opt(rdr, "text.default-newline", newline_callback, &conf->text.default_newline);
}

void
config_load(Config *conf)
{
	config_load_defaults(conf);

	ConfigReader *rdr = config_init();
	config_setup_reader(conf, rdr);

	static const char *const sys_cfg_path = "/etc/werk/system.conf";

	config_read_file(rdr, sys_cfg_path);

	char *home_dir = getenv("HOME");
	if (home_dir) {
		size_t home_dir_len = strlen(home_dir);

		char *config_rel = "/.config/werk/user.conf";
		size_t config_rel_len = strlen(config_rel);

		char config_path[home_dir_len + config_rel_len + 1];
		memcpy(config_path, home_dir, home_dir_len);
		memcpy(config_path + home_dir_len, config_rel, config_rel_len + 1);

		config_read_file(rdr, config_path);
	} else {
		fprintf(stderr, "warning: could not load configuration file: no $HOME!\n");
	}

	config_destroy(rdr);
}

static void
indentation_callback(ConfigReader *rdr, const char *str, void *udata)
{
	int *value = udata;

	if (!sparsef(str, "%d spaces", value)) {
		if (*value <= 0)
			config_report(rdr, "invalid: `%s'\n", str);
		return;
	}

	if (sparsef(str, "tabs")) {
		config_report(rdr, "expected `NUM spaces' or `tabs', not ``%s''\n", str);
		return;
	}

	*value = 0;
}

static void
newline_callback(ConfigReader *rdr, const char *str, void *udata)
{
	const char **value = udata;

	if (!sparsef(str, "unix")) {
		*value = "\n";
		return;
	}

	if (!sparsef(str, "dos")) {
		*value = "\r\n";
		return;
	}

	int unival;
	if (!sparsef(str, "U+%x", &unival)) {
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
			config_report(rdr, "unknown newline `U+%x'\n", (unsigned)unival);
			break;
		}

		return;
	}

	config_report(rdr, "%s line %d: expected `unix', `dos' or `U+XXXX'\n");
}
