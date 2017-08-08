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
 * Initialize gap buffer
 */
void gbuf_init(GapBuf *buf);
/*
 * Destroy gap buffer
 */
void gbuf_destroy(GapBuf *buf);

static inline size_t gbuf_len(GapBuf *buf)
{
	return buf->size - buf->gap_size;
}

void gbuf_write(GapBuf *gbuf, FILE *out);
void gbuf_read(GapBuf *gbuf, FILE *in);

/*
 * Insert given text in buffer gap
 */
void gbuf_insert_text(GapBuf *buf, gbuf_offs cursor, const char *str, size_t len);
void gbuf_backspace_grapheme(GapBuf *buf, gbuf_offs cursor);
void gbuf_delete_grapheme(GapBuf *buf, gbuf_offs cursor);
void gbuf_delete_text(GapBuf *buf, gbuf_offs cursor, size_t len);
/*
 * Get byte at logical offset 'offset'
 */
const char *gbuf_get(GapBuf *buf, gbuf_offs offset);
int gbuf_grapheme_next(GapBuf *buf, const char **str, size_t *size, gbuf_offs *offset);
int gbuf_grapheme_prev(GapBuf *buf, const char **str, size_t *size, gbuf_offs *offset);

void gbuf_strcpy(GapBuf *buf, char *dest, gbuf_offs offset, size_t len);
gbuf_offs gbuf_get_gap_offset(GapBuf *buf);

/*
 * Resize buffer to accomodate 'req' characters
 */
int gbuf_resize(GapBuf *buf, size_t req);
/*
 * Shrink buffer to its current minimal size
 */
int gbuf_auto_resize(GapBuf *buf);
/*
 * Pipe given buffer text through command
 */
int gbuf_pipe(GapBuf *buf, const char *cmd, gbuf_offs start, size_t len);

#endif
