#ifndef __CYANFS_RB_TREE_H__
#define __CYANFS_RB_TREE_H__

/* Macros that define a red-black tree */
#define CYANFS_RB_HEAD(name, type)                                                                                     \
	struct name {                                                                                                  \
		struct type *rbh_root; /* root of the tree */                                                          \
	}

#define CYANFS_RB_INITIALIZER(root)                                                                                    \
	{                                                                                                              \
		NULL                                                                                                   \
	}

#define CYANFS_RB_INIT(root)                                                                                           \
	do {                                                                                                           \
		(root)->rbh_root = NULL;                                                                               \
	} while (0)

#define CYANFS_RB_BLACK 0
#define CYANFS_RB_RED 1
#define CYANFS_RB_ENTRY(type)                                                                                          \
	struct {                                                                                                       \
		struct type *rbe_left; /* left element */                                                              \
		struct type *rbe_right; /* right element */                                                            \
		struct type *rbe_parent; /* parent element and node color*/                                            \
	}

#define CYANFS_RB_LEFT(elm, field) (elm)->field.rbe_left
#define CYANFS_RB_RIGHT(elm, field) (elm)->field.rbe_right
#define CYANFS_RB_PARENT_GET(elm, field) ((typeof(elm))((uint64_t)((elm)->field.rbe_parent) & ~1ULL))
#define CYANFS_RB_PARENT_SET(elm, field, parent)                                                                       \
	do {                                                                                                           \
		uint64_t c = CYANFS_RB_COLOR_GET(elm, field);                                                          \
		(elm)->field.rbe_parent = (typeof(elm))((uint64_t)(parent) | c);                                       \
	} while (0)
#define CYANFS_RB_COLOR_GET(elm, field) (((uint64_t)(elm)->field.rbe_parent) & 1)
#define CYANFS_RB_COLOR_SET(elm, field, color)                                                                         \
	do {                                                                                                           \
		uint64_t p = (uint64_t)CYANFS_RB_PARENT_GET(elm, field);                                               \
		(elm)->field.rbe_parent = (typeof(elm))(p | (uint64_t)(color));                                        \
	} while (0)
#define CYANFS_RB_ROOT(head) (head)->rbh_root
#define CYANFS_RB_EMPTY(head) (CYANFS_RB_ROOT(head) == NULL)

#define CYANFS_RB_SET(elm, parent, field)                                                                              \
	do {                                                                                                           \
		CYANFS_RB_PARENT_SET(elm, field, parent);                                                              \
		CYANFS_RB_LEFT(elm, field) = CYANFS_RB_RIGHT(elm, field) = NULL;                                       \
		CYANFS_RB_COLOR_SET(elm, field, CYANFS_RB_RED);                                                        \
	} while (0)

#define CYANFS_RB_SET_BLACKRED(black, red, field)                                                                      \
	do {                                                                                                           \
		CYANFS_RB_COLOR_SET(black, field, CYANFS_RB_BLACK);                                                    \
		CYANFS_RB_COLOR_SET(red, field, CYANFS_RB_RED);                                                        \
	} while (0)

#ifndef CYANFS_RB_AUGMENT
#define CYANFS_RB_AUGMENT(x)                                                                                           \
	do {                                                                                                           \
	} while (0)
#endif

#define CYANFS_RB_ROTATE_LEFT(head, elm, tmp, field)                                                                   \
	do {                                                                                                           \
		(tmp) = CYANFS_RB_RIGHT(elm, field);                                                                   \
		if ((CYANFS_RB_RIGHT(elm, field) = CYANFS_RB_LEFT(tmp, field)) != NULL) {                              \
			CYANFS_RB_PARENT_SET(CYANFS_RB_LEFT(tmp, field), field, (elm));                                \
		}                                                                                                      \
		CYANFS_RB_AUGMENT(elm);                                                                                \
		CYANFS_RB_PARENT_SET(tmp, field, CYANFS_RB_PARENT_GET(elm, field));                                    \
		if (CYANFS_RB_PARENT_GET(tmp, field) != NULL) {                                                        \
			if ((elm) == CYANFS_RB_LEFT(CYANFS_RB_PARENT_GET(elm, field), field))                          \
				CYANFS_RB_LEFT(CYANFS_RB_PARENT_GET(elm, field), field) = (tmp);                       \
			else                                                                                           \
				CYANFS_RB_RIGHT(CYANFS_RB_PARENT_GET(elm, field), field) = (tmp);                      \
		} else                                                                                                 \
			(head)->rbh_root = (tmp);                                                                      \
		CYANFS_RB_LEFT(tmp, field) = (elm);                                                                    \
		CYANFS_RB_PARENT_SET(elm, field, (tmp));                                                               \
		CYANFS_RB_AUGMENT(tmp);                                                                                \
		if ((CYANFS_RB_PARENT_GET(tmp, field)))                                                                \
			CYANFS_RB_AUGMENT(CYANFS_RB_PARENT_GET(tmp, field));                                           \
	} while (0)

