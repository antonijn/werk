#ifndef CONFIG_H
#define CONFIG_H

#include "win.h"

#include <stdbool.h>

typedef struct {
	RGB bg, fg, inv, sel, line_numbers_bg;
} ColorSet;

typedef struct {
	struct {
		ColorSet insert, select;
	} colors;

	struct {
		bool line_numbers;
		bool show_newlines, show_spaces, show_tabs;
	} editor;

	struct {
		int indentation; /* 0 if using tabs */
		int tab_width;
	} text;
} Config;

void config_load_defaults(Config *cfg);
void config_load(Config *cfg);

#endif
