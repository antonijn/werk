#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unictype.h>
#include <unigbrk.h>
#include <unistr.h>
#include <uniwidth.h>
#include "config.h"
#include "edit.h"
#include "gap.h"

#define CMD_DIALOG_WIDTH 30

typedef struct {
	gbuf_offs offset;
	int line, col;
} BufferMarker;

typedef struct mode Mode;
typedef struct werk_instance WerkInstance;

typedef struct buffer {
	/* Cursor is guaranteed to be at grapheme boundaries */
	GapBuf gbuf;

	/* Viewport top left */
	gbuf_offs vp_first_line;       /* first char in first line in viewport */
	int vp_orig_line, vp_orig_col; /* first viewport line, column */

	/* Selection markers */
	BufferMarker sel_start, sel_finish;

	const char *eol;
	size_t eol_size;

	const char *filename;

	Mode *mode;

	struct {
		bool active;
		int line, col, w;
		GapBuf gbuf;
	} dialog;

	/* part of ring queue */
	struct buffer *next, *prev;

	WerkInstance *werk;
} Buffer;

struct werk_instance {
	Config cfg;
	ColorSet colors;

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

/* Next and prev graphemes */
static int marker_next(Buffer *buf, const char **str, size_t *size, BufferMarker *marker);
static int marker_prev(Buffer *buf, const char **str, size_t *size, BufferMarker *marker);

/* Start of next line */
static void marker_next_line(Buffer *buf, BufferMarker *marker);
/* Start of current line */
static void marker_start_of_line(Buffer *buf, BufferMarker *marker);
static void marker_end_of_line(Buffer *buf, BufferMarker *marker);


static void buf_init(Buffer *buf, WerkInstance *werk);

static bool buf_is_selection_degenerate(Buffer *buf);
static void buf_move_cursor(Buffer *buf, int delta, bool extend);
/* Calculate viewport so that the end of the selection is visible */
static void buf_move_viewport(Buffer *buf, int wlines, int hlines);
static void buf_get_viewport(Buffer *buf, int *vw, int *vh, int wlines, int hlines);
static void buf_draw(Buffer *buf, Drawer *d, int wlines, int hlines);
static void buf_pipe_selection(Buffer *buf, const char *str);
static void buf_insert_input_string(Buffer *buf, const char *input, size_t len);

/* converts line and col number to position on screen (x, y) */
static void buf_get_line_col_position(Buffer *buf, int l, int c, int w, int h, int *x, int *y);

static void cmd_dialog_init(Buffer *buf);

static Mode *nm_init(Buffer *buf);

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

	buf->mode = nm_init(buf);
	buf->eol = "\n";
	buf->eol_size = 1;
}

static bool buf_is_selection_degenerate(Buffer *buf)
{
	return buf->sel_start.offset == buf->sel_finish.offset;
}

/* All Unicode newlines are accepted */
static bool grapheme_is_newline(const char *str, size_t len)
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

		res += grapheme_width(graph, len, res, buf->werk->cfg.text.tab_width);
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
		marker->col += grapheme_width(s, len, marker->col, buf->werk->cfg.text.tab_width);
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
buf_move_viewport(Buffer *buf, int wlines, int hlines)
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

		for (int i = 0; i < (dorig_line - vh); ++i)
			marker_next_line(buf, &new_orig);

		buf->vp_orig_line += (dorig_line - vh);
		buf->vp_first_line = new_orig.offset;
	}
}

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
	int line_num_width = num_width(buf->vp_orig_line + hlines);

	if (vw) {
		int w = wlines - (line_num_width + 1);
		if (w < 0)
			w = 0;
		*vw = w;
	}

	if (vh)
		*vh = hlines;
}

