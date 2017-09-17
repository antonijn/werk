#include <werk/undo.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

UndoTree *
undo_tree_init(void)
{
	UndoTree *root = malloc(sizeof(UndoTree));
	memset(root, 0, sizeof(*root));

	UndoTree *present = malloc(sizeof(UndoTree));
	memset(present, 0, sizeof(*present));
	present->past = root;
	return present;
}

void
undo_tree_destroy(UndoTree *present)
{
}

void
notify_add(UndoTree *present, ChangePos from, ChangePos until)
{
	notify_delete(present, from, until, NULL);
}

void
notify_delete(UndoTree *present, ChangePos from, ChangePos until, const char *text)
{
	Change *ch = malloc(sizeof(Change));
	memset(ch, 0, sizeof(*ch));

	ch->from = from;
	ch->until = until;

	if (text) {
		size_t size = until.offset - from.offset;
		ch->text = malloc(size);
		memcpy((char *)ch->text, text, size);
	}

	ch->next = present->changes;
	present->changes = ch;
}

void
commit(UndoTree **present)
{
	UndoTree *prev_pres = *present;

	if (!prev_pres->changes)
		return;

	UndoTree *new_pres = malloc(sizeof(UndoTree));
	memset(new_pres, 0, sizeof(*new_pres));
	new_pres->past = prev_pres;

	UndoTree *past = prev_pres->past;
	if (past) {
		prev_pres->next_future = past->futures;
		past->futures = prev_pres;
	}

	*present = new_pres;
}

void
undo(UndoTree *present, text_adder adder, text_deleter deleter, void *udata)
{
	assert(present->changes == NULL);

	UndoTree *to_undo = present->past;
	assert(to_undo != NULL);

	/* Detect root node */
	if (!to_undo->changes)
		return;

	/* The undo node becomes a redo node */
	Change *undo_changes = to_undo->changes;
	Change *redo_changes = NULL;

	while (undo_changes) {
		Change *next = undo_changes->next;
		undo_changes->next = NULL;

		ChangePos from = undo_changes->from;
		ChangePos until = undo_changes->until;

		if (undo_changes->text) {
			adder(from, until, undo_changes->text, udata);
			free((char *)undo_changes->text);
			undo_changes->text = NULL;
		} else {
			char *new_text = malloc(until.offset - from.offset);
			deleter(from, until, new_text, udata);
			undo_changes->text = new_text;
		}

		undo_changes->next = redo_changes;
		redo_changes = undo_changes;

		undo_changes = next;
	}

	to_undo->changes = redo_changes;
	present->past = to_undo->past;
}

void
redo(UndoTree *present, UndoTree *go_here, text_adder adder, text_deleter deleter, void *udata)
{
}
