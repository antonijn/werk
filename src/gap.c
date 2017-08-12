#include "gap.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <unistr.h>
#include <unigbrk.h>
#include <fcntl.h>
#include <errno.h>
#include <unigbrk.h>

/* for debugging purposes */
static void
gbuf_debug_dump(GapBuf *gbuf)
{
	fprintf(stderr, "**********\n");
	fprintf(stderr, "start:     %p\n", gbuf->start);
	fprintf(stderr, "gap_offs:  %d\n", gbuf->gap_offs);
	fprintf(stderr, "size:      %d\n", gbuf->size);

	size_t pre_gap_size = gbuf->gap_offs;
	size_t post_gap_size = gbuf->size - pre_gap_size - gbuf->gap_size;
	fprintf(stderr,
	        "%.*s[gap_size: %d]%.*s\n",
	        pre_gap_size,
	        gbuf->start,
	        gbuf->gap_size,
	        post_gap_size,
	        gbuf->start + pre_gap_size + gbuf->gap_size);

	fprintf(stderr, "**********\n");
}

/*                _     _             
 *  _ __ ___  ___(_)___(_)_ __   __ _ 
 * | '__/ _ \/ __| |_  / | '_ \ / _` |
 * | | |  __/\__ \ |/ /| | | | | (_| |
 * |_|  \___||___/_/___|_|_| |_|\__, |
 *                              |___/ 
 */

/*
 * Calculate new buffer size based on previous buffer size 'prev'
 * and new required minimal buffer size 'req'
 *
 * get_new_size(0, 0) represents the initial buffer size.
 */
static size_t
get_new_size(size_t prev, size_t req)
{
	/*
	 * This resizing strategy grows the buffer by a kibibyte every time
	 * an upsize is needed, and shrinks the buffer by a kibibyte whenever
	 * the buffer _could have_ shrunk by two kibibytes.
	 *
	 * The shrinking rule ensures that continuously deleting and retyping
	 * text in the buffer doesn't cause a massive realloc() call each time.
	 */

	const size_t kibi = 1024;

	if (prev == 0 && req == 0)
		return kibi;

	if (req > prev) {
		size_t req_kbis = (req - prev + kibi - 1) / kibi;
		return prev + req_kbis * kibi;
	}

	if (req < prev) {
		size_t left_over_kbis = (prev - req) / kibi;
		if (left_over_kbis == 0)
			return prev;
		size_t shrink_kbis = left_over_kbis - 1;
		size_t new_size = prev - shrink_kbis * kibi;
		if (new_size < kibi)
			new_size = kibi;
		return new_size;
	}

	return prev;
}

void
gbuf_init(GapBuf *buf)
{
	size_t bsize = get_new_size(0, 0);
	buf->start = malloc(bsize);
	buf->gap_offs = 0;
	buf->size = buf->gap_size = bsize;
}

void
gbuf_destroy(GapBuf *buf)
{
	free(buf->start);
}

int
gbuf_resize(GapBuf *buf, size_t req)
{
	size_t new_size = get_new_size(buf->size, req);
	if (new_size == buf->size)
		return 0;

	size_t pre_gap_size = buf->gap_offs;
	size_t post_gap_size = buf->size - pre_gap_size - buf->gap_size;
	size_t end_gap_offs = buf->gap_offs + buf->gap_size;
	size_t new_end_gap_offs = new_size - post_gap_size;

	if (new_size > buf->size) {
		buf->start = realloc(buf->start, new_size);
		if (buf->start == NULL)
			return -1;
		memmove(buf->start + new_end_gap_offs, buf->start + end_gap_offs, post_gap_size);
		buf->gap_size += new_size - buf->size;
	} else if (new_size < buf->size) {
		memmove(buf->start + new_end_gap_offs, buf->start + end_gap_offs, post_gap_size);
		buf->start = realloc(buf->start, new_size);
		if (buf->start == NULL)
			return -1;
		buf->gap_size -= buf->size - new_size;
	}

	buf->size = new_size;

	return 0;
}

int
gbuf_auto_resize(GapBuf *buf)
{
	return gbuf_resize(buf, buf->size - buf->gap_size);
}

