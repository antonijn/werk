#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unictype.h>
#include <unigbrk.h>
#include <unistr.h>
#include <uniwidth.h>
#include "cfg.h"
#include "edit.h"
#include "gap.h"

#define CMD_DIALOG_WIDTH 30

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

struct werk_instance {
	Config cfg;

	/* ring queue */
	Buffer *active_buf;
};

/*  _            __  __             _             _
 * | |__  _   _ / _|/ _| ___ _ __  | | ___   __ _(_) ___
 * | '_ \| | | | |_| |_ / _ \ '__| | |/ _ \ / _` | |/ __|
 * | |_) | |_| |  _|  _|  __/ |    | | (_) | (_| | | (__
 * |_.__/ \__,_|_| |_|  \___|_|    |_|\___/ \__, |_|\___|
 *                                          |___/
 */

/* Returns true for all Unicode newlines */
static bool grapheme_is_newline(const char *str, size_t len);

/* CJK characters are typically width 2, tabs are variable-width */
static int grapheme_width(const char *str, size_t n, int col, int tab_width);

/*
 * Calculate column number of character identifier by given offset.
 */
static int grapheme_column(Buffer *buf, gbuf_offs ofs);

/*
 * Moves `marker' to next grapheme, stores the current grapheme in `str'
 * and its size in `size'.
 */
static int marker_next(Buffer *buf, const char **str, size_t *size, BufferMarker *marker);
/*
 * Moves `marker' to previous grapheme, stores the current grapheme in
 * `str' and its size in `size'.
 */
static int marker_prev(Buffer *buf, const char **str, size_t *size, BufferMarker *marker);

/*
 * Move `marker' to start of next line.
 */
static void marker_next_line(Buffer *buf, BufferMarker *marker);
/*
 * Move `marker' to start of current line.
 */
static void marker_start_of_line(Buffer *buf, BufferMarker *marker);
/*
 * Move `marker' to last character before newline.
 */
static void marker_end_of_line(Buffer *buf, BufferMarker *marker);

/*
 * Swap `a' and `b' if `a' > `b'.
 */
static void marker_sort_pair(BufferMarker *a, BufferMarker *b);


/*
 * Initialize empty buffer.
 */
static void buf_init(Buffer *buf, WerkInstance *werk);

/*
 * Initialize command dialog of buffer. This is called by buf_init().
 */
static void cmd_dialog_init(Buffer *buf);

/*
 * Destroy all resources associated with buffer.
 * This should probably not be called directy, but rather called
 * implicitly by werk_remove_buffer().
 */
static void buf_destroy(Buffer *buf);

/*
 * Read filename into buffer. On failure resets text to what it was
 * before the call.
 */
static int buf_read(Buffer *buf, const char *filename);

/*
 * Sets `buf->eol' and `buf->eol_size' to the value of the first newline
 * found in the buffer. Otherwise it uses "text.default-newline" from
 * the configuration.
 */
static void buf_detect_newline(Buffer *buf);

/*
 * Return whether selection is merely a cursor.
 */
static bool buf_is_selection_degenerate(Buffer *buf);

/*
 * Move cursor by `delta' graphemes. Positive `delta' means movement
 * to the right, negative to the left.
 */
static void buf_move_cursor(Buffer *buf, int delta, bool extend);

/*
 * Calculate viewport origin to make sure that the end of the selection
 * is visible.
 */
static void buf_calc_vp_origin(Buffer *buf, int wlines, int hlines);

/*
 * Get size of the viewport, taking the line number columns into
 * account.
 */
static void buf_get_viewport(Buffer *buf, int *vw, int *vh, int wlines, int hlines);

/*
 * Draw single grapheme. Used by buf_draw_line().
 */
static void buf_draw_grapheme(Buffer *buf,
                              Drawer *d,
		              int x, int y, int w,
                              const char *graph, size_t graph_len);

/*
 * Draw single line. Used by buf_draw().
 * `logic_x' is the translation of the viewport to the left in
 * graphemes (number of columns).
 */
static bool buf_draw_line(Buffer *buf,
                          Drawer *d,
                          BufferMarker marker,
                          int vw, int vh,
                          int logic_x,
                          int x, int y);

/*
 * Draw the buffer text and line numbers.
 */
static void buf_draw(Buffer *buf, Drawer *d, int wlines, int hlines);

/*
 * Pipe buffer selection through command `str'.
 * See also: gbuf_pipe()
 */
static void buf_pipe_selection(Buffer *buf, const char *str);

/*
 * Insert string at end of selection.
 */
static void buf_insert_input_string(Buffer *buf, const char *input, size_t len);

/*
 * Save `buf' to `buf->filename' if possible.
 */
static int buf_save(Buffer *buf);

/*
 * Convert line and column number to position on screen.
 */
static void buf_get_line_col_position(Buffer *buf, int l, int c, int w, int h, int *x, int *y);

/*
 * Handle key press for command dialog.
 */
