#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gtk/gtkimmodule.h>
#include <pango/pangocairo.h>
#include <cairo.h>
#include <unictype.h>
#include <unigbrk.h>
#include <unistr.h>
#include <uniwidth.h>
#include <werk/ui/gtk.h>
#include <werk/ui/win.h>
#include <werk/edit.h>

static PangoFontDescription *font;

static double glyph_w, glyph_h;
static int iglyph_w, iglyph_h;

typedef struct {
	Window base;

	GtkWidget *window;
	GtkWidget *darea;
	GtkIMContext *im_ctx;
} WindowData;

struct caret {
	int x, y;
	bool visible;
};

struct cairo_drawer {
	cairo_t *cr;
	PangoLayout *layout;
	PangoFont *act_font;

	struct drawer d;
	struct caret caret;
};

static void
cairo_set_color(Drawer *d, RGB color)
{
	cairo_t *cr = ((struct cairo_drawer *)d->data)->cr;
	cairo_set_source_rgb(cr, color.r / 255.0, color.g / 255.0, color.b / 255.0);
}
static void
cairo_clear(Drawer *d)
{
	cairo_t *cr = ((struct cairo_drawer *)d->data)->cr;
	cairo_paint(cr);
}
static void
cairo_place_caret(Drawer *d, int x, int y, bool visible)
{
	struct cairo_drawer *cd = d->data;
	cairo_t *cr = cd->cr;

	cairo_set_operator(cr, CAIRO_OPERATOR_XOR);

	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8);
	cairo_set_line_width(cr, 1);

	int caret_x = x * glyph_w;
	int caret_y = y * glyph_h;

	cairo_move_to(cr, caret_x + 0.5, caret_y + 0.5);
	cairo_line_to(cr, caret_x + 0.5, caret_y + glyph_h - 0.5);
	cairo_stroke(cr);

	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}
static void
cairo_draw_glyph(struct cairo_drawer *cd,
                 int x, int y,
                 bool bold, bool it,
                 const char *str, size_t len)
{
	ucs4_t base_char;
	u8_mbtoucr(&base_char, str, len);

	if (!uc_is_graph(base_char))
		return;

	PangoRectangle ink_rect;
	pango_font_get_glyph_extents(cd->act_font, base_char, NULL, &ink_rect);

	pango_layout_set_text(cd->layout, str, len);
	int w, h;
	pango_layout_get_size(cd->layout, &w, &h);

	double wscale = (double)iglyph_w / w;
	double hscale = (double)iglyph_h / h;
	wscale *= uc_width(base_char, "C");

	double scale = wscale <= hscale ? wscale : hscale;

	double wnew = scale * w / (double)PANGO_SCALE;
	double hnew = scale * h / (double)PANGO_SCALE;

	double xoffs = 0.5 * (glyph_w - wnew);
	double yoffs = 0.5 * (glyph_h - hnew);

	cairo_matrix_t backup;
	cairo_get_matrix(cd->cr, &backup);
	cairo_move_to(cd->cr, x * glyph_w + xoffs, y * glyph_h + yoffs);
	cairo_scale(cd->cr, scale, scale);

	pango_cairo_show_layout(cd->cr, cd->layout);

	cairo_set_matrix(cd->cr, &backup);
}
static void
cairo_draw_text(Drawer *d, int x, int y, bool bold, bool it, const char *str, size_t len)
{
	const char *end = str + len;
	while (str != end) {
		const char *str_nxt = u8_grapheme_next(str, end);
		size_t graph_len = str_nxt - str;
		cairo_draw_glyph(d->data, x++, y, bold, it, str, graph_len);
		str = str_nxt;
	}
}
static void
cairo_fill_rect(Drawer *d, int x, int y, int w, int h)
{
	struct cairo_drawer *cd = d->data;
	cairo_rectangle(cd->cr, x * glyph_w, y * glyph_h, w * glyph_w, h * glyph_h);
	cairo_fill(cd->cr);
}
static void
cairo_stroke_rect(Drawer *d, int x, int y, int w, int h)
{
	struct cairo_drawer *cd = d->data;
	cairo_rectangle(cd->cr, x * glyph_w, y * glyph_h, w * glyph_w, h * glyph_h);
	cairo_stroke(cd->cr);
}