/* des_w: desired width */
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
		drw_set_color(d, buf->werk->colors.inv);
		drw_draw_text(d, x + (w - 1), y, false, false, u8"⇥", strlen(u8"⇥"));
		drw_set_color(d, buf->werk->colors.fg);
	} else if (buf->werk->cfg.editor.show_spaces && !strncmp(graph, " ", 1)) {
		drw_set_color(d, buf->werk->colors.inv);
		drw_draw_text(d, x, y, false, false, u8"·", strlen(u8"·"));
		drw_set_color(d, buf->werk->colors.fg);
	} else if (buf->werk->cfg.editor.show_newlines && grapheme_is_newline(graph, graph_len)) {
		drw_set_color(d, buf->werk->colors.inv);
		drw_draw_text(d, x, y, false, false, u8"↲", strlen(u8"↲"));
		drw_set_color(d, buf->werk->colors.fg);
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

		col += grapheme_width(graph, len, col, buf->werk->cfg.text.tab_width);
	}

	while ((col - logic_x) < vw) {
		const char *graph;
		size_t len;
		if (gbuf_grapheme_next(gbuf, &graph, &len, &ofs) == -1)
			return false;

		int w = grapheme_width(graph, len, col, buf->werk->cfg.text.tab_width);
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
	buf_move_viewport(buf, wlines, hlines);

	int vw, vh;
	buf_get_viewport(buf, &vw, &vh, wlines, hlines);

	drw_set_color(d, buf->werk->colors.bg);
	drw_clear(d);

	int logic_x = buf->vp_orig_col;
	int line_num = buf->vp_orig_line;
	int line_num_width = 0;
	if (buf->werk->cfg.editor.line_numbers) {
		line_num_width = num_width(line_num + vh) + 1;

		drw_set_color(d, buf->werk->colors.line_numbers_bg);
		drw_fill_rect(d, 0, 0, line_num_width, vh);
	}

	drw_set_color(d, buf->werk->colors.sel);
	buf_draw_selection(buf, d, vw, vh, line_num_width);

	/* draw lines */

	BufferMarker line_start;
	line_start.offset = buf->vp_first_line;
	line_start.line = buf->vp_orig_line;
	line_start.col = buf->vp_orig_col;

	drw_set_color(d, buf->werk->colors.fg);
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

/*                     _       _ _       _             
 *   ___ _ __ ___   __| |   __| (_) __ _| | ___   __ _ 
 *  / __| '_ ` _ \ / _` |  / _` | |/ _` | |/ _ \ / _` |
 * | (__| | | | | | (_| | | (_| | | (_| | | (_) | (_| |
 *  \___|_| |_| |_|\__,_|  \__,_|_|\__,_|_|\___/ \__, |
 *                                               |___/ 
 */

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

	int x, y;
	buf_get_line_col_position(buf, buf->dialog.line, buf->dialog.col, ww, wh, &x, &y);

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


	drw_draw_text(d,
	              x + x_offs, y,
	              false, false,
	              str + show_offs,
	              num_show);

	free(str);

	drw_place_caret(d, x + num_show, y, true);
}

static void
show_cmd_dialog(Buffer *buf)
{
	int line = buf->sel_finish.line;
	int col = buf->sel_finish.col;

	if (buf->sel_finish.line > buf->sel_start.line)
		++line;
	else
		--line;

	buf->dialog.line = line;
	buf->dialog.col = col;
	buf->dialog.w = CMD_DIALOG_WIDTH;
	buf->dialog.active = true;
}

static void
cmd_dialog_init(Buffer *buf)
{
	memset(&buf->dialog, 0, sizeof(buf->dialog));

	gbuf_init(&buf->dialog.gbuf);
	buf->dialog.active = false;
}

/*                  _                  _ _ _             
 *  _ __ ___   __ _(_)_ __     ___  __| (_) |_ ___  _ __ 
 * | '_ ` _ \ / _` | | '_ \   / _ \/ _` | | __/ _ \| '__|
 * | | | | | | (_| | | | | | |  __/ (_| | | || (_) | |   
 * |_| |_| |_|\__,_|_|_| |_|  \___|\__,_|_|\__\___/|_|   
 */

/*                _      _   _           _    
 *  _ __  ___  __| |__ _| | | |___  __ _(_)__ 
 * | '  \/ _ \/ _` / _` | | | / _ \/ _` | / _|
 * |_|_|_\___/\__,_\__,_|_| |_\___/\__, |_\__|
 *                                 |___/      
 */

