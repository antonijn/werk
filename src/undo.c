#include <werk/undo.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
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
	assert(past != NULL);

	prev_pres->next_future = past->futures;
	past->futures = prev_pres;

	*present = new_pres;
}

/*
 * Returns list of Changes required to un-exec
 */
static Change *
exec_changes(Change *changes, text_adder adder, text_deleter deleter, void *udata)
{
	Change *result = NULL;

	while (changes) {
		Change *next = changes->next;
		changes->next = NULL;

		ChangePos from = changes->from;
		ChangePos until = changes->until;

		if (changes->text) {
			adder(from, until, changes->text, udata);
			free((char *)changes->text);
			changes->text = NULL;
		} else {
			char *new_text = malloc(until.offset - from.offset);
			deleter(from, until, new_text, udata);
			changes->text = new_text;
		}

		changes->next = result;
		result = changes;

		changes = next;
	}

	return result;
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
	to_undo->changes = exec_changes(to_undo->changes, adder, deleter, udata);
	present->past = to_undo->past;
}

void
redo(UndoTree *present, UndoTree *go_here, text_adder adder, text_deleter deleter, void *udata)
{
	UndoTree *go_here_parent = present->past;
	assert(go_here_parent != NULL);

	bool is_valid_go_here = false;
	for (UndoTree *fut = go_here_parent->futures; fut; fut = fut->next_future) {
		if (fut == go_here) {
			is_valid_go_here = true;
			break;
		}
	}

	assert(is_valid_go_here);

	go_here->changes = exec_changes(go_here->changes, adder, deleter, udata);
	present->past = go_here;
}