static void cmd_dialog_on_key_press(Buffer *buf, KeyMods mods, const char *input, size_t len);

/*
 * Remove last character from command dialog.
 */
static void cmd_dialog_on_backspace_press(Buffer *buf, KeyMods mods);

/*
 * Initiate buf_pipe_selection().
 */
static void cmd_dialog_on_enter_press(Buffer *buf, KeyMods mods);

/*
 * Draw command dialog onto the screen (if it's active).
 */
static void draw_cmd_dialog(Buffer *buf, Drawer *d, int ww, int wh);

/*
 * Activate command dialog.
 */
static void show_cmd_dialog(Buffer *buf);


/*
 * Pop last mode from mode stack.
 * Release resources used by the mode.
 */
static void pop_mode(Buffer *buf);

/*
 * Push select mode to mode stack.
 */
static void push_select_mode(Buffer *buf);

/*
 * Push insert mode to mode stack.
 */
static void push_insert_mode(Buffer *buf);

static void
buf_init(Buffer *buf, WerkInstance *werk)
{
	memset(buf, 0, sizeof(*buf));

	gbuf_init(&buf->gbuf);
	cmd_dialog_init(buf);

	buf->werk = werk;
	buf->vp_orig_col = buf->vp_orig_line
	                 = buf->sel_start.line
	                 = buf->sel_start.col
	                 = buf->sel_finish.line
	                 = buf->sel_finish.col = 1;

	/* is reset later using buf_detect_newline() */
	buf->eol = werk->cfg.text.default_newline;
	buf->eol_size = strlen(buf->eol);

	push_select_mode(buf);
}

static void
cmd_dialog_init(Buffer *buf)
{
	memset(&buf->dialog, 0, sizeof(buf->dialog));

	gbuf_init(&buf->dialog.gbuf);
	buf->dialog.active = false;
}

static void
buf_destroy(Buffer *buf)
{
	gbuf_destroy(&buf->gbuf);
	gbuf_destroy(&buf->dialog.gbuf);
	free((char *)buf->filename);

	while (buf->mode)
		pop_mode(buf);
}

static int
buf_read(Buffer *buf, const char *filename)
{
	FILE *in = fopen(filename, "rb");
	if (!in) {
		fprintf(stderr, "error opening file `%s'\n", filename);
		return -1;
	}

	size_t ln = gbuf_len(&buf->gbuf);
	char *backup = malloc(ln);
	if (!backup) {
		fprintf(stderr, "out of memory\n");
		return -1;
	}

	gbuf_strcpy(&buf->gbuf, backup, 0, ln);

	if (gbuf_read(&buf->gbuf, in)) {
		gbuf_clear(&buf->gbuf);
		gbuf_insert_text(&buf->gbuf, 0, backup, ln);
		free(backup);
		return -1;
	}

	buf_detect_newline(buf);
	buf->filename = strdup(filename);

	free(backup);
	return 0;
}

static void
buf_detect_newline(Buffer *buf)
{
	static const char *newlines[] = {
		u8"\r\n", u8"\r", u8"\f", u8"\v",
		u8"\xc2\x85", u8"\u2028", u8"\u2029",
	};

	gbuf_offs offs = 0;
	const char *graph;
	size_t graph_len;
	while (!gbuf_grapheme_next(&buf->gbuf, &graph, &graph_len, &offs)) {
		if (!grapheme_is_newline(graph, graph_len) || graph_len == 0)
			continue;

		/* the following loop is guaranteed to cause a return */
		for (int i = 0; i < sizeof(newlines) / sizeof(newlines[0]); ++i) {
			const char *nl = newlines[i];
			size_t nl_size = strlen(nl);

			if (strncmp(graph, nl, nl_size))
				continue;

			buf->eol = nl;
			buf->eol_size = nl_size;
			return;
		}
	}

	const char *dflt = buf->werk->cfg.text.default_newline;
	buf->eol = dflt;
	buf->eol_size = strlen(dflt);
}

static bool
buf_is_selection_degenerate(Buffer *buf)
{
	return buf->sel_start.offset == buf->sel_finish.offset;
}

static bool
grapheme_is_newline(const char *str, size_t len)
{
	switch (len) {
	case 1:
		return !strncmp(str, u8"\n", len)
	    	    || !strncmp(str, u8"\r", len)
	    	    || !strncmp(str, u8"\f", len)
	    	    || !strncmp(str, u8"\v", len);
	case 2:
		return !strncmp(str, u8"\r\n", len)
		    || !strncmp(str, u8"\xc2\x85", len); /* u0085 */
	case 3:
		return !strncmp(str, u8"\u2028", len)
	    	    || !strncmp(str, u8"\u2029", len);
	case 0:
		return true;
	default:
		return false;
	}
}

