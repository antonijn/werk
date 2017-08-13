#ifndef EDIT_H
#define EDIT_H

#include "cfg.h"
#include "cfgprs.h"
#include "gap.h"
#include "win.h"

/* instance of the editor (possibly containing multiple buffers) */
typedef struct werk_instance WerkInstance;

typedef struct mode Mode;

typedef struct buffer Buffer;

/* used to keep track of locations in the gap buffer */
typedef struct {
	gbuf_offs offset;
	int line, col;
} BufferMarker;

struct mode {
	void (*on_key_press)(Buffer *buf, Mode *mode, KeyMods mods, const char *input, size_t len);
	void (*on_enter_press)(Buffer *buf, Mode *mode, KeyMods mods);
	void (*on_backspace_press)(Buffer *buf, Mode *mode, KeyMods mods);
	void (*on_delete_press)(Buffer *buf, Mode *mode, KeyMods mods);
	void (*destroy)(Mode *mode);

	ColorSet colors;

	Mode *below;
};

struct buffer {
	/* Cursor is guaranteed to be at grapheme boundaries */
	GapBuf gbuf;

	/* Viewport top left */
	gbuf_offs vp_first_line;       /* first char in first line in viewport */
	int vp_orig_line, vp_orig_col; /* first viewport line, column */

	/* Selection markers */
	BufferMarker sel_start, sel_finish;

	/* Buffer-specific newline character */
	const char *eol;
	size_t eol_size;

	const char *filename;

	Mode *mode;

	struct {
		bool active;
		int w; /* width */
		GapBuf gbuf;
	} dialog;

	/* part of ring queue */
	struct buffer *next, *prev;

	WerkInstance *werk;
};

/* Returns true for all Unicode newlines */
bool grapheme_is_newline(const char *str, size_t len);

/* CJK characters are typically width 2, tabs are variable-width */
int grapheme_width(const char *str, size_t n, int col, int tab_width);

/*
 * Calculate column number of character identifier by given offset.
 */
int grapheme_column(Buffer *buf, gbuf_offs ofs);

/*
 * Moves `marker' to next grapheme, stores the current grapheme in `str'
 * and its size in `size'.
 */
int marker_next(Buffer *buf, const char **str, size_t *size, BufferMarker *marker);
/*
 * Moves `marker' to previous grapheme, stores the current grapheme in
 * `str' and its size in `size'.
 */
int marker_prev(Buffer *buf, const char **str, size_t *size, BufferMarker *marker);

/*
 * Move `marker' to start of next line.
 */
void marker_next_line(Buffer *buf, BufferMarker *marker);
/*
 * Move `marker' to start of current line.
 */
void marker_start_of_line(Buffer *buf, BufferMarker *marker);
/*
 * Move `marker' to last character before newline.
 */
void marker_end_of_line(Buffer *buf, BufferMarker *marker);

/*
 * Swap `a' and `b' if `a' > `b'.
 */
void marker_sort_pair(BufferMarker *a, BufferMarker *b);

/*
 * Pipe buffer selection through command `str'.
 * See also: gbuf_pipe()
 */
void buf_pipe_selection(Buffer *buf, const char *str);

/*
 * Insert string at end of selection.
 */
void buf_insert_input_string(Buffer *buf, const char *input, size_t len);

void buf_delete_range(Buffer *buf, BufferMarker left, BufferMarker right);

/*
 * Move cursor by `delta' graphemes. Positive `delta' means movement
 * to the right, negative to the left.
 */
void buf_move_cursor(Buffer *buf, int delta, bool extend);

/*
 * Save `buf' to `buf->filename' if possible.
 */
int buf_save(Buffer *buf);

/*
 * Return whether selection is merely a cursor.
 */
bool buf_is_selection_degenerate(Buffer *buf);

/*
 * Activate command dialog.
 */
void show_cmd_dialog(Buffer *buf);

struct werk_instance {
	Config cfg;

	/* ring queue */
	Buffer *active_buf;
};

void werk_init(Window *win, const char **filenames, int num_filenames, ConfigReader *rdr);

#endif
