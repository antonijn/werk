#if !defined(HAS_NCURSES) && !defined(HAS_GTK)
#error Must compile with at least one of HAS_NCURSES and HAS_GTK
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "edit.h"
#ifdef HAS_NCURSES
#include "ncurses.h"
#endif
#ifdef HAS_GTK
#include <gtk/gtk.h>
#include "gtk.h"
#endif

static const char *ui_mode =
#ifdef HAS_GTK
	"gui";
#else
	"term";
#endif

static const char **filenames;
static size_t filename_count;

static void usage(FILE *f)
{
	fputs("usage: te [-i <ui mode>] <file>...\n", f);
}

static int validate_ui_mode(void)
{
	const char *available[] = {
#ifdef HAS_GTK
		"gui",
#endif
#ifdef HAS_NCURSES
		"term",
#endif
	};

	size_t navail = sizeof(available) / sizeof(available[0]);

	for (int i = 0; i < navail; ++i)
		if (!strcmp(ui_mode, available[i]))
			return 0;
		
	/* Invalid */
	fprintf(stderr, "invalid ui mode -- %s\navailable are:\n", ui_mode);
	for (int i = 0; i < navail; ++i)
		fprintf(stderr, " * %s\n", available[i]);
	return -1;
}

/*
 * Return -1 on error, 0 on success, and 1 if the application should exit
 * cleanly
 */
static int parse_opt(int *i, int argc, char **argv)
{
	const char *arg = argv[*i];

	if (!strncmp(arg, "--", 2)) {
		if (!strcmp(arg, "--help")) {
			usage(stdout);
			return 1;
		}

		fprintf(stderr, "unrecognized option -- %s\n", arg);
		usage(stderr);
		return -1;
	}

	char ch;
	while (ch = *++arg) {
		switch (ch) {
		case 'i':
			/* Interface selection */
			++*i;
			if (*i >= argc) {
				fprintf(stderr, "-i needs an argument\n");
				usage(stderr);
				return -1;
			}

			ui_mode = argv[*i];
			if (validate_ui_mode())
				return -1;
			break;
		default:
			fprintf(stderr, "unrecognized option -- -%c\n", ch);
			usage(stderr);
			return -1;
		}
	}

	return 0;
}

static int parse_args(int argc, char **argv)
{
	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] == '-') {
			if (parse_opt(&i, argc, argv))
				return -1;
		} else {
			filenames[filename_count++] = argv[i];
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	int ecode = 0;

	filenames = calloc(argc - 1, sizeof(const char *));
	filename_count = 0;

#ifdef HAS_GTK
	/* This has to be done before parse_args(), as gtk_init() may
	 * remove args from argv */
	bool gtk_works = gtk_init_check(&argc, &argv);
#endif
	int status = parse_args(argc, argv);
	if (status) {
		ecode = status < 0;
		goto stop;
	}

#ifdef HAS_GTK
	if (!strcmp(ui_mode, "gui") && !gtk_works) {
		fprintf(stderr, "warning: failed to initialize gtk, using terminal mode instead\n");
		ui_mode = "term";
	}

	if (!strcmp(ui_mode, "gui")) {
		if (werk_gtk_main(filenames, filename_count))
			ecode = 1;
	} else
#endif
#ifdef HAS_NCURSES
	if (!strcmp(ui_mode, "term")) {
		if (werk_ncurses_main(filenames, filename_count))
			ecode = 1;
	} else
#endif
	{
		fprintf(stderr, "internal error: invalid ui mode -- %s\n", ui_mode);
		ecode = 1;
	}

stop:
	free(filenames);
	return ecode;
}