static int
grapheme_width(const char *str, size_t n, int col, int tab_width)
{
	if (n == 1 && str[0] == '\t') {
		int dest_col = (1 + (col - 1) / tab_width) * tab_width;
		return (dest_col + 1) - col;
	}

	if (n == 0)
		return 0;

	ucs4_t base;
	u8_mbtoucr(&base, str, n);
	return uc_width(base, "C");
}

static int
grapheme_column(Buffer *buf, gbuf_offs ofs)
{
	const char *bufstop = buf->gbuf.start;

	/* find last newline */

	/* character after last eol */
	gbuf_offs last_eol = ofs;
	for (;;) {
		gbuf_offs prev_ofs = last_eol;
		const char *graph;
		size_t len;
		if (gbuf_grapheme_prev(&buf->gbuf, &graph, &len, &prev_ofs) == -1)
			break;

		if (grapheme_is_newline(graph, len))
			break;

		last_eol = prev_ofs;
	}

	/* measure line until ofs */
	int res = 1;

	gbuf_offs scan = last_eol;
	while (scan != ofs) {
		const char *graph;
		size_t len;
		gbuf_grapheme_next(&buf->gbuf, &graph, &len, &scan);

		res += grapheme_width(graph, len, res, buf->werk->cfg.editor.tab_width);
	}

	return res;
}

static int
marker_next(Buffer *buf, const char **str, size_t *size, BufferMarker *marker)
{
	const char *s;
	size_t len;

	gbuf_offs ofs = marker->offset;
	gbuf_offs next_ofs = ofs;
	if (gbuf_grapheme_next(&buf->gbuf, &s, &len, &next_ofs))
		return -1;

	marker->offset = next_ofs;

	if (grapheme_is_newline(s, len)) {
		++marker->line;
		marker->col = 1;
	} else {
		marker->col += grapheme_width(s, len, marker->col, buf->werk->cfg.editor.tab_width);
	}

	if (str)
		*str = s;
	if (size)
		*size = len;

	return 0;
}

static int
marker_prev(Buffer *buf, const char **str, size_t *size, BufferMarker *marker)
{
	const char *s;
	size_t len;

	gbuf_offs next_ofs = marker->offset;
	gbuf_offs ofs = next_ofs;
	if (gbuf_grapheme_prev(&buf->gbuf, &s, &len, &ofs))
		return -1;

	marker->offset = ofs;

	if (grapheme_is_newline(s, len))
		--marker->line;

	marker->col = grapheme_column(buf, ofs);

	if (str)
		*str = s;
	if (size)
		*size = len;

	return 0;
}

static void
marker_next_line(Buffer *buf, BufferMarker *res)
{
	const char *str;
	size_t len;

	while (marker_next(buf, &str, &len, res) == 0)
		if (grapheme_is_newline(str, len))
			break;
}

static void
marker_start_of_line(Buffer *buf, BufferMarker *res)
{
	const char *str;
	size_t len;

	do {
		if (res->col == 1)
			break;
	} while (marker_prev(buf, &str, &len, res) == 0);
}

static void
marker_end_of_line(Buffer *buf, BufferMarker *res)
{
	BufferMarker prev;
	const char *str;
	size_t len;

	while (prev = *res, marker_next(buf, &str, &len, res) == 0) {
		if (grapheme_is_newline(str, len)) {
			*res = prev;
			break;
		}
	}
}

static void
marker_sort_pair(BufferMarker *a, BufferMarker *b)
{
	if (a->offset >= b->offset) {
		BufferMarker temp = *a;
		*a = *b;
		*b = temp;
	}
}

static void
buf_delete_range(Buffer *buf, BufferMarker a, BufferMarker b)
{
	marker_sort_pair(&a, &b);
	gbuf_delete_text(&buf->gbuf, a.offset, b.offset - a.offset);
}

static void
buf_move_cursor(Buffer *buf, int delta, bool extend)
{
	if (delta > 0) {

		for (int i = 0; i < delta; ++i)
			if (marker_next(buf, NULL, NULL, &buf->sel_finish) == -1)
				break;

	} else if (delta < 0) {

		for (int i = 0; i < -delta; ++i)
			if (marker_prev(buf, NULL, NULL, &buf->sel_finish) == -1)
				break;
	}

	if (!extend)
		buf->sel_start = buf->sel_finish;
}

static void
buf_pipe_selection(Buffer *buf, const char *str)
{
	gbuf_offs start = buf->sel_start.offset;
	gbuf_offs stop = buf->sel_finish.offset;
	bool flipped = false; /* hacky */

	if (start > stop) {
		gbuf_offs temp = start;
		start = stop;
		stop = temp;

		flipped = true;

		buf->sel_start = buf->sel_finish;
	}

	int old_size = gbuf_len(&buf->gbuf);

	gbuf_pipe(&buf->gbuf, str, start, stop - start);

	int new_size = gbuf_len(&buf->gbuf);
	int delta_size = new_size - old_size;
	gbuf_offs new_finish_ofs = stop + delta_size;

	/* the start marker doesn't move,
	 * the finsh marker does */

	buf->sel_finish = buf->sel_start;
	while (buf->sel_finish.offset != new_finish_ofs)
		if (marker_next(buf, NULL, NULL, &buf->sel_finish) == -1)
			break;

	if (flipped) {
		BufferMarker temp = buf->sel_start;
		buf->sel_start = buf->sel_finish;
		buf->sel_finish = temp;
	}
}