#define CYANFS_RB_ROTATE_RIGHT(head, elm, tmp, field)                                                                  \
	do {                                                                                                           \
		(tmp) = CYANFS_RB_LEFT(elm, field);                                                                    \
		if ((CYANFS_RB_LEFT(elm, field) = CYANFS_RB_RIGHT(tmp, field)) != NULL) {                              \
			CYANFS_RB_PARENT_SET(CYANFS_RB_RIGHT(tmp, field), field, (elm));                               \
		}                                                                                                      \
		CYANFS_RB_AUGMENT(elm);                                                                                \
		CYANFS_RB_PARENT_SET(tmp, field, CYANFS_RB_PARENT_GET(elm, field));                                    \
		if (CYANFS_RB_PARENT_GET(tmp, field) != NULL) {                                                        \
			if ((elm) == CYANFS_RB_LEFT(CYANFS_RB_PARENT_GET(elm, field), field))                          \
				CYANFS_RB_LEFT(CYANFS_RB_PARENT_GET(elm, field), field) = (tmp);                       \
			else                                                                                           \
				CYANFS_RB_RIGHT(CYANFS_RB_PARENT_GET(elm, field), field) = (tmp);                      \
		} else                                                                                                 \
			(head)->rbh_root = (tmp);                                                                      \
		CYANFS_RB_RIGHT(tmp, field) = (elm);                                                                   \
		CYANFS_RB_PARENT_SET(elm, field, (tmp));                                                               \
		CYANFS_RB_AUGMENT(tmp);                                                                                \
		if ((CYANFS_RB_PARENT_GET(tmp, field)))                                                                \
			CYANFS_RB_AUGMENT(CYANFS_RB_PARENT_GET(tmp, field));                                           \
	} while (0)

/* Generates prototypes and inline functions */
#define CYANFS_RB_PROTOTYPE(name, type, field, cmp) CYANFS_RB_PROTOTYPE_INTERNAL(name, type, field, cmp, )
#define CYANFS_RB_PROTOTYPE_STATIC(name, type, field, cmp)                                                             \
	CYANFS_RB_PROTOTYPE_INTERNAL(name, type, field, cmp, __unused static)
#define CYANFS_RB_PROTOTYPE_INTERNAL(name, type, field, cmp, attr)                                                     \
	CYANFS_RB_PROTOTYPE_INSERT_COLOR(name, type, attr);                                                            \
	CYANFS_RB_PROTOTYPE_REMOVE_COLOR(name, type, attr);                                                            \
	CYANFS_RB_PROTOTYPE_INSERT(name, type, attr);                                                                  \
	CYANFS_RB_PROTOTYPE_REMOVE(name, type, attr);                                                                  \
	CYANFS_RB_PROTOTYPE_FIND(name, type, attr);                                                                    \
	CYANFS_RB_PROTOTYPE_NFIND(name, type, attr);                                                                   \
	CYANFS_RB_PROTOTYPE_NEXT(name, type, attr);                                                                    \
	CYANFS_RB_PROTOTYPE_PREV(name, type, attr);                                                                    \
	CYANFS_RB_PROTOTYPE_MINMAX(name, type, attr);
#define CYANFS_RB_PROTOTYPE_INSERT_COLOR(name, type, attr)                                                             \
	attr void name##_CYANFS_RB_INSERT_COLOR(struct name *, struct type *)