/*           _ _ _   _
 *   ___  __| (_) |_(_)_ __   __ _
 *  / _ \/ _` | | __| | '_ \ / _` |
 * |  __/ (_| | | |_| | | | | (_| |
 *  \___|\__,_|_|\__|_|_| |_|\__, |
 *                           |___/
 */

void
gbuf_clear(GapBuf *gbuf)
{
	gbuf_destroy(gbuf);
	gbuf_init(gbuf);
}

void
gbuf_write(GapBuf *gbuf, FILE *out)
{
	fwrite(gbuf->start, gbuf->gap_offs, 1, out);

	const char *snd_part = gbuf->start + gbuf->gap_offs + gbuf->gap_size;
	size_t snd_size = gbuf_len(gbuf) - gbuf->gap_offs;
	fwrite(snd_part, snd_size, 1, out);
}

int
gbuf_read(GapBuf *gbuf, FILE *in)
{
	if (fseek(in, 0, SEEK_END) < 0) {
		fprintf(stderr, "error reading buffer: file stream does not support seeking.\n");
		fprintf(stderr, "are you perhaps trying to open a network stream?\n");
		return -1;
	}

	long fsize = ftell(in);
	if (fsize < 0) {
		fprintf(stderr, "error reading buffer: ftell() failed.\n");
		fprintf(stderr, "are you perhaps trying to open a network stream?\n");
		return -1;
	}

	if (fseek(in, 0, SEEK_SET) < 0) {
		fprintf(stderr, "error reading buffer: file stream does not support seeking anymore.\n");
		return -1;
	}


	if (gbuf_resize(gbuf, fsize))
		return -1;

	if (fread(gbuf->start, 1, fsize, in) < fsize) {
		fprintf(stderr, "error reading buffer: fread() failed.\n");
		return -1;
	}

	if (u8_check(gbuf->start, fsize)) {
		fprintf(stderr, "error reading buffer: file is not UTF-8.\n");
		return -1;
	}

	gbuf->gap_offs = fsize;
	gbuf->gap_size = gbuf->size - fsize;
	return 0;
}

/* Force gap move */
static void gbuf_move_cursor(GapBuf *buf, gbuf_offs pos);

void
gbuf_insert_text(GapBuf *buf, gbuf_offs cursor, const char *str, size_t len)
{
	if (len > buf->gap_size)
		gbuf_resize(buf, buf->size - buf->gap_size + len);

	gbuf_move_cursor(buf, cursor);
	memcpy(buf->start + buf->gap_offs, str, len);
	buf->gap_offs += len;
	buf->gap_size -= len;
}
void
gbuf_backspace_grapheme(GapBuf *buf, gbuf_offs cursor)
{
	if (cursor == 0)
		return;

	gbuf_move_cursor(buf, cursor);
	const char *cursor_ptr = buf->start + cursor;
	const char *goto_ptr = u8_grapheme_prev(cursor_ptr, buf->start);
	size_t n = cursor_ptr - goto_ptr;
	buf->gap_offs -= n;
	buf->gap_size += n;

	gbuf_auto_resize(buf);
}
void
gbuf_delete_grapheme(GapBuf *buf, gbuf_offs cursor)
{
	if (cursor >= gbuf_len(buf))
		return;

	gbuf_move_cursor(buf, cursor);
	const char *cursor_stop = buf->start + cursor + buf->gap_size;
	const char *goto_ptr = u8_grapheme_next(cursor_stop, buf->start + buf->size);
	size_t n = goto_ptr - cursor_stop;

	buf->gap_size += n;
	gbuf_auto_resize(buf);
}
void
gbuf_delete_text(GapBuf *buf, gbuf_offs cursor, size_t len)
{
	if (cursor >= gbuf_len(buf))
		return;

	if (cursor + len > gbuf_len(buf))
		len = gbuf_len(buf) - cursor;

	gbuf_move_cursor(buf, cursor);
	buf->gap_size += len;
	gbuf_auto_resize(buf);
}

