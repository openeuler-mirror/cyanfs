#include <assert.h>
#include "core/internal.h"

static void init_file(struct cyanfs_file *file, struct cyanfs_super *s, uint64_t size,
		      struct cyanfs_file *parent)
{
	file->super = s;
	file->parent = parent;
	file->open_type = CYANFS_FILE_READONLY;
	file->meta.size = size;
	CYANFS_RB_INIT(&file->extents);
}

static void init_extent(struct cyanfs_extent_node *extent, cyanfs_extent_id file)
{
	extent->v.map = 1;
	extent->v.file = file;
}

int main(void)
{
	struct cyanfs_super s = { 0 };
	struct cyanfs_file grandparent = { 0 }, parent = { 0 }, child = { 0 };
	struct cyanfs_extent_node grandparent_two = { 0 };
	struct cyanfs_extent_node parent_zero = { 0 }, parent_two = { 0 };
	struct cyanfs_extent_node child_one = { 0 };
	uint64_t off;

	assert(!cyanfs_init_rwlock(&s.files_lock));
	s.flags = CYANFS_SUPER_FLAG_READY;
	init_file(&grandparent, &s, 3 * CYANFS_EXTENT_SIZE, NULL);
	init_file(&parent, &s, 3 * CYANFS_EXTENT_SIZE, &grandparent);
	init_file(&child, &s, 3 * CYANFS_EXTENT_SIZE, &parent);
	init_extent(&parent_zero, 0);
	init_extent(&parent_two, 2);
	init_extent(&child_one, 1);
	assert(!CYANFS_RB_INSERT(cyanfs_file_extents_rb, &parent.extents, &parent_zero));
	assert(!CYANFS_RB_INSERT(cyanfs_file_extents_rb, &parent.extents, &parent_two));
	assert(!CYANFS_RB_INSERT(cyanfs_file_extents_rb, &child.extents, &child_one));

	off = 0;
	assert(!cyanfs_seek_extent(&child, &off));
	assert(off == CYANFS_EXTENT_SIZE);
	off = 0;
	assert(!cyanfs_seek_data(&child, &off));
	assert(off == 0);
	off = CYANFS_FILE_ALIGN_SIZE;
	assert(!cyanfs_seek_data(&child, &off));
	assert(off == CYANFS_FILE_ALIGN_SIZE);
	off = CYANFS_EXTENT_SIZE;
	assert(!cyanfs_seek_data(&child, &off));
	assert(off == CYANFS_EXTENT_SIZE);
	off = 2 * CYANFS_EXTENT_SIZE;
	assert(!cyanfs_seek_data(&child, &off));
	assert(off == 2 * CYANFS_EXTENT_SIZE);

	parent_zero.v.error = 1;
	off = 0;
	assert(cyanfs_seek_data(&child, &off) == -CYANFS_ERR_IO);
	parent_zero.v.error = 0;
	child_one.v.error = 1;
	off = CYANFS_EXTENT_SIZE;
	assert(cyanfs_seek_data(&child, &off) == -CYANFS_ERR_IO);
	child_one.v.error = 0;

	s.flags |= CYANFS_SUPER_FLAG_ERROR;
	off = 0;
	assert(cyanfs_seek_data(&child, &off) == -CYANFS_ERR_IO);
	s.flags = CYANFS_SUPER_FLAG_READY;

	CYANFS_RB_REMOVE(cyanfs_file_extents_rb, &parent.extents, &parent_two);
	parent.meta.size = CYANFS_EXTENT_SIZE;
	init_extent(&grandparent_two, 2);
	assert(!CYANFS_RB_INSERT(cyanfs_file_extents_rb, &grandparent.extents, &grandparent_two));
	off = 2 * CYANFS_EXTENT_SIZE;
	assert(cyanfs_seek_data(&child, &off) == -CYANFS_ERR_NOENT);

	child.open_type = CYANFS_FILE_READWRITE;
	off = 0;
	assert(cyanfs_seek_data(&child, &off) == -CYANFS_ERR_INVAL);
	child.open_type = CYANFS_FILE_READONLY;
	off = child.meta.size;
	assert(cyanfs_seek_data(&child, &off) == -CYANFS_ERR_NOENT);

	cyanfs_destroy_rwlock(&s.files_lock);
	puts("seek-data: PASS");
	return 0;
}