#define CYANFS_RB_PROTOTYPE_REMOVE_COLOR(name, type, attr)                                                             \
	attr void name##_CYANFS_RB_REMOVE_COLOR(struct name *, struct type *, struct type *)
#define CYANFS_RB_PROTOTYPE_REMOVE(name, type, attr)                                                                   \
	attr struct type *name##_CYANFS_RB_REMOVE(struct name *, struct type *)
#define CYANFS_RB_PROTOTYPE_INSERT(name, type, attr)                                                                   \
	attr struct type *name##_CYANFS_RB_INSERT(struct name *, struct type *)
#define CYANFS_RB_PROTOTYPE_FIND(name, type, attr) attr struct type *name##_CYANFS_RB_FIND(struct name *, struct type *)
#define CYANFS_RB_PROTOTYPE_NFIND(name, type, attr)                                                                    \
	attr struct type *name##_CYANFS_RB_NFIND(struct name *, struct type *)
#define CYANFS_RB_PROTOTYPE_NEXT(name, type, attr) attr struct type *name##_CYANFS_RB_NEXT(struct type *)
#define CYANFS_RB_PROTOTYPE_PREV(name, type, attr) attr struct type *name##_CYANFS_RB_PREV(struct type *)
#define CYANFS_RB_PROTOTYPE_MINMAX(name, type, attr) attr struct type *name##_CYANFS_RB_MINMAX(struct name *, int)

/* Main rb operation.
 * Moves node close to the key of elm to top
 */
#define CYANFS_RB_GENERATE(name, type, field, cmp) CYANFS_RB_GENERATE_INTERNAL(name, type, field, cmp, )
#define CYANFS_RB_GENERATE_STATIC(name, type, field, cmp)                                                              \
	CYANFS_RB_GENERATE_INTERNAL(name, type, field, cmp, __unused static)
#define CYANFS_RB_GENERATE_INTERNAL(name, type, field, cmp, attr)                                                      \
	CYANFS_RB_GENERATE_INSERT_COLOR(name, type, field, attr)                                                       \
	CYANFS_RB_GENERATE_REMOVE_COLOR(name, type, field, attr)                                                       \
	CYANFS_RB_GENERATE_INSERT(name, type, field, cmp, attr)                                                        \
	CYANFS_RB_GENERATE_REMOVE(name, type, field, attr)                                                             \
	CYANFS_RB_GENERATE_FIND(name, type, field, cmp, attr)                                                          \
	CYANFS_RB_GENERATE_NFIND(name, type, field, cmp, attr)                                                         \
	CYANFS_RB_GENERATE_NEXT(name, type, field, attr)                                                               \
	CYANFS_RB_GENERATE_PREV(name, type, field, attr)                                                               \
	CYANFS_RB_GENERATE_MINMAX(name, type, field, attr)

#define CYANFS_RB_GENERATE_INSERT_COLOR(name, type, field, attr)                                                       \
	attr void name##_CYANFS_RB_INSERT_COLOR(struct name *head, struct type *elm)                                   \
	{                                                                                                              \
		struct type *parent, *gparent, *tmp;                                                                   \
		while ((parent = CYANFS_RB_PARENT_GET(elm, field)) != NULL &&                                          \
		       CYANFS_RB_COLOR_GET(parent, field) == CYANFS_RB_RED) {                                          \
			gparent = CYANFS_RB_PARENT_GET(parent, field);                                                 \
			if (parent == CYANFS_RB_LEFT(gparent, field)) {                                                \
				tmp = CYANFS_RB_RIGHT(gparent, field);                                                 \
				if (tmp && CYANFS_RB_COLOR_GET(tmp, field) == CYANFS_RB_RED) {                         \
					CYANFS_RB_COLOR_SET(tmp, field, CYANFS_RB_BLACK);                              \
					CYANFS_RB_SET_BLACKRED(parent, gparent, field);                                \
					elm = gparent;                                                                 \
					continue;                                                                      \
				}                                                                                      \
				if (CYANFS_RB_RIGHT(parent, field) == elm) {                                           \
					CYANFS_RB_ROTATE_LEFT(head, parent, tmp, field);                               \
					tmp = parent;                                                                  \
					parent = elm;                                                                  \
					elm = tmp;                                                                     \
				}                                                                                      \
				CYANFS_RB_SET_BLACKRED(parent, gparent, field);                                        \
				CYANFS_RB_ROTATE_RIGHT(head, gparent, tmp, field);                                     \
			} else {                                                                                       \
				tmp = CYANFS_RB_LEFT(gparent, field);                                                  \
				if (tmp && CYANFS_RB_COLOR_GET(tmp, field) == CYANFS_RB_RED) {                         \
					CYANFS_RB_COLOR_SET(tmp, field, CYANFS_RB_BLACK);                              \
					CYANFS_RB_SET_BLACKRED(parent, gparent, field);                                \
					elm = gparent;                                                                 \
					continue;                                                                      \
				}                                                                                      \
				if (CYANFS_RB_LEFT(parent, field) == elm) {                                            \
					CYANFS_RB_ROTATE_RIGHT(head, parent, tmp, field);                              \
					tmp = parent;                                                                  \
					parent = elm;                                                                  \
					elm = tmp;                                                                     \
				}                                                                                      \
				CYANFS_RB_SET_BLACKRED(parent, gparent, field);                                        \
				CYANFS_RB_ROTATE_LEFT(head, gparent, tmp, field);                                      \
			}                                                                                              \
		}                                                                                                      \
		CYANFS_RB_COLOR_SET(head->rbh_root, field, CYANFS_RB_BLACK);                                           \
	}