static void
buf_insert_input_string(Buffer *buf, const char *input, size_t len)
{
	if (!strncmp(input, "\t", 1)) {
		int indent = buf->werk->cfg.text.indentation;
		for (int i = 0; i < indent; ++i)
			buf_insert_input_string(buf, " ", 1);

		if (indent)
			return;
	}

	gbuf_insert_text(&buf->gbuf, buf->sel_finish.offset, input, len);
	buf_move_cursor(buf, 1, false);
	return;
}

static void
buf_calc_vp_origin(Buffer *buf, int wlines, int hlines)
{
	BufferMarker marker = buf->sel_finish;

	int vw, vh;
	buf_get_viewport(buf, &vw, &vh, wlines, hlines);

	int orig_col = buf->vp_orig_col;
	int orig_line = buf->vp_orig_line;

	int dorig_col = marker.col - orig_col;
	int dorig_line = marker.line - orig_line;

	if (dorig_col < 0)
		buf->vp_orig_col += dorig_col;
	else if (dorig_col >= vw)
		buf->vp_orig_col += (dorig_col - vw);

	if (dorig_line < 0) {
		BufferMarker new_orig;
		new_orig.offset = buf->vp_first_line;

		for (int i = 0; i < -dorig_line; ++i) {
			marker_prev(buf, NULL, NULL, &new_orig);
			marker_start_of_line(buf, &new_orig);
		}

		buf->vp_orig_line += dorig_line;
		buf->vp_first_line = new_orig.offset;
	} else if (dorig_line >= vh) {
		BufferMarker new_orig;
		new_orig.offset = buf->vp_first_line;

		int line_delta = (dorig_line - vh) + 1;
		for (int i = 0; i < line_delta; ++i)
			marker_next_line(buf, &new_orig);

		buf->vp_orig_line += line_delta;
		buf->vp_first_line = new_orig.offset;
	}
}

/*
 * The number of characters needed to represent `i' in base ten.
 */
static int
num_width(int i)
{
	int res = 1;

	if (i < 0) {
		++res;
		i = -i;
	}

	while (i >= 10) {
		++res;
		i /= 10;
	}

	return res;
}
static void
buf_get_viewport(Buffer *buf, int *vw, int *vh, int wlines, int hlines)
{
	int line_num_width = 0;

	if (buf->werk->cfg.editor.line_numbers)
		line_num_width = num_width(buf->vp_orig_line + hlines) + 1;

	if (vw) {
		int w = wlines - line_num_width;
		if (w < 0)
			w = 0;
		*vw = w;
	}

	if (vh)
		*vh = hlines;
}

/*
 * Draw a line number `n' at given position, `des_w' being the
 * desired width of the line number column.
 */
static void
draw_line_num(Drawer *d, int n, int x, int y, int des_w)
{
	char ch;

	int m = n % 10;
	ch = '0' + m;
	drw_draw_text(d, (des_w - 1) + x, y, false, false, &ch, 1);

	n /= 10;
	if (n == 0)
		return;

	draw_line_num(d, n, x, y, des_w - 1);
}

static void
buf_draw_grapheme(Buffer *buf,
                  Drawer *d,
		  int x, int y, int w,
                  const char *graph, size_t graph_len)
{
	if (graph_len == 0)
		return;

	ucs4_t ucs;
	u8_next(&ucs, graph);

	if (buf->werk->cfg.editor.show_tabs && !strncmp(graph, "\t", 1)) {
		drw_set_color(d, buf->mode->colors.inv);
		drw_draw_text(d, x + (w - 1), y, false, false, u8"⇥", strlen(u8"⇥"));
		drw_set_color(d, buf->mode->colors.fg);
	} else if (buf->werk->cfg.editor.show_spaces && !strncmp(graph, " ", 1)) {
		drw_set_color(d, buf->mode->colors.inv);
		drw_draw_text(d, x, y, false, false, u8"·", strlen(u8"·"));
		drw_set_color(d, buf->mode->colors.fg);
	} else if (buf->werk->cfg.editor.show_newlines && grapheme_is_newline(graph, graph_len)) {
		drw_set_color(d, buf->mode->colors.inv);
		drw_draw_text(d, x, y, false, false, u8"↲", strlen(u8"↲"));
		drw_set_color(d, buf->mode->colors.fg);
	} else if (uc_is_graph(ucs)) {
		drw_draw_text(d, x, y, false, false, graph, graph_len);
	}
}

