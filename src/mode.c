#include "edit.h"
#include "mode.h"

#include <stdlib.h>

void
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

void
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
		buf_delete_selection(buf);
		push_insert_mode(buf);
		break;

	case 'd':
		buf_delete_selection(buf);
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

void
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
	buf_move_cursor(buf, -1, true);
	buf_delete_selection(buf);
}

static void
im_on_delete_press(Buffer *buf, Mode *mode, KeyMods mods)
{
	buf_move_cursor(buf, 1, true);
	buf_delete_selection(buf);
}