/* aka Mode */
struct mode {
	void *data;

	Mode *(*on_key_press)(Mode *mode, KeyMods mods, const char *input, size_t len);
	Mode *(*on_enter_press)(Mode *mode, KeyMods mods);
	Mode *(*on_backspace_press)(Mode *mode, KeyMods mods);
	Mode *(*on_delete_press)(Mode *mode, KeyMods mods);
};

/* Function prefixes:
 *
 * nm* - normal mode functions
 * im* - insert mode functions
 * pm* - parenthesis mode functions
 */

typedef struct {
	Mode base;
	Buffer *buf;
} NormalMode;

static Mode *nm_on_key_press(Mode *mode, KeyMods mods, const char *input, size_t len);
static Mode *nm_on_enter_press(Mode *mode, KeyMods mods);
static Mode *nm_on_backspace_press(Mode *mode, KeyMods mods);
static Mode *nm_on_delete_press(Mode *mode, KeyMods mods);

static Mode *im_init(NormalMode *nm_parent);

static Mode *
nm_init(Buffer *buf)
{
	NormalMode *nmode = malloc(sizeof(NormalMode));
	Mode *mode = &nmode->base;

	nmode->buf = buf;

	mode->data = nmode;
	mode->on_key_press = nm_on_key_press;
	mode->on_enter_press = nm_on_enter_press;
	mode->on_backspace_press = nm_on_backspace_press;
	mode->on_delete_press = nm_on_delete_press;

	buf->werk->colors = buf->werk->cfg.colors.select;

	return mode;
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

static Mode *
nm_on_key_press(Mode *mode, KeyMods mods, const char *input, size_t len)
{
	NormalMode *nmode = mode->data;
	Buffer *buf = nmode->buf;

	if (len != 1)
		return mode;

	if (mods & KM_CONTROL) {
		switch (input[0]) {
		case 'd':
			show_cmd_dialog(buf);
			return mode;
		case 's': {
			if (!buf->filename)
				return mode;

			FILE *out = fopen(buf->filename, "wb");
			gbuf_write(&buf->gbuf, out);
			fclose(out);
			return mode;
			  }
		}
	}

	switch (input[0]) {
	case 'i':
		buf->sel_finish = buf->sel_start;
		return im_init(mode->data);
	case 'a':
		buf->sel_start = buf->sel_finish;
		return im_init(mode->data);
	case 'c':
		marker_sort_pair(&buf->sel_start, &buf->sel_finish);
		buf_delete_range(buf, buf->sel_start, buf->sel_finish);
		buf->sel_finish = buf->sel_start;
		return im_init(mode->data);
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

	return mode;
}

static Mode *
nm_on_enter_press(Mode *mode, KeyMods mods)
{
	return mode;
}

static Mode *
nm_on_backspace_press(Mode *mode, KeyMods mods)
{
	return mode;
}

static Mode *
nm_on_delete_press(Mode *mode, KeyMods mods)
{
	return mode;
}

typedef struct {
	Mode base;
	Buffer *buf;
	Mode *parent;
} InsertMode;

static Mode *im_on_key_press(Mode *mode, KeyMods mods, const char *input, size_t len);
static Mode *im_on_enter_press(Mode *mode, KeyMods mods);
static Mode *im_on_backspace_press(Mode *mode, KeyMods mods);
static Mode *im_on_delete_press(Mode *mode, KeyMods mods);

static Mode *
im_init(NormalMode *nm_parent)
{
	InsertMode *imode = malloc(sizeof(InsertMode));
	Mode *mode = &imode->base;

	imode->parent = &nm_parent->base;
	imode->buf = nm_parent->buf;

	mode->data = imode;
	mode->on_key_press = im_on_key_press;
	mode->on_enter_press = im_on_enter_press;
	mode->on_backspace_press = im_on_backspace_press;
	mode->on_delete_press = im_on_delete_press;

	nm_parent->buf->werk->colors = nm_parent->buf->werk->cfg.colors.insert;

	return mode;
}

static Mode *
im_on_key_press(Mode *mode, KeyMods mods, const char *input, size_t len)
{
	InsertMode *imode = mode->data;

	if ((mods & KM_CONTROL) == 0) {
		buf_insert_input_string(imode->buf, input, len);
		return mode;
	}

	if (len != 1) {
		return mode;
	}

	switch (input[0]) {
	case 'd':
		show_cmd_dialog(imode->buf);
		return mode;
	case '[':
		/* UNELEGANT HACKY SOLUTION */
		imode->buf->werk->colors = imode->buf->werk->cfg.colors.select;
		/* END OF UNELEGANT HACKY SOLUTION */

		mode = imode->parent;
		free(imode);
		return imode->parent;
	case 's': {
		if (!imode->buf->filename)
			break;

		FILE *out = fopen(imode->buf->filename, "wb");
		gbuf_write(&imode->buf->gbuf, out);
		fclose(out);
		break;
		  }
	}

	return mode;
}

static Mode *
im_on_enter_press(Mode *mode, KeyMods mods)
{
	InsertMode *imode = mode->data;
	Buffer *buf = imode->buf;

	im_on_key_press(mode, mods, buf->eol, buf->eol_size);
	return mode;
}

static Mode *
im_on_backspace_press(Mode *mode, KeyMods mods)
{
	InsertMode *imode = mode->data;
	gbuf_offs cur = imode->buf->sel_finish.offset;
	buf_move_cursor(imode->buf, -1, false);
	gbuf_backspace_grapheme(&imode->buf->gbuf, cur);
	return mode;
}

static Mode *
im_on_delete_press(Mode *mode, KeyMods mods)
{
	return mode;
}

/*         _ _ _                    _         _            
 *  ___ __| (_) |_ ___ _ _  __ __ _(_)_ _  __| |_____ __ __
 * / -_) _` | |  _/ _ \ '_| \ V  V / | ' \/ _` / _ \ V  V /
 * \___\__,_|_|\__\___/_|    \_/\_/|_|_||_\__,_\___/\_/\_/ 
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
		active_buf->mode = mode->on_key_press(mode, mods, input, len);
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
		active_buf->mode = mode->on_enter_press(mode, mods);
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
		active_buf->mode = mode->on_backspace_press(mode, mods);
	}

	win_redraw(win);
}

static void
werk_on_draw(Window *win, Drawer *d, int wlines, int hlines)
{
	WerkInstance *werk = win->user_data;
	Buffer *active_buf = werk->active_buf;

	drw_set_color(d, werk->colors.bg);
	drw_clear(d);
	buf_draw(active_buf, d, wlines, hlines);
	draw_cmd_dialog(active_buf, d, wlines, hlines);
}
static void
werk_on_close(Window *win)
{
	WerkInstance *werk = win->user_data;
	/* TODO: free contents of instance */
	free(werk);
}

static Buffer *
werk_add_buffer(WerkInstance *werk)
{
	Buffer *buf = malloc(sizeof(Buffer));
	buf_init(buf, werk);
	gbuf_insert_text(&buf->gbuf, 0, "\n", 1);

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

static Buffer *
werk_add_file(WerkInstance *werk, const char *path)
{
	Buffer *buf = werk_add_buffer(werk);

	FILE *fin = fopen(path, "rb");
	if (fin) {
		gbuf_read(&buf->gbuf, fin);
		fclose(fin);
	}

	/* TODO: strdup */
	buf->filename = path;

	return buf;
}

void
werk_init(Window *win, const char **files, int num_files)
{
	WerkInstance *werk = malloc(sizeof(WerkInstance));
	memset(werk, 0, sizeof(*werk));
	win->user_data = werk;

	config_load(&werk->cfg);

	if (num_files == 0) {
		werk_add_buffer(werk);
	} else {
		for (int i = 0; i < num_files; ++i)
			werk_add_file(werk, files[i]);
	}

	win->on_draw = werk_on_draw;
	win->on_key_press = werk_on_key_press;
	win->on_enter_press = werk_on_enter_press;
	win->on_backspace_press = werk_on_backspace_press;
	win->on_delete_press = werk_on_backspace_press;
	win->on_close = werk_on_close;

	win_show(win);
}
