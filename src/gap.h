#ifndef GAP_H
#define GAP_H

#include <stdio.h>
#include <stddef.h>

/*
 * NOTE: Do not use to offset gbuf->start directly!
 * Use gbuf_get()
 */
typedef int gbuf_offs;

typedef struct gap_buf GapBuf;

struct gap_buf {
	char *start;
	size_t size;

	size_t gap_size;
	gbuf_offs gap_offs;
};

/*
 * Initialize empty gap buffer.
 */
void gbuf_init(GapBuf *buf);
/*
 * Destroy gap buffer.
 */
void gbuf_destroy(GapBuf *buf);

/*
 * Buffer length in bytes.
 */
static inline size_t gbuf_len(GapBuf *buf)
{
	return buf->size - buf->gap_size;
}

/*
 * Write buffer contents to file.
 */
void gbuf_write(GapBuf *gbuf, FILE *out);
/*
 * Read file `in' to gap buffer.
 * NOTE: Performs no UTF-8 validation as of yet.
 */
void gbuf_read(GapBuf *gbuf, FILE *in);

/*
 * Insert given text at location `cursor'.
 */
void gbuf_insert_text(GapBuf *buf, gbuf_offs cursor, const char *str, size_t len);
/*
 * Delete a single grapheme backwards from `cursor'.
 */
void gbuf_backspace_grapheme(GapBuf *buf, gbuf_offs cursor);
/*
 * Delete a single grapheme forwards from `cursor'.
 */
void gbuf_delete_grapheme(GapBuf *buf, gbuf_offs cursor);
/*
 * Remove text from buffer and automatically resize.
 */
void gbuf_delete_text(GapBuf *buf, gbuf_offs cursor, size_t len);

/*
 * Get byte at logical offset 'offset'
 */
const char *gbuf_get(GapBuf *buf, gbuf_offs offset);

/*
 * Moves `marker' to next grapheme, stores the current grapheme in `str'
 * and its size in `size'.
 */
int gbuf_grapheme_next(GapBuf *buf, const char **str, size_t *size, gbuf_offs *offset);
/*
 * Moves `marker' to previous grapheme, stores the current grapheme in
 * `str' and its size in `size'.
 */
int gbuf_grapheme_prev(GapBuf *buf, const char **str, size_t *size, gbuf_offs *offset);

/*
 * Copy excerpt from gap buffer to `dest'.
 */
void gbuf_strcpy(GapBuf *buf, char *dest, gbuf_offs offset, size_t len);

/*
 * Get current location of buffer gap.
 */
gbuf_offs gbuf_get_gap_offset(GapBuf *buf);

/*
 * Resize buffer to accomodate `req' characters.
 */
int gbuf_resize(GapBuf *buf, size_t req);
/*
 * Shrink buffer to its current minimal size.
 */
int gbuf_auto_resize(GapBuf *buf);
/*
 * Pipe given buffer text through command.
 */
int gbuf_pipe(GapBuf *buf, const char *cmd, gbuf_offs start, size_t len);

#endif