#define CYANFS_RB_GENERATE_REMOVE_COLOR(name, type, field, attr)                                                       \
	attr void name##_CYANFS_RB_REMOVE_COLOR(struct name *head, struct type *parent, struct type *elm)              \
	{                                                                                                              \
		struct type *tmp;                                                                                      \
		while ((elm == NULL || CYANFS_RB_COLOR_GET(elm, field) == CYANFS_RB_BLACK) &&                          \
		       elm != CYANFS_RB_ROOT(head)) {                                                                  \
			if (CYANFS_RB_LEFT(parent, field) == elm) {                                                    \
				tmp = CYANFS_RB_RIGHT(parent, field);                                                  \
				if (CYANFS_RB_COLOR_GET(tmp, field) == CYANFS_RB_RED) {                                \
					CYANFS_RB_SET_BLACKRED(tmp, parent, field);                                    \
					CYANFS_RB_ROTATE_LEFT(head, parent, tmp, field);                               \
					tmp = CYANFS_RB_RIGHT(parent, field);                                          \
				}                                                                                      \
				if ((CYANFS_RB_LEFT(tmp, field) == NULL ||                                             \
				     CYANFS_RB_COLOR_GET(CYANFS_RB_LEFT(tmp, field), field) == CYANFS_RB_BLACK) &&     \
				    (CYANFS_RB_RIGHT(tmp, field) == NULL ||                                            \
				     CYANFS_RB_COLOR_GET(CYANFS_RB_RIGHT(tmp, field), field) == CYANFS_RB_BLACK)) {    \
					CYANFS_RB_COLOR_SET(tmp, field, CYANFS_RB_RED);                                \
					elm = parent;                                                                  \
					parent = CYANFS_RB_PARENT_GET(elm, field);                                     \
				} else {                                                                               \
					if (CYANFS_RB_RIGHT(tmp, field) == NULL ||                                     \
					    CYANFS_RB_COLOR_GET(CYANFS_RB_RIGHT(tmp, field), field) ==                 \
						    CYANFS_RB_BLACK) {                                                 \
						struct type *oleft;                                                    \
						if ((oleft = CYANFS_RB_LEFT(tmp, field)) != NULL)                      \
							CYANFS_RB_COLOR_SET(oleft, field, CYANFS_RB_BLACK);            \
						CYANFS_RB_COLOR_SET(tmp, field, CYANFS_RB_RED);                        \
						CYANFS_RB_ROTATE_RIGHT(head, tmp, oleft, field);                       \
						tmp = CYANFS_RB_RIGHT(parent, field);                                  \
					}                                                                              \
					CYANFS_RB_COLOR_SET(tmp, field, CYANFS_RB_COLOR_GET(parent, field));           \
					CYANFS_RB_COLOR_SET(parent, field, CYANFS_RB_BLACK);                           \
					if (CYANFS_RB_RIGHT(tmp, field))                                               \
						CYANFS_RB_COLOR_SET(CYANFS_RB_RIGHT(tmp, field), field,                \
								    CYANFS_RB_BLACK);                                  \
					CYANFS_RB_ROTATE_LEFT(head, parent, tmp, field);                               \
					elm = CYANFS_RB_ROOT(head);                                                    \
					break;                                                                         \
				}                                                                                      \
			} else {                                                                                       \
				tmp = CYANFS_RB_LEFT(parent, field);                                                   \
				if (CYANFS_RB_COLOR_GET(tmp, field) == CYANFS_RB_RED) {                                \
					CYANFS_RB_SET_BLACKRED(tmp, parent, field);                                    \
					CYANFS_RB_ROTATE_RIGHT(head, parent, tmp, field);                              \
					tmp = CYANFS_RB_LEFT(parent, field);                                           \
				}                                                                                      \
				if ((CYANFS_RB_LEFT(tmp, field) == NULL ||                                             \
				     CYANFS_RB_COLOR_GET(CYANFS_RB_LEFT(tmp, field), field) == CYANFS_RB_BLACK) &&     \
				    (CYANFS_RB_RIGHT(tmp, field) == NULL ||                                            \
				     CYANFS_RB_COLOR_GET(CYANFS_RB_RIGHT(tmp, field), field) == CYANFS_RB_BLACK)) {    \
					CYANFS_RB_COLOR_SET(tmp, field, CYANFS_RB_RED);                                \
					elm = parent;                                                                  \
					parent = CYANFS_RB_PARENT_GET(elm, field);                                     \
				} else {                                                                               \
					if (CYANFS_RB_LEFT(tmp, field) == NULL ||                                      \
					    CYANFS_RB_COLOR_GET(CYANFS_RB_LEFT(tmp, field), field) ==                  \
						    CYANFS_RB_BLACK) {                                                 \
						struct type *oright;                                                   \
						if ((oright = CYANFS_RB_RIGHT(tmp, field)) != NULL)                    \
							CYANFS_RB_COLOR_SET(oright, field, CYANFS_RB_BLACK);           \
						CYANFS_RB_COLOR_SET(tmp, field, CYANFS_RB_RED);                        \
						CYANFS_RB_ROTATE_LEFT(head, tmp, oright, field);                       \
						tmp = CYANFS_RB_LEFT(parent, field);                                   \
					}                                                                              \
					CYANFS_RB_COLOR_SET(tmp, field, CYANFS_RB_COLOR_GET(parent, field));           \
					CYANFS_RB_COLOR_SET(parent, field, CYANFS_RB_BLACK);                           \
					if (CYANFS_RB_LEFT(tmp, field))                                                \
						CYANFS_RB_COLOR_SET(CYANFS_RB_LEFT(tmp, field), field,                 \
								    CYANFS_RB_BLACK);                                  \
					CYANFS_RB_ROTATE_RIGHT(head, parent, tmp, field);                              \
					elm = CYANFS_RB_ROOT(head);                                                    \
					break;                                                                         \
				}                                                                                      \
			}                                                                                              \
		}                                                                                                      \
		if (elm)                                                                                               \
			CYANFS_RB_COLOR_SET(elm, field, CYANFS_RB_BLACK);                                              \
	}

