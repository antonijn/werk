#ifndef UNDO_H
#define UNDO_H

#include <stddef.h>

typedef struct change {
	struct change *next;

	long pos;
	/* Text is to be deleted if `text == NULL' */
	const char *text;
	size_t size;
} Change;

typedef void (*text_adder)(long pos, const char *text, size_t size, void *udata);
typedef void (*text_deleter)(long pos, size_t size, char *copy_deleted_text_here, void *udata);

typedef struct future_node FutureNode;

typedef struct undo_tree {
	struct undo_tree *past;
	/* ring buffer of all futures */
	FutureNode *futures;

	/* acts as a staging area until changes are commited */
	Change *first_change, *last_change;
} UndoTree;

struct future_node {
	UndoTree tree;

	/* ring buffer structure */
	FutureNode *next;
};

UndoTree *undo_tree_init(void);
void undo_tree_destroy(UndoTree *present);

void notify_add(UndoTree *present, long pos, size_t size);
void notify_delete(UndoTree *present, long pos, const char *text, size_t size);

void commit(UndoTree **present);

void undo(UndoTree **present, text_adder adder, text_deleter deleter, void *udata);
void redo(UndoTree **present, FutureNode *fut, text_adder adder, text_deleter deleter, void *udata);

#endif
