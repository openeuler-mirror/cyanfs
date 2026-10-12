#include <assert.h>
#include "core/internal.h"

static cyanfs_file_name_t make_name(unsigned char value)
{
	cyanfs_file_name_t name = { 0 };

	name.data[0] = value;
	return name;
}

static struct cyanfs_file *find_id(struct cyanfs_super *s, cyanfs_file_id_t id)
{
	struct cyanfs_file file = { 0 };

	file.meta.id = id;
	return CYANFS_RB_FIND(cyanfs_files_id_rb, &s->files_by_id, &file);
}

static struct cyanfs_file *find_name(struct cyanfs_super *s, cyanfs_file_name_t name)
{
	struct cyanfs_file file = { 0 };

	file.meta.name = name;
	return CYANFS_RB_FIND(cyanfs_files_name_rb, &s->files_by_name, &file);
}

static cyanfs_status replay_create(struct cyanfs_super *s, cyanfs_file_id_t id, cyanfs_file_name_t *name)
{
	struct cyanfs_journal_entry journal = { 0 };

	journal.type = CYANFS_JOURNAL_CREATE;
	journal.create.id = id;
	journal.create.name = name;
	return __cyanfs_super_journal_replay(s, &journal);
}

static cyanfs_status replay_fork(struct cyanfs_super *s, cyanfs_file_id_t id, cyanfs_file_id_t parent_id,
				 cyanfs_file_name_t *name)
{
	struct cyanfs_journal_entry journal = { 0 };

	journal.type = CYANFS_JOURNAL_FORK;
	journal.fork.id = id;
	journal.fork.pid = parent_id;
	journal.fork.name = name;
	return __cyanfs_super_journal_replay(s, &journal);
}

static cyanfs_status replay_truncate(struct cyanfs_super *s, cyanfs_file_id_t id, uint64_t size)
{
	struct cyanfs_journal_entry journal = { 0 };

	journal.type = CYANFS_JOURNAL_TRUNCATE;
	journal.truncate.id = id;
	journal.truncate.size = size;
	return __cyanfs_super_journal_replay(s, &journal);
}

static cyanfs_status replay_delete(struct cyanfs_super *s, cyanfs_file_id_t id)
{
	struct cyanfs_journal_entry journal = { 0 };

	journal.type = CYANFS_JOURNAL_DELETE;
	journal.delete.id = id;
	return __cyanfs_super_journal_replay(s, &journal);
}

static cyanfs_status replay_bind(struct cyanfs_super *s, cyanfs_file_id_t id, cyanfs_extent_id file,
			     cyanfs_extent_id backend)
{
	struct cyanfs_journal_entry journal = { 0 };

	journal.type = CYANFS_JOURNAL_BIND;
	journal.bind.id = id;
	journal.bind.file = file;
	journal.bind.backend = backend;
	return __cyanfs_super_journal_replay(s, &journal);
}

static void cleanup(struct cyanfs_super *s)
{
	struct cyanfs_file *file;

	while ((file = CYANFS_RB_MIN(cyanfs_files_id_rb, &s->files_by_id))) {
		CYANFS_RB_REMOVE(cyanfs_files_id_rb, &s->files_by_id, file);
		CYANFS_RB_REMOVE(cyanfs_files_name_rb, &s->files_by_name, file);
		cyanfs_free(file);
	}
}

