#ifndef __CYANFS_LIST_HEADER__
#define __CYANFS_LIST_HEADER__

#define cyanfs_offsetof(type, member) ((unsigned long long)(&(((type *)0)->member)))
#define cyanfs_container_of(ptr, type, member) ((type *)((unsigned long long)(ptr)-cyanfs_offsetof(type, member)))

struct cyanfs_list_head {
	struct cyanfs_list_head *next, *prev;
};

/*
 * Circular doubly linked list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

#define CYANFS_LIST_HEAD_INIT(name)                                                                                    \
	{                                                                                                              \
		&(name), &(name)                                                                                       \
	}

#define CYANFS_LIST_HEAD(name) struct cyanfs_list_head name = CYANFS_LIST_HEAD_INIT(name)

/**
 * INIT_LIST_HEAD - Initialize a list_head structure
 * @list: list_head structure to be initialized.
 *
 * Initializes the list_head to point to itself.  If it is a list header,
 * the result is an empty list.
 */
static inline void CYANFS_INIT_LIST_HEAD(struct cyanfs_list_head *list)
{
	list->next = list;
	list->prev = list;
}

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __cyanfs_list_add(struct cyanfs_list_head *_new, struct cyanfs_list_head *prev,
				     struct cyanfs_list_head *next)
{
	next->prev = _new;
	_new->next = next;
	_new->prev = prev;
	prev->next = _new;
}

/**
 * list_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void cyanfs_list_add(struct cyanfs_list_head *_new, struct cyanfs_list_head *head)
{
	__cyanfs_list_add(_new, head, head->next);
}

/**
 * list_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void cyanfs_list_add_tail(struct cyanfs_list_head *_new, struct cyanfs_list_head *head)
{
	__cyanfs_list_add(_new, head->prev, head);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __cyanfs_list_del(struct cyanfs_list_head *prev, struct cyanfs_list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty() on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void cyanfs_list_del(struct cyanfs_list_head *entry)
{
	__cyanfs_list_del(entry->prev, entry->next);
}

/**
 * list_is_first -- tests whether @list is the first entry in list @head
 * @list: the entry to test
 * @head: the head of the list
 */
static inline int cyanfs_list_is_first(const struct cyanfs_list_head *list, const struct cyanfs_list_head *head)
{
	return list->prev == head;
}

/**
 * list_is_last - tests whether @list is the last entry in list @head
 * @list: the entry to test
 * @head: the head of the list
 */
static inline int cyanfs_list_is_last(const struct cyanfs_list_head *list, const struct cyanfs_list_head *head)
{
	return list->next == head;
}

/**
 * list_is_head - tests whether @list is the list @head
 * @list: the entry to test
 * @head: the head of the list
 */
static inline int cyanfs_list_is_head(const struct cyanfs_list_head *list, const struct cyanfs_list_head *head)
{
	return list == head;
}

/**
 * list_empty - tests whether a list is empty
 * @head: the list to test.
 */
static inline int cyanfs_list_empty(const struct cyanfs_list_head *head)
{
	return head->next == head;
}

/**
 * list_is_singular - tests whether a list has just one entry.
 * @head: the list to test.
 */
static inline int cyanfs_list_is_singular(const struct cyanfs_list_head *head)
{
	return !cyanfs_list_empty(head) && (head->next == head->prev);
}