static gbuf_offs
ptr_to_offs(GapBuf *buf, const char *ptr)
{
	if (ptr >= buf->start + buf->gap_offs + buf->gap_size)
		return (ptr - buf->gap_size) - buf->start;

	return ptr - buf->start;
}
static char *
offs_to_ptr(GapBuf *buf, gbuf_offs offset)
{
	if (buf->start + offset < (buf->start + buf->gap_offs))
		return buf->start + offset;

	return buf->start + offset + buf->gap_size;
}

const char *
gbuf_get(GapBuf *buf, gbuf_offs offset)
{
	return offs_to_ptr(buf, offset);
}

int
gbuf_grapheme_next(GapBuf *buf, const char **str, size_t *size, gbuf_offs *offset)
{
	const char *gbuf_stop = buf->start + buf->size;
	const char *ptr = gbuf_get(buf, *offset);
	const char *nxt = u8_grapheme_next(ptr, gbuf_stop);
	if (!nxt)
		return -1;

	if (str)
		*str = ptr;
	if (size)
		*size = nxt - ptr;

	*offset = ptr_to_offs(buf, nxt);
	return 0;
}
int
gbuf_grapheme_prev(GapBuf *buf, const char **str, size_t *size, gbuf_offs *offset)
{
	const char *gbuf_stop = buf->start;
	const char *ptr = (*offset == buf->gap_offs)
	                ? (buf->start + buf->gap_offs)
	                : gbuf_get(buf, *offset);

	const char *prev = u8_grapheme_prev(ptr, gbuf_stop);
	if (!prev)
		return -1;

	if (str)
		*str = prev;
	if (size)
		*size = ptr - prev;

	*offset = ptr_to_offs(buf, prev);
	return 0;
}

void
gbuf_strcpy(GapBuf *buf, char *dest, gbuf_offs offset, size_t len)
{
	for (int i = 0; i < len; ++i)
		dest[i] = *gbuf_get(buf, offset + i);
}

static void
gbuf_move_cursor(GapBuf *buf, gbuf_offs pos)
{
	if (pos == buf->gap_offs)
		return;

	if (pos < buf->gap_offs) {
		/* Cursor moves backwards */

		size_t move_len = buf->gap_offs - pos;
		char *dest = buf->start + buf->gap_offs + buf->gap_size - move_len;
		const char *src = buf->start + pos;
		memmove(dest, src, move_len);
		buf->gap_offs = pos;
	} else {
		/* Cursor moves forwards */

		size_t move_len = pos - buf->gap_offs;
		char *dest = (buf->start + buf->gap_offs);
		const char *src = (buf->start + buf->gap_offs) + buf->gap_size;
		memmove(dest, src, move_len);
		buf->gap_offs = pos;
	}
}


/*        _            __          _             ____  
 *   __ _| |__  _   _ / _|   _ __ (_)_ __   ___ / /\ \\
 *  / _` | '_ \| | | | |_   | '_ \| | '_ \ / _ \ |  | |
 * | (_| | |_) | |_| |  _|  | |_) | | |_) |  __/ |  | |
 *  \__, |_.__/ \__,_|_|____| .__/|_| .__/ \___| |  | |
 *  |___/             |_____|_|     |_|         \_\/_/ 
 */

/*
 * Only used by gbuf_pipe()
 *
 * Run file 'cmd' and open pipes for communication:
 * - pipes[0] -> stdin
 * - pipes[1] <- stdout
 * - pipes[2] <- stderr
 */
static pid_t
opencmd(const char *cmd, int pipes[3])
{
	int in[2];
	if (pipe(in) < 0)
		goto out_fdin;

	int out[2];
	if (pipe(out) < 0)
		goto out_fdout;

	int err[2];
	if (pipe(err) < 0)
		goto out_fderr;

	pid_t pid = fork();
	switch (pid) {
	case -1:
		goto out;

	case 0:
		close(in[1]);
		close(out[0]);
		close(err[0]);

		dup2(in[0], 0);
		dup2(out[1], 1);
		dup2(err[1], 2);

		char bash[] = "/bin/bash";
		char *cmd_buf = malloc(strlen(cmd) + 1);
		strcpy(cmd_buf, cmd);
		char *args[] = { bash, "-c", cmd_buf, NULL };
		execv(bash, args);

		/* an error has occured */
		exit(1);

	default:
		close(in[0]);
		close(out[1]);
		close(err[1]);

		pipes[0] = in[1];
		pipes[1] = out[0];
		pipes[2] = err[0];
		break;
	}

	return pid;

out:
	close(err[0]);
	close(err[1]);

out_fderr:
	close(out[0]);
	close(out[1]);

out_fdout:
	close(in[0]);
	close(in[1]);

out_fdin:
	return -1;
}
/*
 * Only used by gbuf_pipe()
 *
 * Pipes a continuous unit of buffer text (not containing the gap) through
 * the given pipes.
 *
 * write_start is where the running command can start writing its output,
 * this value may coincide with start, and may be inside the buffer gap.
 * The function makes sure not to overwrite any text before it has been
 * written to pipes[0].
 *
 * Note that pipes[1] should be set to non-blocking reading.
 */