static Drawer *
make_cairo_drawer(struct cairo_drawer *cd, cairo_t *cr)
{
	cd->cr = cr;
	cd->layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(cd->layout, font);
	cd->d.data = cd;

	PangoContext *ctx = pango_layout_get_context(cd->layout);
	PangoFontMap *map = pango_context_get_font_map(ctx);
	cd->act_font = pango_font_map_load_font(map, ctx, font);

	if (iglyph_w == 0 && iglyph_h == 0) {
		/* get normal glyph extents
	 	 * this is used for measuring rectangle widths and character spacing,
	 	 * as well as proper rendering for possible non-monospace characters
	 	 * (think: CJK) in the loaded font
	 	 */
		PangoRectangle ink_rect;
		pango_font_get_glyph_extents(cd->act_font, 'A', NULL, &ink_rect);
		iglyph_w = ink_rect.width;
		iglyph_h = ink_rect.height;
		glyph_w = iglyph_w / (double)PANGO_SCALE;
		glyph_h = iglyph_h / (double)PANGO_SCALE;
	}

	cd->d.set_color = cairo_set_color;
	cd->d.clear = cairo_clear;
	cd->d.fill_rect = cairo_fill_rect;
	cd->d.stroke_rect = cairo_stroke_rect;
	cd->d.place_caret = cairo_place_caret;
	cd->d.draw_text = cairo_draw_text;
	return &cd->d;
}

static void
destroy_cairo_drawer(struct cairo_drawer *cd)
{
	g_object_unref(cd->layout);
	g_object_unref(cd->act_font);
}

static Window *gtk_spawner_ex(void);

static void
on_commit(GtkIMContext *ctx, gchar *str, Window *window)
{
	if (window->on_key_press)
		window->on_key_press(window, KM_NONE, str, strlen(str));
}

