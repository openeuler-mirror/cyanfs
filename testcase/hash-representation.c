#include <assert.h>
#include <stddef.h>

#include "core/internal.h"

struct test_container {
	uint8_t prefix[3];
	struct cyanfs_list_head node;
};

int main(void)
{
	static const uint8_t expected_input[16] = {
		0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
		0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
	};
	struct test_container container;
	uint32_t expected_hash;

	_Static_assert(sizeof(cyanfs_uintptr_t) == sizeof(void *), "pointer integer type mismatch");
	_Static_assert(cyanfs_offsetof(struct test_container, node) == offsetof(struct test_container, node),
		       "container offset mismatch");

	expected_hash = cyanfs_crc32(~0, (void *)expected_input, sizeof(expected_input));
	assert(cyanfs_extent_alloc_hash(0x0102030405060708ULL, 0x1112131415161718ULL) == expected_hash);
	assert(cyanfs_container_of(&container.node, struct test_container, node) == &container);
	return 0;
}
