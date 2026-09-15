#include <assert.h>
#include "core/internal.h"

int main(void)
{
	struct cyanfs_extent extent = { 0 };
	unsigned int bits;

	_Static_assert(sizeof(extent) == 8, "extent layout changed");
	extent.backend = CYANFS_EXTENT_INDEX_MAX;
	extent.file = CYANFS_EXTENT_INDEX_MAX;
	extent.map = extent.pending = extent.error = 1;
	assert(extent.map == 1 && extent.pending == 1 && extent.error == 1);
	for (bits = 0; bits < 8; ++bits) {
		extent.map = bits & 1;
		extent.pending = (bits >> 1) & 1;
		extent.error = (bits >> 2) & 1;
		assert(extent.map == (bits & 1));
		assert(extent.pending == ((bits >> 1) & 1));
		assert(extent.error == ((bits >> 2) & 1));
		assert(extent.backend == CYANFS_EXTENT_INDEX_MAX);
		assert(extent.file == CYANFS_EXTENT_INDEX_MAX);
	}
	extent.backend = extent.file = 0;
	assert(extent.map == 1 && extent.pending == 1 && extent.error == 1);
	return 0;
}
