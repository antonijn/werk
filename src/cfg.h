#ifndef CFG_H
#define CFG_H

#include "win.h"

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
void config_load(Config *cfg);

#endif