static bool
buf_draw_line(Buffer *buf,
              Drawer *d,
              BufferMarker marker,
              int vw, int vh,
              int logic_x,
              int x, int y)
{
	GapBuf *gbuf = &buf->gbuf;

	/* draw line num */
	if (buf->werk->cfg.editor.line_numbers)
		draw_line_num(d, marker.line, 0, y, x - 1);

	gbuf_offs ofs = marker.offset;

	/* calculate first visible column */
	int col;
	for (col = 1; col < logic_x; ) {
		const char *graph;
		size_t len;
		if (gbuf_grapheme_next(gbuf, &graph, &len, &ofs) == -1)
			return false;

		if (grapheme_is_newline(graph, len));
			return true;

		col += grapheme_width(graph, len, col, buf->werk->cfg.editor.tab_width);
	}

	while ((col - logic_x) < vw) {
		const char *graph;
		size_t len;
		if (gbuf_grapheme_next(gbuf, &graph, &len, &ofs) == -1)
			return false;

		int w = grapheme_width(graph, len, col, buf->werk->cfg.editor.tab_width);
		buf_draw_grapheme(buf, d, x + (col - logic_x), y, w, graph, len);
		col += w;

		if (grapheme_is_newline(graph, len))
			return true;
	}

	return true;
}

static void
buf_draw_selection(Buffer *buf, Drawer *d, int vw, int vh, int offset_x)
{
	if (buf_is_selection_degenerate(buf))
		return;

	BufferMarker left = buf->sel_start;
	BufferMarker right = buf->sel_finish;
	if (left.offset > right.offset) {
		BufferMarker temp = right;
		right = left;
		left = temp;
	}

	if (left.line == right.line) {

		int y = left.line - buf->vp_orig_line;
		int x = left.col - buf->vp_orig_col + offset_x;
		drw_fill_rect(d, x, y, right.col - left.col, 1);

	} else {

		int top_y = left.line - buf->vp_orig_line;
		int top_x = left.col - buf->vp_orig_col + offset_x;
		drw_fill_rect(d, top_x, top_y, vw - top_x + offset_x, 1);

		int bottom_y = right.line - buf->vp_orig_line;
		int bottom_x = right.col - buf->vp_orig_col + offset_x;
		drw_fill_rect(d, offset_x, bottom_y, bottom_x - offset_x, 1);

		drw_fill_rect(d, offset_x, top_y + 1, vw, bottom_y - top_y - 1);
	}
}

static void
buf_draw(Buffer *buf, Drawer *d, int wlines, int hlines)
{
	drw_set_color(d, buf->mode->colors.bg);
	drw_clear(d);

	buf_calc_vp_origin(buf, wlines, hlines);

	int vw, vh;
	buf_get_viewport(buf, &vw, &vh, wlines, hlines);

	drw_set_color(d, buf->mode->colors.bg);
	drw_clear(d);

	int logic_x = buf->vp_orig_col;
	int line_num = buf->vp_orig_line;
	int line_num_width = 0;
	if (buf->werk->cfg.editor.line_numbers) {
		line_num_width = num_width(line_num + vh) + 1;

		drw_set_color(d, buf->mode->colors.line_numbers_bg);
		drw_fill_rect(d, 0, 0, line_num_width, vh);
	}

	drw_set_color(d, buf->mode->colors.sel);
	buf_draw_selection(buf, d, vw, vh, line_num_width);

	/* draw lines */

	BufferMarker line_start;
	line_start.offset = buf->vp_first_line;
	line_start.line = buf->vp_orig_line;
	line_start.col = buf->vp_orig_col;

	drw_set_color(d, buf->mode->colors.fg);
	for (int i = 0; i < vh; ++i) {
		if (!buf_draw_line(buf, d, line_start, vw, vh, logic_x, line_num_width, i))
			break;

		int prev_line_num = line_start.line;
		marker_next_line(buf, &line_start);
		if (prev_line_num == line_start.line)
			break;
	}

	int cur_x = line_num_width + buf->sel_finish.col - buf->vp_orig_col;
	int cur_y = buf->sel_finish.line - line_num;
	drw_place_caret(d, cur_x, cur_y, true);
}

static void
buf_get_line_col_position(Buffer *buf, int l, int c, int w, int h, int *x, int *y)
{
	int vw, vh;
	buf_get_viewport(buf, &vw, &vh, w, h);

	int line_num = buf->vp_orig_line;
	int line_num_width = num_width(line_num + vh) + 1;

	int orig_x = buf->vp_orig_col;
	int orig_y = line_num;

	if (x)
		*x = line_num_width + (c - orig_x);
	if (y)
		*y = l - orig_y;
}

static int
buf_save(Buffer *buf)
{
	if (!buf->filename)
		return -1;

	FILE *out = fopen(buf->filename, "wb");
	if (!out)
		return -1;

	gbuf_write(&buf->gbuf, out);
	fclose(out);

	return 0;
}

