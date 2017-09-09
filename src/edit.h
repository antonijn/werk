#ifndef EDIT_H
#define EDIT_H

#include "cfg.h"
#include "cfgprs.h"
#include "gap.h"
#include "rbtree.h"
#include "win.h"

/* instance of the editor (possibly containing multiple buffers) */
typedef struct werk_instance WerkInstance;

typedef struct mode Mode;

typedef struct buffer Buffer;

/* used to keep track of locations in the gap buffer */
typedef struct {
	/*
	 * THERE ARE TWO TYPES OF BUFFER MARKERS: left-to-right and
	 * right-to-left.
	 *
	 * The absolute offset of a marker within a buffer of size N is:
	 *
	 *   rtol * N + offset
	 *
	 * The absolute line number in a file with L lines:
	 *
	 *   rtol * (L - 1) + line + 1
	 *
	 * Right-to-left markers are considered "greater than" (>)
	 * left-to-right markers.
	 */

	int rtol;

	long offset;
	int line, col;
} BufferMarker;

int cmp_buffer_markers(const BufferMarker *a, const BufferMarker *b);
int cmp_buffer_markers_rbtree(struct rb_tree *tree, struct rb_node *a, struct rb_node *b);

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

	BufferMarker buf_start, buf_end;

	/*
	 * Markers that come before the "high marker" (greatest of
	 * sel_start and sel_finish) are in lo_markers, the remaining
	 * in hi_markers.
	 *
	 * sel_start and sel_finish themselves are never in either.
	 *
	 * The high markers are all right-to-left, the low markers all
	 * left-to-right.
	 */
	struct rb_tree *lo_markers, *hi_markers;

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

gbuf_offs marker_offs(Buffer *buf, const BufferMarker *marker);

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
 * if (cmp_buffer_markers(a, b) <= 0) {
 *         *left = a;
 *         *right = b;
 * } else {
 *         *left = b;
 *         *right = a;
 * }
 */
void marker_sort_pair(BufferMarker *a,
                      BufferMarker *b,
                      BufferMarker **left,
                      BufferMarker **right);

/*
 * Copies given start and finish markers to set new selection.
 * Pass either argument `NULL' to indicate no change.
 */
void buf_set_sel(Buffer *buf, const BufferMarker *start, const BufferMarker *finish);

/*
 * Pipe buffer selection through command `str'.
 * See also: gbuf_pipe()
 */
void buf_pipe_selection(Buffer *buf, const char *str);

/*
 * Return a pointer to sel_start if sel_start > sel_finish, or a
 * pointer to sel_finish otherwise.
 */
BufferMarker *buf_high_selection(Buffer *buf);

/*
 * Insert string at end of selection. Moves end of selection forwards
 * accordingly.
 *
 * Requires a degenerate selection.
 *
 * buf_insert_input_string() Replaces tabs with spaces if so configured.
 * buf_insert_text()         Does not.
 */
void buf_insert_input_string(Buffer *buf, const char *input, size_t len);
void buf_insert_text(Buffer *buf, const char *input, size_t len);

void buf_delete_selection(Buffer *buf);

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