static inline void __cyanfs_list_splice(const struct cyanfs_list_head *list, struct cyanfs_list_head *prev,
					struct cyanfs_list_head *next)
{
	struct cyanfs_list_head *first = list->next;
	struct cyanfs_list_head *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

/**
 * list_splice - join two lists, this is designed for stacks
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void cyanfs_list_splice(const struct cyanfs_list_head *list, struct cyanfs_list_head *head)
{
	if (!cyanfs_list_empty(list))
		__cyanfs_list_splice(list, head, head->next);
}

/**
 * list_splice_tail - join two lists, each list being a queue
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void cyanfs_list_splice_tail(struct cyanfs_list_head *list, struct cyanfs_list_head *head)
{
	if (!cyanfs_list_empty(list))
		__cyanfs_list_splice(list, head->prev, head);
}

/**
 * list_splice_init - join two lists and reinitialise the emptied list.
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * The list at @list is reinitialised
 */
static inline void cyanfs_list_splice_init(struct cyanfs_list_head *list, struct cyanfs_list_head *head)
{
	if (!cyanfs_list_empty(list)) {
		__cyanfs_list_splice(list, head, head->next);
		CYANFS_INIT_LIST_HEAD(list);
	}
}

/**
 * list_splice_tail_init - join two lists and reinitialise the emptied list
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * Each of the lists is a queue.
 * The list at @list is reinitialised
 */
static inline void cyanfs_list_splice_tail_init(struct cyanfs_list_head *list, struct cyanfs_list_head *head)
{
	if (!cyanfs_list_empty(list)) {
		__cyanfs_list_splice(list, head->prev, head);
		CYANFS_INIT_LIST_HEAD(list);
	}
}

/**
 * list_entry - get the struct for this entry
 * @ptr:	the &struct list_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_head within the struct.
 */
#define cyanfs_list_entry(ptr, type, member) cyanfs_container_of(ptr, type, member)

/**
 * list_first_entry - get the first element from a list
 * @ptr:	the list head to take the element from.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_head within the struct.
 *
 * Note, that list is expected to be not empty.
 */
#define cyanfs_list_first_entry(ptr, type, member) cyanfs_list_entry((ptr)->next, type, member)

/**
 * list_last_entry - get the last element from a list
 * @ptr:	the list head to take the element from.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_head within the struct.
 *
 * Note, that list is expected to be not empty.
 */
#define cyanfs_list_last_entry(ptr, type, member) cyanfs_list_entry((ptr)->prev, type, member)

/**
 * list_for_each	-	iterate over a list
 * @pos:	the &struct list_head to use as a loop cursor.
 * @head:	the head for your list.
 */
#define cyanfs_list_for_each(pos, head) for (pos = (head)->next; !cyanfs_list_is_head(pos, (head)); pos = pos->next)

/**
 * list_for_each_continue - continue iteration over a list
 * @pos:	the &struct list_head to use as a loop cursor.
 * @head:	the head for your list.
 *
 * Continue to iterate over a list, continuing after the current position.
 */
#define cyanfs_list_for_each_continue(pos, head)                                                                       \
	for (pos = pos->next; !cyanfs_list_is_head(pos, (head)); pos = pos->next)

/**
 * list_for_each_prev	-	iterate over a list backwards
 * @pos:	the &struct list_head to use as a loop cursor.
 * @head:	the head for your list.
 */
#define cyanfs_list_for_each_prev(pos, head)                                                                           \
	for (pos = (head)->prev; !cyanfs_list_is_head(pos, (head)); pos = pos->prev)

/**
 * list_for_each_safe - iterate over a list safe against removal of list entry
 * @pos:	the &struct list_head to use as a loop cursor.
 * @n:		another &struct list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define cyanfs_list_for_each_safe(pos, n, head)                                                                        \
	for (pos = (head)->next, n = pos->next; !cyanfs_list_is_head(pos, (head)); pos = n, n = pos->next)

/**
 * list_for_each_prev_safe - iterate over a list backwards safe against removal of list entry
 * @pos:	the &struct list_head to use as a loop cursor.
 * @n:		another &struct list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define cyanfs_list_for_each_prev_safe(pos, n, head)                                                                   \
	for (pos = (head)->prev, n = pos->prev; !cyanfs_list_is_head(pos, (head)); pos = n, n = pos->prev)

/**
 * list_entry_is_head - test if the entry points to the head of the list
 * @pos:	the type * to cursor
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define cyanfs_list_entry_is_head(pos, head, member) (&pos->member == (head))

#endif
