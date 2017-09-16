#ifndef CONF_APP_H
#define CONF_APP_H

#include <werk/conf/file.h>
#include <werk/ui/win.h>

#include <stdbool.h>

typedef struct {
	RGB bg, fg, inv, sel, line_numbers_bg;
} ColorSet;

typedef struct {
	struct {
		/* insert-mode and selection-mode color schemes */
		ColorSet insert, select;
	} colors;

	struct {
		/* whether to show line numbers or not */
		bool line_numbers;
		/* whether to show class of invisibles */
		bool show_newlines, show_spaces, show_tabs;
		/* whether to show scroll bar */
		bool scroll_bar;
		/* number of spaces displayed per tab */
		int tab_width;
	} editor;

	struct {
		/* number of spaces to insert per tab keypress, or zero
		 * if inserting tab characters (default) */
		int indentation;
		/* newline to use in newly opened files
		 * "\r\n" on Windows, "\n" on everything else */
		const char *default_newline;
	} text;
} Config;

/*
 * Load ~/.config/werk/user.conf and /etc/werk/system.conf into
 * given configuration struct.
 */
void config_load(Config *cfg, ConfigReader *rdr);

#endif