#define CYANFS_RB_GENERATE_REMOVE(name, type, field, attr)                                                             \
	attr struct type *name##_CYANFS_RB_REMOVE(struct name *head, struct type *elm)                                 \
	{                                                                                                              \
		struct type *child, *parent, *old = elm;                                                               \
		int color;                                                                                             \
		if (CYANFS_RB_LEFT(elm, field) == NULL)                                                                \
			child = CYANFS_RB_RIGHT(elm, field);                                                           \
		else if (CYANFS_RB_RIGHT(elm, field) == NULL)                                                          \
			child = CYANFS_RB_LEFT(elm, field);                                                            \
		else {                                                                                                 \
			struct type *left;                                                                             \
			elm = CYANFS_RB_RIGHT(elm, field);                                                             \
			while ((left = CYANFS_RB_LEFT(elm, field)) != NULL)                                            \
				elm = left;                                                                            \
			child = CYANFS_RB_RIGHT(elm, field);                                                           \
			parent = CYANFS_RB_PARENT_GET(elm, field);                                                     \
			color = CYANFS_RB_COLOR_GET(elm, field);                                                       \
			if (child)                                                                                     \
				CYANFS_RB_PARENT_SET(child, field, parent);                                            \
			if (parent) {                                                                                  \
				if (CYANFS_RB_LEFT(parent, field) == elm)                                              \
					CYANFS_RB_LEFT(parent, field) = child;                                         \
				else                                                                                   \
					CYANFS_RB_RIGHT(parent, field) = child;                                        \
				CYANFS_RB_AUGMENT(parent);                                                             \
			} else                                                                                         \
				CYANFS_RB_ROOT(head) = child;                                                          \
			if (CYANFS_RB_PARENT_GET(elm, field) == old)                                                   \
				parent = elm;                                                                          \
			(elm)->field = (old)->field;                                                                   \
			if (CYANFS_RB_PARENT_GET(old, field)) {                                                        \
				if (CYANFS_RB_LEFT(CYANFS_RB_PARENT_GET(old, field), field) == old)                    \
					CYANFS_RB_LEFT(CYANFS_RB_PARENT_GET(old, field), field) = elm;                 \
				else                                                                                   \
					CYANFS_RB_RIGHT(CYANFS_RB_PARENT_GET(old, field), field) = elm;                \
				CYANFS_RB_AUGMENT(CYANFS_RB_PARENT_GET(old, field));                                   \
			} else                                                                                         \
				CYANFS_RB_ROOT(head) = elm;                                                            \
			CYANFS_RB_PARENT_SET(CYANFS_RB_LEFT(old, field), field, elm);                                  \
			if (CYANFS_RB_RIGHT(old, field))                                                               \
				CYANFS_RB_PARENT_SET(CYANFS_RB_RIGHT(old, field), field, elm);                         \
			if (parent) {                                                                                  \
				left = parent;                                                                         \
				do {                                                                                   \
					CYANFS_RB_AUGMENT(left);                                                       \
				} while ((left = CYANFS_RB_PARENT_GET(left, field)) != NULL);                          \
			}                                                                                              \
			goto color;                                                                                    \
		}                                                                                                      \
		parent = CYANFS_RB_PARENT_GET(elm, field);                                                             \
		color = CYANFS_RB_COLOR_GET(elm, field);                                                               \
		if (child)                                                                                             \
			CYANFS_RB_PARENT_SET(child, field, parent);                                                    \
		if (parent) {                                                                                          \
			if (CYANFS_RB_LEFT(parent, field) == elm)                                                      \
				CYANFS_RB_LEFT(parent, field) = child;                                                 \
			else                                                                                           \
				CYANFS_RB_RIGHT(parent, field) = child;                                                \
			CYANFS_RB_AUGMENT(parent);                                                                     \
		} else                                                                                                 \
			CYANFS_RB_ROOT(head) = child;                                                                  \
	color:                                                                                                         \
		if (color == CYANFS_RB_BLACK)                                                                          \
			name##_CYANFS_RB_REMOVE_COLOR(head, parent, child);                                            \
		return (old);                                                                                          \
	}