static void
cmd_dialog_on_key_press(Buffer *buf, KeyMods mods, const char *input, size_t len)
{
	gbuf_insert_text(&buf->dialog.gbuf, buf->dialog.gbuf.gap_offs, input, len);
}

static void
cmd_dialog_on_backspace_press(Buffer *buf, KeyMods mods)
{
	GapBuf *gbuf = &buf->dialog.gbuf;
	gbuf_backspace_grapheme(gbuf, gbuf->gap_offs);
}

static void
cmd_dialog_on_enter_press(Buffer *buf, KeyMods mods)
{
	GapBuf *gbuf = &buf->dialog.gbuf;
	const size_t buf_len = gbuf->size - gbuf->gap_size;
	char *str = calloc(1, buf_len + 1);
	gbuf_strcpy(gbuf, str, 0, buf_len);
	buf_pipe_selection(buf, str);
	free(str);

	/* TODO: reimplement */
	/* make cursor degenerate if in insert mode */
	/*if (!dlg->select)
		werk->sel_start = werk->sel_finish;*/

	buf->dialog.active = false;

	/* clear gap buf */
	gbuf_destroy(gbuf);
	gbuf_init(gbuf);
}

static void
draw_cmd_dialog(Buffer *buf, Drawer *d, int ww, int wh)
{
	if (!buf->dialog.active)
		return;

	int line = buf->sel_finish.line;
	int col = buf->sel_finish.col;

	if (buf->sel_finish.line > buf->sel_start.line)
		++line;
	else
		--line;

	int x, y;
	buf_get_line_col_position(buf, line, col, ww, wh, &x, &y);

	if (y >= wh)
		y -= 2;
	else if (y < 0)
		y += 2;

	int overhang = x + buf->dialog.w - ww;
	if (overhang > 0)
		x -= overhang;

	drw_set_color(d, buf->werk->cfg.colors.insert.bg);
	drw_fill_rect(d, x, y, buf->dialog.w, 1);
	drw_set_color(d, buf->werk->cfg.colors.insert.fg);
	drw_stroke_rect(d, x, y, buf->dialog.w, 1);

	drw_set_color(d, buf->werk->cfg.colors.insert.fg);

	GapBuf *gbuf = &buf->dialog.gbuf;

	const size_t buf_len = gbuf->size - gbuf->gap_size;
	char *str = calloc(1, buf_len + 1);
	gbuf_strcpy(gbuf, str, 0, buf_len);

	/* TODO should obviously think in terms of grapheme length... */
	int num_graphemes = buf_len;
	int num_show = num_graphemes > buf->dialog.w ? buf->dialog.w : num_graphemes;
	int show_offs = num_graphemes - num_show;

	int x_offs = (buf->dialog.w < num_show) * (buf->dialog.w - num_graphemes);


	drw_draw_text(d, x + x_offs, y, false, false, str + show_offs, num_show);

	free(str);

	drw_place_caret(d, x + num_show, y, true);
}

static void
show_cmd_dialog(Buffer *buf)
{
	buf->dialog.w = CMD_DIALOG_WIDTH;
	buf->dialog.active = true;
}

/*
 *                      _       _   _             _
 *  _ __ ___   ___   __| | __ _| | | | ___   __ _(_) ___
 * | '_ ` _ \ / _ \ / _` |/ _` | | | |/ _ \ / _` | |/ __|
 * | | | | | | (_) | (_| | (_| | | | | (_) | (_| | | (__
 * |_| |_| |_|\___/ \__,_|\__,_|_| |_|\___/ \__, |_|\___|
 *                                          |___/
 */

static void
pop_mode(Buffer *buf)
{
	Mode *mode = buf->mode;
	if (!mode)
		return;

	mode->destroy(mode);
	buf->mode = mode->below;
}

static void sm_destroy(Mode *mode);
static void sm_on_key_press(Buffer *buf, Mode *mode, KeyMods mods, const char *input, size_t len);
static void sm_on_enter_press(Buffer *buf, Mode *mode, KeyMods mods);
static void sm_on_backspace_press(Buffer *buf, Mode *mode, KeyMods mods);
static void sm_on_delete_press(Buffer *buf, Mode *mode, KeyMods mods);

static void
push_select_mode(Buffer *buf)
{
	Mode *mode = malloc(sizeof(Mode));
	if (!mode)
		return;

	mode->on_key_press = sm_on_key_press;
	mode->on_enter_press = sm_on_enter_press;
	mode->on_backspace_press = sm_on_backspace_press;
	mode->on_delete_press = sm_on_delete_press;
	mode->destroy = sm_destroy;

	mode->colors = buf->werk->cfg.colors.select;

	mode->below = buf->mode;
	buf->mode = mode;
}

static void
sm_destroy(Mode *mode)
{
	free(mode);
}

static void
select_next_line(Buffer *buf, bool extend)
{
	BufferMarker next_line = buf->sel_finish;
	BufferMarker line_start = next_line;

	marker_next_line(buf, &next_line);
	buf->sel_finish = next_line;

	if (!extend || line_start.col != 1) {
		marker_start_of_line(buf, &line_start);
		buf->sel_start = line_start;
	}
}

