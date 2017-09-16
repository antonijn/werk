#include <werk/undo.h>
#include <stdlib.h>
#include <string.h>

UndoTree *
undo_tree_init(void)
{
	return NULL;
}

void
undo_tree_destroy(UndoTree *present)
{
}

void
notify_add(UndoTree *present, long pos, size_t size)
{
}

void
notify_delete(UndoTree *present, long pos, const char *text, size_t size)
{
}

void
commit(UndoTree **present)
{
}

void
undo(UndoTree **present, text_adder adder, text_deleter deleter, void *udata)
{
}

void
redo(UndoTree **present, FutureNode *fut, text_adder adder, text_deleter deleter, void *udata)
{
}