#define CYANFS_RB_GENERATE_INSERT(name, type, field, cmp, attr)                                                        \
	/* Inserts a node into the RB tree */                                                                          \
	attr struct type *name##_CYANFS_RB_INSERT(struct name *head, struct type *elm)                                 \
	{                                                                                                              \
		struct type *tmp;                                                                                      \
		struct type *parent = NULL;                                                                            \
		int comp = 0;                                                                                          \
		tmp = CYANFS_RB_ROOT(head);                                                                            \
		while (tmp) {                                                                                          \
			parent = tmp;                                                                                  \
			comp = (cmp)(elm, parent);                                                                     \
			if (comp < 0)                                                                                  \
				tmp = CYANFS_RB_LEFT(tmp, field);                                                      \
			else if (comp > 0)                                                                             \
				tmp = CYANFS_RB_RIGHT(tmp, field);                                                     \
			else                                                                                           \
				return (tmp);                                                                          \
		}                                                                                                      \
		CYANFS_RB_SET(elm, parent, field);                                                                     \
		if (parent != NULL) {                                                                                  \
			if (comp < 0)                                                                                  \
				CYANFS_RB_LEFT(parent, field) = elm;                                                   \
			else                                                                                           \
				CYANFS_RB_RIGHT(parent, field) = elm;                                                  \
			CYANFS_RB_AUGMENT(parent);                                                                     \
		} else                                                                                                 \
			CYANFS_RB_ROOT(head) = elm;                                                                    \
		name##_CYANFS_RB_INSERT_COLOR(head, elm);                                                              \
		return (NULL);                                                                                         \
	}

