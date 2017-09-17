#ifndef UNDO_H
#define UNDO_H

#include <stddef.h>

/* Significant overlap with BufferMarker */
typedef struct {
	long offset;
	int line, col;
} ChangePos;

typedef struct change {
	struct change *next;

	ChangePos from, until;
	/* Text is to be deleted if `text == NULL' */
	const char *text;
} Change;

typedef void (*text_adder)(ChangePos from, ChangePos until, const char *text, void *udata);
typedef void (*text_deleter)(ChangePos from, ChangePos until, char *copy_deleted_text_here, void *udata);

typedef struct undo_tree {
	struct undo_tree *past;

	/* all futures */
	struct undo_tree *futures;
	struct undo_tree *next_future; /* next tree sibling */

	/* acts as a staging area until changes are commited */
	Change *changes;
} UndoTree;

UndoTree *undo_tree_init(void);
void undo_tree_destroy(UndoTree *present);

void notify_add(UndoTree *present, ChangePos from, ChangePos until);
void notify_delete(UndoTree *present, ChangePos from, ChangePos until, const char *text);

void commit(UndoTree **present);

void undo(UndoTree *present, text_adder adder, text_deleter deleter, void *udata);
void redo(UndoTree *present, UndoTree *go_here, text_adder adder, text_deleter deleter, void *udata);

#endif
