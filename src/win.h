#ifndef WERK_WIN_H
#define WERK_WIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint8_t r, g, b;
} RGB;

typedef struct drawer Drawer;

struct drawer {
	void *data;

	void (*set_color)(Drawer *d, RGB color);
	void (*clear)(Drawer *d);
	void (*fill_rect)(Drawer *d, int xa, int ya, int w, int h);
	void (*stroke_rect)(Drawer *d, int xa, int ya, int w, int h);
	void (*place_caret)(Drawer *d, int xa, int ya, bool visible);
	void (*draw_text)(Drawer *d,
	                  int x, int y,
	                  bool bold, bool it,
	                  const char *str, size_t len);
};

static inline void
drw_set_color(Drawer *d, RGB color)
{
	d->set_color(d, color);
}
static inline void
drw_clear(Drawer *d)
{
	d->clear(d);
}
static inline void
drw_fill_rect(Drawer *d, int xa, int ya, int w, int h)
{
	d->fill_rect(d, xa, ya, w, h);
}
static inline void
drw_stroke_rect(Drawer *d, int xa, int ya, int w, int h)
{
	d->stroke_rect(d, xa, ya, w, h);
}
static inline void
drw_place_caret(Drawer *d, int x, int y, bool visible)
{
	d->place_caret(d, x, y, visible);
}
static inline void
drw_draw_text(Drawer *d,
              int x, int y,
              bool bold, bool it,
              const char *str, size_t len)
{
	d->draw_text(d, x, y, bold, it, str, len);
}

typedef enum key_mods {
	KM_NONE = 0x00,
	KM_SHIFT = 0x01,
	KM_CONTROL = 0x02,
} KeyMods;

typedef enum win_kind {
	WK_NONE = 0x00,
	WK_CLOSE_ON_NOFOCUS = 0x01,
} WinKind;

typedef struct window Window;

struct window {
	void *data, *user_data;

	void (*on_draw)(Window *win, Drawer *d, int w, int h);
	void (*on_key_press)(Window *win, KeyMods mods, const char *ch, size_t len);
	void (*on_enter_press)(Window *win, KeyMods mods);
	void (*on_backspace_press)(Window *win, KeyMods mods);
	void (*on_delete_press)(Window *win, KeyMods mods);
	void (*on_focus_change)(Window *win, bool focus);
	void (*on_close)(Window *win);

	void (*get_size)(Window *win, int *w, int *h);
	void (*set_size)(Window *win, int w, int h);
	void (*show)(Window *win);
	void (*close)(Window *win);
	void (*redraw)(Window *win);
};

static inline void
win_set_size(Window *win, int w, int h)
{
	win->set_size(win, w, h);
}
static inline void
win_get_size(Window *win, int *w, int *h)
{
	win->get_size(win, w, h);
}
static inline void
win_show(Window *win)
{
	win->show(win);
}
static inline void
win_close(Window *win)
{
	win->close(win);
}
static inline void
win_redraw(Window *win)
{
	win->redraw(win);
}

#endif
