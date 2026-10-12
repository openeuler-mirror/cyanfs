#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "core/base.h"

struct test_node {
	int key;
	CYANFS_RB_ENTRY(test_node) entry;
};

CYANFS_RB_HEAD(test_tree, test_node);

static int test_node_compare(struct test_node *a, struct test_node *b)
{
	if (a->key > b->key)
		return 1;
	if (a->key < b->key)
		return -1;
	return 0;
}

static unsigned int color_read_count;

static uint64_t test_color_get(struct test_node *node)
{
	++color_read_count;
	return ((cyanfs_uintptr_t)node->entry.rbe_parent) & 1;
}

#undef CYANFS_RB_COLOR_GET
#define CYANFS_RB_COLOR_GET(elm, field) test_color_get(elm)

CYANFS_RB_GENERATE_INTERNAL(test_tree, test_node, entry, test_node_compare, static inline)

int main(void)
{
	static const int keys[] = { 8, 4, 12, 2, 6, 10, 14, 1, 3, 5, 7, 9, 11, 13, 15 };
	struct test_node *nodes[sizeof(keys) / sizeof(keys[0])];
	struct test_node *node;
	struct test_node query;
	struct test_tree tree;
	size_t i;
	int expected;

	CYANFS_RB_INIT(&tree);
	for (i = 0; i < sizeof(nodes) / sizeof(nodes[0]); ++i) {
		nodes[i] = malloc(sizeof(*nodes[i]));
		assert(nodes[i]);
#ifdef CYANFS_TEST_POISON_LINKS
		memset(&nodes[i]->entry, 0xa5, sizeof(nodes[i]->entry));
#endif
		nodes[i]->key = keys[i];
		assert(!CYANFS_RB_INSERT(test_tree, &tree, nodes[i]));
		if (!i)
			assert(!color_read_count);
	}

	expected = 1;
	CYANFS_RB_FOREACH(node, test_tree, &tree)
	{
		assert(node->key == expected++);
	}
	assert(expected == 16);

	expected = 15;
	CYANFS_RB_FOREACH_REVERSE(node, test_tree, &tree)
	{
		assert(node->key == expected--);
	}
	assert(expected == 0);

	query.key = 0;
	assert(CYANFS_RB_NFIND(test_tree, &tree, &query)->key == 1);
	query.key = 16;
	assert(!CYANFS_RB_NFIND(test_tree, &tree, &query));

	for (i = 0; i < sizeof(nodes) / sizeof(nodes[0]); ++i) {
		query.key = keys[i];
		assert(CYANFS_RB_FIND(test_tree, &tree, &query) == nodes[i]);
	}

	for (i = 0; i < sizeof(nodes) / sizeof(nodes[0]); ++i) {
		assert(CYANFS_RB_REMOVE(test_tree, &tree, nodes[i]) == nodes[i]);
		free(nodes[i]);
	}
	assert(CYANFS_RB_EMPTY(&tree));
	return 0;
}