static char *
pipe_buf_unit(GapBuf *buf,
             int pipes[3],
             char *write_start,
             char *start,
             char *stop)
{
	const size_t max_bsize = 512;

	char *read_from = start;
	while (read_from < stop) {
		size_t bsize = stop - read_from;
		if (bsize > max_bsize)
			bsize = max_bsize;

		if (write(pipes[0], read_from, bsize) < 0)
			return NULL;

		size_t writable = read_from - write_start + bsize;

		/* pipes[1] should be set to non-blocking */
		ssize_t write_back = read(pipes[1], write_start, writable);

		if (write_back < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK)
				return NULL;

			/* io would have blocked */
			write_back = 0;
		}

		write_start += write_back;
		read_from += bsize;
	}

	return write_start;
}
/*
 * Pipe buffer text 'start' - 'stop' through command 'cmd', writing output
 * back to the buffer.
 */
int
gbuf_pipe(GapBuf *buf, const char *cmd, gbuf_offs start_offs, size_t len)
{
	int ecode = 0;

	int pipes[3];
	if (opencmd(cmd, pipes) == 0)
		return -1;

	/* Set up non-blocking pipe reading */
	int flags = fcntl(pipes[1], F_GETFL, 0);
	fcntl(pipes[1], F_SETFL, flags | O_NONBLOCK);

	char *start = offs_to_ptr(buf, start_offs);
	char *stop = offs_to_ptr(buf, start_offs + len);

	char *write_start = start;
	if (start < (buf->start + buf->gap_offs) && stop > (buf->start + buf->gap_offs)) {
		/* Section contains gap */

		write_start = pipe_buf_unit(buf,
		                            pipes,
		                            write_start,
		                            start,
		                            (buf->start + buf->gap_offs));
		if (write_start == NULL) {
			ecode = -1;
			close(pipes[0]);
			goto stop;
		}

		write_start = pipe_buf_unit(buf,
		                            pipes,
		                            write_start,
		                            (buf->start + buf->gap_offs) + buf->gap_size,
		                            stop);
		if (write_start == NULL) {
			ecode = -1;
			close(pipes[0]);
			goto stop;
		}

		/* Move gap to end of editing section */
		buf->gap_offs = write_start - buf->start;
		buf->gap_size = stop - write_start;

	} else {
		/* Section is continuous */

		write_start = pipe_buf_unit(buf,
		                            pipes,
		                            write_start,
		                            start,
		                            stop);
		if (write_start == NULL) {
			ecode = -1;
			close(pipes[0]);
			goto stop;
		}

		/* Move gap (outside the editing section) into the editing
		 * section, expanding its size with the area not yet filled */
		gbuf_move_cursor(buf, ptr_to_offs(buf, write_start));
		buf->gap_size += stop - write_start;
	}

	close(pipes[0]);

	/* Reset to blocking io */
	fcntl(pipes[1], F_SETFL, flags);

	ssize_t write_size;
	while (write_size = read(pipes[1], (buf->start + buf->gap_offs), buf->gap_size)) {
		if (write_size < 0) {
			ecode = -1;
			goto stop;
		}
		buf->gap_offs += write_size;
		buf->gap_size -= write_size;
		if (buf->gap_size == 0)
			gbuf_resize(buf, buf->size + 512);
	}

	gbuf_auto_resize(buf);

stop:
	close(pipes[1]);
	close(pipes[2]);

	return ecode;
}