int main(void)
{
	struct cyanfs_super s = { 0 };
	struct cyanfs_extent_node extents[2] = { 0 };
	struct cyanfs_file *parent, *child;
	cyanfs_file_name_t parent_name = make_name('p');
	cyanfs_file_name_t child_name = make_name('c');
	cyanfs_file_name_t other_name = make_name('o');
	int i;

	CYANFS_RB_INIT(&s.files_by_id);
	CYANFS_RB_INIT(&s.files_by_name);
	CYANFS_RB_INIT(&s.free_extents);
	CYANFS_RB_INIT(&s.unused_extents);
	for (i = 0; i < 2; ++i) {
		extents[i].v.backend = i;
		assert(!CYANFS_RB_INSERT(cyanfs_backend_extents_rb, &s.free_extents, &extents[i]));
	}
	s.meta.total_extents = s.meta.free_extents = 2;

	assert(replay_create(&s, 0, &parent_name) == -CYANFS_ERR_INVAL);
	assert(!s.meta.files && !s.max_file_id);

	assert(!replay_create(&s, 10, &parent_name));
	parent = find_id(&s, 10);
	assert(parent && find_name(&s, parent_name) == parent);
	assert(s.meta.files == 1 && s.max_file_id == 10 && !parent->meta.children);

	assert(replay_create(&s, 10, &other_name) == -CYANFS_ERR_INVAL);
	assert(s.meta.files == 1 && s.max_file_id == 10);
	assert(find_id(&s, 10) == parent && !find_name(&s, other_name));
	assert(!replay_truncate(&s, 10, 2 * CYANFS_EXTENT_SIZE));
	assert(parent->meta.size == 2 * CYANFS_EXTENT_SIZE);

	assert(replay_fork(&s, 0, 10, &child_name) == -CYANFS_ERR_INVAL);
	assert(replay_fork(&s, 9, 10, &child_name) == -CYANFS_ERR_INVAL);
	assert(replay_fork(&s, 11, 9, &child_name) == -CYANFS_ERR_INVAL);
	assert(s.meta.files == 1 && s.max_file_id == 10 && !parent->meta.children);
	assert(!find_name(&s, child_name));

	assert(!replay_fork(&s, 11, 10, &child_name));
	child = find_id(&s, 11);
	assert(child && find_name(&s, child_name) == child);
	assert(child->parent == parent && child->meta.parent_id == 10);
	assert(s.meta.files == 2 && s.max_file_id == 11 && parent->meta.children == 1);

	assert(!replay_bind(&s, 11, 0, 0));
	assert(child->meta.extents == 1 && s.meta.data_extents == 1 && s.meta.free_extents == 1);
	assert(__cyanfs_file_find_extent(child, 0) == &extents[0]);
	assert(__cyanfs_super_find_extent(&s, 0) == NULL);
	assert(__cyanfs_super_find_extent(&s, 1) == &extents[1]);
	assert(replay_bind(&s, 11, 0, 1) == -CYANFS_ERR_INVAL);
	assert(child->meta.extents == 1 && s.meta.data_extents == 1 && s.meta.free_extents == 1);
	assert(__cyanfs_file_find_extent(child, 0) == &extents[0]);
	assert(__cyanfs_super_find_extent(&s, 1) == &extents[1]);

	assert(replay_fork(&s, 11, 10, &other_name) == -CYANFS_ERR_INVAL);
	assert(replay_fork(&s, 12, 10, &child_name) == -CYANFS_ERR_INVAL);
	assert(s.meta.files == 2 && s.max_file_id == 11 && parent->meta.children == 1);
	assert(find_id(&s, 11) == child && !find_id(&s, 12));
	assert(!find_name(&s, other_name));

	assert(replay_truncate(&s, 10, CYANFS_FILE_ALIGN_SIZE) == -CYANFS_ERR_INVAL);
	assert(replay_delete(&s, 10) == -CYANFS_ERR_INVAL);
	assert(s.meta.files == 2 && parent->meta.size == 2 * CYANFS_EXTENT_SIZE && parent->meta.children == 1);
	assert(child->parent == parent && find_id(&s, 10) == parent && find_id(&s, 11) == child);

	assert(!replay_delete(&s, 11));
	assert(s.meta.files == 1 && !parent->meta.children && !find_id(&s, 11));
	assert(s.meta.data_extents == 0 && s.meta.free_extents == 2);
	assert(__cyanfs_super_find_extent(&s, 0) == &extents[0]);
	assert(__cyanfs_super_find_extent(&s, 1) == &extents[1]);
	assert(!find_name(&s, child_name));
	assert(!replay_truncate(&s, 10, CYANFS_FILE_ALIGN_SIZE));
	assert(parent->meta.size == CYANFS_FILE_ALIGN_SIZE);
	assert(!replay_delete(&s, 10));
	assert(!s.meta.files && !find_id(&s, 10) && !find_name(&s, parent_name));

	cleanup(&s);
	return 0;
}