static gboolean
on_key_press(GtkWidget *widget, GdkEventKey *event, Window *window)
{
	WindowData *wdata = window->data;

	if (gtk_im_context_filter_keypress(wdata->im_ctx, event))
		return TRUE;

	KeyMods m = KM_NONE;
	if ((event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
		m |= KM_CONTROL;
	if ((event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK)
		m |= KM_SHIFT;

	switch (event->keyval) {
	case GDK_KEY_Return:
		if (window->on_enter_press)
			window->on_enter_press(window, m);
		break;
	case GDK_KEY_Tab:
		if (window->on_key_press)
			window->on_key_press(window, KM_NONE, "\t", 1);
		break;
	case GDK_KEY_BackSpace:
		if (window->on_backspace_press)
			window->on_backspace_press(window, m);
		break;
	case GDK_KEY_Delete:
		if (window->on_delete_press)
			window->on_delete_press(window, m);
		break;
	default:
		if (!isprint(event->keyval))
			return TRUE;

		if (window->on_key_press) {
			char ch = event->keyval;
			window->on_key_press(window, m, &ch, 1);
		}
		break;
	}

	return FALSE;
}

static gboolean
on_focus_in(GtkWidget *widget, GdkEvent *event, Window *window)
{
	WindowData *wdata = window->data;
	gtk_im_context_focus_in(wdata->im_ctx);
	if (window->on_focus_change)
		window->on_focus_change(window, true);
	return TRUE;
}

static gboolean
on_focus_out(GtkWidget *widget, GdkEvent *event, Window *window)
{
	WindowData *wdata = window->data;
	gtk_im_context_focus_out(wdata->im_ctx);
	if (window->on_focus_change)
		window->on_focus_change(window, false);
	return TRUE;
}

static gboolean
on_draw(GtkWidget *widget, cairo_t *cr, Window *window)
{
	WindowData *wdata = window->data;

	struct cairo_drawer cd;
	Drawer *d = make_cairo_drawer(&cd, cr);

	if (window->on_draw) {
		GtkAllocation *alloc = g_new(GtkAllocation, 1);
		gtk_widget_get_allocation(widget, alloc);
		int wlines = alloc->width / glyph_w;
		int hlines = alloc->height / glyph_h;
		g_free(alloc);

		window->on_draw(window, d, wlines, hlines);
	}

	destroy_cairo_drawer(&cd);

	return FALSE;
}

static void
on_destroy(GtkWidget *widget, Window *window)
{
	if (window->on_close)
		window->on_close(window);
}

static void
my_set_size(Window *win, int w, int h)
{
	WindowData *wdata = win->data;

	int wpix = w * glyph_w;
	int hpix = h * glyph_h;

	gtk_window_resize(GTK_WINDOW(wdata->window), wpix, hpix);
}
static void
my_get_size(Window *win, int *w, int *h)
{
	WindowData *wdata = win->data;

	GtkAllocation *alloc = g_new(GtkAllocation, 1);
	gtk_widget_get_allocation(wdata->darea, alloc);
	int wlines = alloc->width / glyph_w;
	int hlines = alloc->height / glyph_h;
	g_free(alloc);

	if (w)
		*w = wlines;
	if (h)
		*h = hlines;
}
static void
my_show(Window *win)
{
	WindowData *wdata = win->data;
	gtk_widget_show_all(wdata->window);
}
static void
my_close(Window *win)
{
	WindowData *wdata = win->data;
	gtk_window_close(GTK_WINDOW(wdata->window));
}
static void
my_redraw(Window *win)
{
	WindowData *wdata = win->data;
	gtk_widget_queue_draw(wdata->darea);
}

static Window *
gtk_spawner_ex(void)
{
	WindowData *wdata = malloc(sizeof(WindowData));
	memset(wdata, 0, sizeof(WindowData));
	Window *result = &wdata->base;

	result->data = wdata;
	result->set_size = my_set_size;
	result->get_size = my_get_size;
	result->close = my_close;
	result->show = my_show;
	result->redraw = my_redraw;

	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget *darea = gtk_drawing_area_new();
	GtkIMContext *im_ctx = gtk_im_multicontext_new();

	wdata->window = window;
	wdata->darea = darea;
	wdata->im_ctx = im_ctx;

	gtk_container_add(GTK_CONTAINER(window), darea);

	/*
	 * Set up GtkIMContext
	 * This is so that the user can use their own keyboard input method,
	 * and the editor can just be concerned with the unicode characters.
	 */
	GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
	gtk_im_context_set_client_window(im_ctx, gdk_window);

	g_signal_connect(im_ctx, "commit", G_CALLBACK(on_commit), result);
	g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), result);
	g_signal_connect(GTK_WIDGET(window), "focus-in-event", G_CALLBACK(on_focus_in), result);
	g_signal_connect(GTK_WIDGET(window), "focus-out-event", G_CALLBACK(on_focus_out), result);

	g_signal_connect(G_OBJECT(darea), "draw", G_CALLBACK(on_draw), result);

	/* resize */
	g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), result);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), window);

	gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
	gtk_window_set_title(GTK_WINDOW(window), "werk");

	return result;
}

int
werk_gtk_main(const char **filenames, int num_filenames, ConfigReader *rdr)
{
	static const char *const default_font = "monospace 10";
	const char *font_desc_str = default_font;
	config_add_opt_s(rdr, "gui.font", &font_desc_str);

	Window *win = gtk_spawner_ex();
	werk_init(win, filenames, num_filenames, rdr);

	font = pango_font_description_from_string(font_desc_str);

	if (font_desc_str != default_font)
		free((char *)font_desc_str);

	gtk_main();
	return 0;
}
