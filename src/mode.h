#ifndef MODE_H
#define MODE_H

#include "edit.h"

/*
 * Pop last mode from mode stack.
 * Release resources used by the mode.
 */
void pop_mode(Buffer *buf);

/*
 * Push select mode to mode stack.
 */
void push_select_mode(Buffer *buf);

/*
 * Push insert mode to mode stack.
 */
void push_insert_mode(Buffer *buf);

#endif