#define CYANFS_RB_GENERATE_FIND(name, type, field, cmp, attr)                                                          \
	/* Finds the node with the same key as elm */                                                                  \
	attr struct type *name##_CYANFS_RB_FIND(struct name *head, struct type *elm)                                   \
	{                                                                                                              \
		struct type *tmp = CYANFS_RB_ROOT(head);                                                               \
		int comp;                                                                                              \
		while (tmp) {                                                                                          \
			comp = cmp(elm, tmp);                                                                          \
			if (comp < 0)                                                                                  \
				tmp = CYANFS_RB_LEFT(tmp, field);                                                      \
			else if (comp > 0)                                                                             \
				tmp = CYANFS_RB_RIGHT(tmp, field);                                                     \
			else                                                                                           \
				return (tmp);                                                                          \
		}                                                                                                      \
		return (NULL);                                                                                         \
	}

#define CYANFS_RB_GENERATE_NFIND(name, type, field, cmp, attr)                                                         \
	/* Finds the first node greater than or equal to the search key */                                             \
	attr struct type *name##_CYANFS_RB_NFIND(struct name *head, struct type *elm)                                  \
	{                                                                                                              \
		struct type *tmp = CYANFS_RB_ROOT(head);                                                               \
		struct type *res = NULL;                                                                               \
		int comp;                                                                                              \
		while (tmp) {                                                                                          \
			comp = cmp(elm, tmp);                                                                          \
			if (comp < 0) {                                                                                \
				res = tmp;                                                                             \
				tmp = CYANFS_RB_LEFT(tmp, field);                                                      \
			} else if (comp > 0)                                                                           \
				tmp = CYANFS_RB_RIGHT(tmp, field);                                                     \
			else                                                                                           \
				return (tmp);                                                                          \
		}                                                                                                      \
		return (res);                                                                                          \
	}

#define CYANFS_RB_GENERATE_NEXT(name, type, field, attr)                                                               \
	/* ARGSUSED */                                                                                                 \
	attr struct type *name##_CYANFS_RB_NEXT(struct type *elm)                                                      \
	{                                                                                                              \
		if (CYANFS_RB_RIGHT(elm, field)) {                                                                     \
			elm = CYANFS_RB_RIGHT(elm, field);                                                             \
			while (CYANFS_RB_LEFT(elm, field))                                                             \
				elm = CYANFS_RB_LEFT(elm, field);                                                      \
		} else {                                                                                               \
			if (CYANFS_RB_PARENT_GET(elm, field) &&                                                        \
			    (elm == CYANFS_RB_LEFT(CYANFS_RB_PARENT_GET(elm, field), field)))                          \
				elm = CYANFS_RB_PARENT_GET(elm, field);                                                \
			else {                                                                                         \
				while (CYANFS_RB_PARENT_GET(elm, field) &&                                             \
				       (elm == CYANFS_RB_RIGHT(CYANFS_RB_PARENT_GET(elm, field), field)))              \
					elm = CYANFS_RB_PARENT_GET(elm, field);                                        \
				elm = CYANFS_RB_PARENT_GET(elm, field);                                                \
			}                                                                                              \
		}                                                                                                      \
		return (elm);                                                                                          \
	}