static void
select_prev_line(Buffer *buf, bool extend)
{
	BufferMarker line_start = buf->sel_finish;

	marker_start_of_line(buf, &line_start);
	marker_prev(buf, NULL, NULL, &line_start);
	marker_start_of_line(buf, &line_start);
	buf->sel_finish = line_start;

	BufferMarker next_line = line_start;
	marker_next_line(buf, &next_line);
	buf->sel_start = next_line;
}

static void
sm_on_key_press(Buffer *buf, Mode *mode, KeyMods mods, const char *input, size_t len)
{
	if (len != 1)
		return;

	if (mods & KM_CONTROL) {
		switch (input[0]) {
		case 'd':
			show_cmd_dialog(buf);
			break;
		case 's':
			buf_save(buf);
			break;
		}

		return;
	}

	switch (input[0]) {
	case 'i':
		buf->sel_finish = buf->sel_start;
		push_insert_mode(buf);
		break;

	case 'a':
		buf->sel_start = buf->sel_finish;
		push_insert_mode(buf);
		break;

	case 'c':
		marker_sort_pair(&buf->sel_start, &buf->sel_finish);
		buf_delete_range(buf, buf->sel_start, buf->sel_finish);
		buf->sel_finish = buf->sel_start;
		push_insert_mode(buf);
		break;

	case 'd':
		marker_sort_pair(&buf->sel_start, &buf->sel_finish);
		buf_delete_range(buf, buf->sel_start, buf->sel_finish);
		buf->sel_finish = buf->sel_start;
		break;

	case 'L':
	case 'l':
		buf_move_cursor(buf, 1, input[0] == 'L');
		break;

	case 'H':
	case 'h':
		buf_move_cursor(buf, -1, input[0] == 'H');
		break;

	case 'J':
	case 'j':
		select_next_line(buf, input[0] == 'J');
		break;

	case 'K':
	case 'k':
		select_prev_line(buf, input[0] == 'K');
		break;

	case '.':
		buf->werk->active_buf = buf->prev;
		break;

	case '/':
		buf->werk->active_buf = buf->next;
		break;
	}
}

static void
sm_on_enter_press(Buffer *buf, Mode *mode, KeyMods mods)
{
}

static void
sm_on_backspace_press(Buffer *buf, Mode *mode, KeyMods mods)
{
}

static void
sm_on_delete_press(Buffer *buf, Mode *mode, KeyMods mods)
{
}

static void im_destroy(Mode *mode);
static void im_on_key_press(Buffer *buf, Mode *mode, KeyMods mods, const char *input, size_t len);
static void im_on_enter_press(Buffer *buf, Mode *mode, KeyMods mods);
static void im_on_backspace_press(Buffer *buf, Mode *mode, KeyMods mods);
static void im_on_delete_press(Buffer *buf, Mode *mode, KeyMods mods);

static void
push_insert_mode(Buffer *buf)
{
	Mode *mode = malloc(sizeof(Mode));
	if (!mode)
		return;

	mode->on_key_press = im_on_key_press;
	mode->on_enter_press = im_on_enter_press;
	mode->on_backspace_press = im_on_backspace_press;
	mode->on_delete_press = im_on_delete_press;
	mode->destroy = im_destroy;

	mode->colors = buf->werk->cfg.colors.insert;

	mode->below = buf->mode;
	buf->mode = mode;
}

static void
im_destroy(Mode *mode)
{
	free(mode);
}

static void
im_on_key_press(Buffer *buf, Mode *mode, KeyMods mods, const char *input, size_t len)
{
	if ((mods & KM_CONTROL) == 0) {
		buf_insert_input_string(buf, input, len);
		return;
	}

	if (len != 1)
		return;

	switch (input[0]) {
	case 'd':
		show_cmd_dialog(buf);
		break;

	case '[':
		pop_mode(buf);
		break;

	case 's':
		buf_save(buf);
		break;
	}
}

static void
im_on_enter_press(Buffer *buf, Mode *mode, KeyMods mods)
{
	im_on_key_press(buf, mode, mods, buf->eol, buf->eol_size);
}

static void
im_on_backspace_press(Buffer *buf, Mode *mode, KeyMods mods)
{
	gbuf_offs cur = buf->sel_finish.offset;
	buf_move_cursor(buf, -1, false);
	gbuf_backspace_grapheme(&buf->gbuf, cur);
}

static void
im_on_delete_press(Buffer *buf, Mode *mode, KeyMods mods)
{
	gbuf_delete_grapheme(&buf->gbuf, buf->sel_finish.offset);
}

/*
 *           _ _ _               _           _
 *   ___  __| (_) |_ ___  _ __  (_)_ __  ___| |_ __ _ _ __   ___ ___
 *  / _ \/ _` | | __/ _ \| '__| | | '_ \/ __| __/ _` | '_ \ / __/ _ \
 * |  __/ (_| | | || (_) | |    | | | | \__ \ || (_| | | | | (_|  __/
 *  \___|\__,_|_|\__\___/|_|    |_|_| |_|___/\__\__,_|_| |_|\___\___|
 *
 */

static void
werk_on_key_press(Window *win, KeyMods mods, const char *input, size_t len)
{
	WerkInstance *werk = win->user_data;
	Buffer *active_buf = werk->active_buf;

	if (active_buf->dialog.active) {
		cmd_dialog_on_key_press(active_buf, mods, input, len);
	} else {
		Mode *mode = active_buf->mode;
		mode->on_key_press(active_buf, mode, mods, input, len);
	}

	win_redraw(win);
}

static void
werk_on_enter_press(Window *win, KeyMods mods)
{
	WerkInstance *werk = win->user_data;
	Buffer *active_buf = werk->active_buf;

	if (active_buf->dialog.active) {
		cmd_dialog_on_enter_press(active_buf, mods);
	} else {
		Mode *mode = active_buf->mode;
		mode->on_enter_press(active_buf, mode, mods);
	}

	win_redraw(win);
}

static void
werk_on_backspace_press(Window *win, KeyMods mods)
{
	WerkInstance *werk = win->user_data;
	Buffer *active_buf = werk->active_buf;

	if (active_buf->dialog.active) {
		cmd_dialog_on_backspace_press(active_buf, mods);
	} else {
		Mode *mode = active_buf->mode;
		mode->on_backspace_press(active_buf, mode, mods);
	}

	win_redraw(win);
}

static void
werk_on_delete_press(Window *win, KeyMods mods)
{
	WerkInstance *werk = win->user_data;
	Buffer *active_buf = werk->active_buf;

	if (active_buf->dialog.active) {
		/* nop as of yet */
	} else {
		Mode *mode = active_buf->mode;
		mode->on_delete_press(active_buf, mode, mods);
	}

	win_redraw(win);
}

static void
werk_on_draw(Window *win, Drawer *d, int wlines, int hlines)
{
	WerkInstance *werk = win->user_data;
	Buffer *active_buf = werk->active_buf;

	buf_draw(active_buf, d, wlines, hlines);
	draw_cmd_dialog(active_buf, d, wlines, hlines);
}

/*
 * Removes buffer from instance.
 * Does not free buffer resources as of yet.
 */
static void
werk_remove_buffer(WerkInstance *werk, Buffer *buf)
{
	if (werk->active_buf == buf) {
		if (buf->next == buf)
			werk->active_buf = NULL;
		else
			werk->active_buf = buf->next;
	}

	buf->next->prev = buf->prev;
	buf->prev->next = buf->next;

	buf_destroy(buf);
	free(buf);
}

/*
 * Add buffer containing only a newline to the instance, and set it as
 * the active buffer.
 */
static Buffer *
werk_add_buffer(WerkInstance *werk)
{
	Buffer *buf = malloc(sizeof(Buffer));
	buf_init(buf, werk);
	gbuf_insert_text(&buf->gbuf, 0, buf->eol, buf->eol_size);

	Buffer *cur_active = werk->active_buf;
	if (cur_active) {
		Buffer *nxt = cur_active->next;
		cur_active->next = buf;
		nxt->prev = buf;
		buf->next = nxt;
		buf->prev = cur_active;
	} else {
		buf->next = buf->prev = buf;
	}

	werk->active_buf = buf;
	return buf;
}

/*
 * Add file buffer to the instance, and set it as the active buffer.
 * In case of failure, nothing is changed.
 */
static Buffer *
werk_add_file(WerkInstance *werk, const char *path)
{
	Buffer *buf = werk_add_buffer(werk);
	if (buf_read(buf, path)) {
		werk_remove_buffer(werk, buf);
		return NULL;
	}
	return buf;
}

static void
werk_on_close(Window *win)
{
	WerkInstance *werk = win->user_data;
	while (werk->active_buf)
		werk_remove_buffer(werk, werk->active_buf);
	free(werk);
}

void
werk_init(Window *win, const char **files, int num_files, ConfigReader *crdr)
{
	WerkInstance *werk = malloc(sizeof(WerkInstance));
	memset(werk, 0, sizeof(*werk));
	win->user_data = werk;

	config_load(&werk->cfg, crdr);
	config_destroy(crdr);

	for (int i = 0; i < num_files; ++i)
		werk_add_file(werk, files[i]);

	if (!werk->active_buf)
		werk_add_buffer(werk);

	win->on_draw = werk_on_draw;
	win->on_key_press = werk_on_key_press;
	win->on_enter_press = werk_on_enter_press;
	win->on_backspace_press = werk_on_backspace_press;
	win->on_delete_press = werk_on_delete_press;
	win->on_close = werk_on_close;

	win_show(win);
}