#define CYANFS_RB_GENERATE_PREV(name, type, field, attr)                                                               \
	/* ARGSUSED */                                                                                                 \
	attr struct type *name##_CYANFS_RB_PREV(struct type *elm)                                                      \
	{                                                                                                              \
		if (CYANFS_RB_LEFT(elm, field)) {                                                                      \
			elm = CYANFS_RB_LEFT(elm, field);                                                              \
			while (CYANFS_RB_RIGHT(elm, field))                                                            \
				elm = CYANFS_RB_RIGHT(elm, field);                                                     \
		} else {                                                                                               \
			if (CYANFS_RB_PARENT_GET(elm, field) &&                                                        \
			    (elm == CYANFS_RB_RIGHT(CYANFS_RB_PARENT_GET(elm, field), field)))                         \
				elm = CYANFS_RB_PARENT_GET(elm, field);                                                \
			else {                                                                                         \
				while (CYANFS_RB_PARENT_GET(elm, field) &&                                             \
				       (elm == CYANFS_RB_LEFT(CYANFS_RB_PARENT_GET(elm, field), field)))               \
					elm = CYANFS_RB_PARENT_GET(elm, field);                                        \
				elm = CYANFS_RB_PARENT_GET(elm, field);                                                \
			}                                                                                              \
		}                                                                                                      \
		return (elm);                                                                                          \
	}

#define CYANFS_RB_GENERATE_MINMAX(name, type, field, attr)                                                             \
	attr struct type *name##_CYANFS_RB_MINMAX(struct name *head, int val)                                          \
	{                                                                                                              \
		struct type *tmp = CYANFS_RB_ROOT(head);                                                               \
		struct type *parent = NULL;                                                                            \
		while (tmp) {                                                                                          \
			parent = tmp;                                                                                  \
			if (val < 0)                                                                                   \
				tmp = CYANFS_RB_LEFT(tmp, field);                                                      \
			else                                                                                           \
				tmp = CYANFS_RB_RIGHT(tmp, field);                                                     \
		}                                                                                                      \
		return (parent);                                                                                       \
	}

#define CYANFS_RB_NEGINF -1
#define CYANFS_RB_INF 1

#define CYANFS_RB_INSERT(name, x, y) name##_CYANFS_RB_INSERT(x, y)
#define CYANFS_RB_REMOVE(name, x, y) name##_CYANFS_RB_REMOVE(x, y)
#define CYANFS_RB_FIND(name, x, y) name##_CYANFS_RB_FIND(x, y)
#define CYANFS_RB_NFIND(name, x, y) name##_CYANFS_RB_NFIND(x, y)
#define CYANFS_RB_NEXT(name, x, y) name##_CYANFS_RB_NEXT(y)
#define CYANFS_RB_PREV(name, x, y) name##_CYANFS_RB_PREV(y)
#define CYANFS_RB_MIN(name, x) name##_CYANFS_RB_MINMAX(x, CYANFS_RB_NEGINF)
#define CYANFS_RB_MAX(name, x) name##_CYANFS_RB_MINMAX(x, CYANFS_RB_INF)

#define CYANFS_RB_FOREACH(x, name, head)                                                                               \
	for ((x) = CYANFS_RB_MIN(name, head); (x) != NULL; (x) = name##_CYANFS_RB_NEXT(x))

#define CYANFS_RB_FOREACH_FROM(x, name, y)                                                                             \
	for ((x) = (y); ((x) != NULL) && ((y) = name##_CYANFS_RB_NEXT(x), (x) != NULL); (x) = (y))

#define CYANFS_RB_FOREACH_SAFE(x, name, head, y)                                                                       \
	for ((x) = CYANFS_RB_MIN(name, head); ((x) != NULL) && ((y) = name##_CYANFS_RB_NEXT(x), (x) != NULL); (x) = (y))

#define CYANFS_RB_FOREACH_REVERSE(x, name, head)                                                                       \
	for ((x) = CYANFS_RB_MAX(name, head); (x) != NULL; (x) = name##_CYANFS_RB_PREV(x))

#define CYANFS_RB_FOREACH_REVERSE_FROM(x, name, y)                                                                     \
	for ((x) = (y); ((x) != NULL) && ((y) = name##_CYANFS_RB_PREV(x), (x) != NULL); (x) = (y))

#define CYANFS_RB_FOREACH_REVERSE_SAFE(x, name, head, y)                                                               \
	for ((x) = CYANFS_RB_MAX(name, head); ((x) != NULL) && ((y) = name##_CYANFS_RB_PREV(x), (x) != NULL); (x) = (y))

#endif
