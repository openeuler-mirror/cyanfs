
#include <pthread.h>
#include "utils.h"

struct disk {
	int fd;
	uint64_t size;
	struct cyanfs_super *super;
	pthread_t thread;
	pthread_cond_t cond;
	int new_task;
	int terminated;
	pthread_mutex_t lock;
};

typedef int (*disk_fn)(struct disk *disk, int argc, const char *argv[]);

static void do_loop(struct disk *disk)
{
	struct cyanfs_task *t;

	if (!disk->super)
		return;

	while ((t = cyanfs_super_get_task(disk->super)) != NULL) {
		int err = 0;
		char *buf;

		switch (t->type) {
		case CYANFS_TASK_NOP:
			break;
		case CYANFS_TASK_READ:
			err = safe_pread(disk->fd, t->read.buf, t->read.b_off, t->read.len);
			break;
		case CYANFS_TASK_WRITE:
			err = safe_pwrite(disk->fd, t->read.buf, t->read.b_off, t->read.len);
			break;
		case CYANFS_TASK_COPY:
			buf = malloc(t->copy.len);
			if (!buf) {
				err = -ENOMEM;
			} else {
				err = safe_pread(disk->fd, buf, t->copy.b_src_off, t->copy.len);
				if (!err) {
					err = safe_pwrite(disk->fd, buf, t->copy.b_dst_off, t->copy.len);
				}
				free(buf);
			}
			break;
		case CYANFS_TASK_FLUSH:
			fsync(disk->fd);
			break;
		case CYANFS_TASK_DISCARD:
			break;
		case CYANFS_TASK_REQUEUE:
			t->requeue.fn(t->requeue.ctx);
			break;
		}
		t->done(t, err);
	}
}

static int do_enable_debug(struct disk *disk, int argc, const char *argv[])
{
#ifdef CYANFS_DEBUG_ENABLED
	debug_enable = 1;
#endif
	return 0;
}

static int do_disable_debug(struct disk *disk, int argc, const char *argv[])
{
#ifdef CYANFS_DEBUG_ENABLED
	debug_enable = 0;
#endif
	return 0;
}

static int do_mkfs(struct disk *disk, int argc, const char *argv[])
{
	int fd, r;
	cyanfs_uuid_t uuid;
	char block[CYANFS_SUPER_BLOCK_SIZE];

	fd = open("/dev/urandom", O_RDONLY, 0);
	if (fd < 0) {
		r = -errno;
		fprintf(stderr, "failed to open urandom device\n");
		return r;
	}
	r = safe_read(fd, &uuid, sizeof(uuid));
	if (r < 0) {
		fprintf(stderr, "failed to read urandom device\n");
		return r;
	}
	close(fd);

	cyanfs_super_make(uuid, block);
	r = safe_pwrite(disk->fd, block, 0, CYANFS_SUPER_BLOCK_SIZE);
	if (r < 0) {
		fprintf(stderr, "failed to write super block\n");
		return r;
	}

	return 0;
}

static int do_list(struct disk *disk, int argc, const char *argv[])
{
	struct cyanfs_file_meta iter = cyanfs_list_init_iter;
	printf("    ID Parent Child  Extents         Size Name\n");
	while (cyanfs_list(disk->super, &iter) == 0) {
		printf("%6" PRIu64 " %6" PRIu64 " %5" PRIu32 " %8" PRIu32 " %12" PRIu64 " %s\n", iter.id,
		       iter.parent_id, iter.children, iter.extents, iter.size, (const char *)(&iter.name));
	}
	return 0;
}

static int do_statfs(struct disk *disk, int argc, const char *argv[])
{
	int r;
	struct cyanfs_super_meta meta;
	uint8_t *u = (uint8_t *)&meta.uuid;
	r = cyanfs_statfs(disk->super, &meta);
	if (r < 0) {
		return r;
	}
	printf("    Free     Data  Journal Reserved    Total         Size                                  UUID\n");
	printf("%8" PRIu32 " %8" PRIu32 " %8" PRIu32 " %8" PRIu32 " %8" PRIu32 " %12" PRIu64
	       "  %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
	       meta.free_extents, meta.data_extents, meta.journal_extents, meta.reserved_extents, meta.total_extents, //
	       meta.size, //
	       u[0], u[1], u[2], u[3], //
	       u[4], u[5], //
	       u[6], u[7], //
	       u[8], u[9], //
	       u[10], u[11], u[12], u[13], u[14], u[15]);
	return 0;
}

static int do_compact(struct disk *disk, int argc, const char *argv[])
{
	cyanfs_super_compact(disk->super);
	return 0;
}

static int do_create(struct disk *disk, int argc, const char *argv[])
{
	cyanfs_file_name_t name = { 0 };
	int r;
	if (argc < 1)
		return -EINVAL;
	strcpy((char *)(&name), argv[0]);
	r = cyanfs_create(disk->super, name, NULL);
	if (r < 0)
		return r;
	return 0;
}

static int do_fork(struct disk *disk, int argc, const char *argv[])
{
	cyanfs_file_name_t from = { 0 }, to = { 0 };
	struct cyanfs_file_meta meta;
	int r;
	if (argc < 2)
		return -EINVAL;
	strcpy((char *)(&from), argv[0]);
	strcpy((char *)(&to), argv[1]);
	r = cyanfs_lookup(disk->super, from, &meta);
	if (r < 0)
		return r;
	r = cyanfs_fork(disk->super, meta.id, to, NULL);
	if (r < 0)
		return r;
	return 0;
}

static int do_rename(struct disk *disk, int argc, const char *argv[])
{
	cyanfs_file_name_t from = { 0 }, to = { 0 };
	struct cyanfs_file_meta meta;
	int r;
	if (argc < 2)
		return -EINVAL;
	strcpy((char *)(&from), argv[0]);
	strcpy((char *)(&to), argv[1]);
	r = cyanfs_lookup(disk->super, from, &meta);
	if (r < 0)
		return r;
	r = cyanfs_rename(disk->super, meta.id, to);
	if (r < 0)
		return r;
	return 0;
}

static int do_truncate(struct disk *disk, int argc, const char *argv[])
{
	cyanfs_file_name_t name = { 0 };
	struct cyanfs_file_meta meta;
	uint64_t size;
	int r;
	if (argc < 2)
		return -EINVAL;
	strcpy((char *)(&name), argv[0]);
	r = cyanfs_lookup(disk->super, name, &meta);
	if (r < 0)
		return r;
	if (sscanf(argv[1], "%" PRIu64, &size) != 1)
		return -EINVAL;
	r = cyanfs_truncate(disk->super, meta.id, (size + CYANFS_FILE_ALIGN_MASK) & ~CYANFS_FILE_ALIGN_MASK);
	if (r < 0)
		return r;
	return 0;
}

static int do_delete(struct disk *disk, int argc, const char *argv[])
{
	cyanfs_file_name_t name = { 0 };
	struct cyanfs_file_meta meta;
	int r;
	if (argc < 1)
		return -EINVAL;
	strcpy((char *)(&name), argv[0]);
	r = cyanfs_lookup(disk->super, name, &meta);
	if (r < 0)
		return r;
	r = cyanfs_delete(disk->super, meta.id);
	if (r < 0)
		return r;
	return 0;
}

static int do_extents(struct disk *disk, int argc, const char *argv[])
{
	cyanfs_file_name_t name = { 0 };
	struct cyanfs_file_meta meta;
	uint64_t offset = 0;
	struct cyanfs_file *file;
	int r;
	if (argc < 1)
		return -EINVAL;
	strcpy((char *)(&name), argv[0]);
	r = cyanfs_lookup(disk->super, name, &meta);
	if (r < 0)
		return r;
	r = cyanfs_open(disk->super, meta.id, 0, &file);
	if (r < 0)
		return r;
	while (cyanfs_seek_extent(file, &offset) == 0) {
		printf("extent: %" PRIu64 "\n", offset);
		offset += CYANFS_EXTENT_SIZE;
	}
	cyanfs_close(file);
	return 0;
}

struct requeue_ctx {
	struct disk *disk;
	int done;
	pthread_cond_t cond;
	pthread_mutex_t lock;
};

static void requeue_done(void *c)
{
	struct requeue_ctx *ctx = c;
	pthread_mutex_lock(&ctx->lock);
	ctx->done = 1;
	pthread_cond_signal(&ctx->cond);
	pthread_mutex_unlock(&ctx->lock);
}

const int io_op_read = 0;
const int io_op_write = 1;
const int io_op_discard = 2;

struct ioctx {
	struct disk *disk;
	struct cyanfs_file *file;
	void *buffer;
	uint64_t offset;
	int op;
};

static int do_io(struct disk *disk, struct cyanfs_file *file, int op, uint64_t offset, uint64_t len, void *buffer);

static int do_io_partial(cyanfs_map_type_t type, uint64_t offset, uint64_t len, void *c)
{
	struct ioctx *ctx = c;
	struct disk *disk = ctx->disk;
	struct requeue_ctx requeue = { .disk = disk, .done = 0 };
	int err = -EIO;

	switch (type) {
	case CYANFS_MAP_NOP:
		if (ctx->op == io_op_read)
			memset(ctx->buffer, 0, len);
		err = 0;
		break;
	case CYANFS_MAP_SUBMIT:
		if (ctx->op == io_op_write)
			err = safe_pwrite(disk->fd, ctx->buffer, offset, len);
		else if (ctx->op == io_op_read)
			err = safe_pread(disk->fd, ctx->buffer, offset, len);
		else
			CYANFS_BUG_ON(1);
		break;
	case CYANFS_MAP_REQUEUE:
		pthread_cond_init(&requeue.cond, NULL);
		pthread_mutex_init(&requeue.lock, NULL);
		err = cyanfs_requeue(ctx->file, &requeue, requeue_done);
		pthread_mutex_lock(&requeue.lock);
		if (!requeue.done)
			pthread_cond_wait(&requeue.cond, &requeue.lock);
		pthread_mutex_unlock(&requeue.lock);
		err = do_io(disk, ctx->file, ctx->op, ctx->offset, len, ctx->buffer);
		break;
	default:
		CYANFS_BUG_ON(1);
	}

	if (!err) {
		ctx->buffer = (uint8_t *)ctx->buffer + len;
		ctx->offset += len;
	}

	return err;
}

static int do_io(struct disk *disk, struct cyanfs_file *file, int op, uint64_t offset, uint64_t len, void *buffer)
{
	struct ioctx ctx = {
		.disk = disk,
		.file = file,
		.buffer = buffer,
		.op = op,
		.offset = offset,
	};
	cyanfs_status (*fn)(struct cyanfs_file * f, void *ctx, uint64_t f_off, uint64_t len, cyanfs_io_spliter handler);
	if (!len) {
		return 0;
	}
	if (op == io_op_write)
		fn = cyanfs_write;
	else if (op == io_op_read)
		fn = cyanfs_read;
	else
		fn = cyanfs_discard;
	return fn(file, &ctx, offset, len, do_io_partial);
}

static int do_read(struct disk *disk, int argc, const char *argv[])
{
	cyanfs_file_name_t name = { 0 };
	struct cyanfs_file_meta meta;
	uint64_t offset;
	uint64_t length;
	struct cyanfs_file *file;
	int r;
	if (argc < 2)
		return -EINVAL;
	strcpy((char *)(&name), argv[0]);
	r = cyanfs_lookup(disk->super, name, &meta);
	if (r < 0)
		return r;
	if (sscanf(argv[1], "%" PRIu64, &offset) != 1)
		return -EINVAL;
	if (argc >= 3) {
		if (sscanf(argv[2], "%" PRIu64, &length) != 1)
			return -EINVAL;
	} else {
		length = meta.size - offset;
	}
	r = cyanfs_open(disk->super, meta.id, 0, &file);
	if (r < 0)
		return r;
	r = 0;
	while (offset < meta.size && length) {
		char buffer[CYANFS_EXTENT_SIZE];
		uint64_t count = meta.size - offset;
		if (count > CYANFS_EXTENT_SIZE)
			count = CYANFS_EXTENT_SIZE;
		if (count > length)
			count = length;
		r = do_io(disk, file, io_op_read, offset, count, buffer);
		if (r < 0)
			break;
		offset += count;
		length -= count;
		r = safe_write(1, buffer, count);
		if (r < 0)
			break;
	}
	cyanfs_close(file);
	return r;
}

static int do_write(struct disk *disk, int argc, const char *argv[])
{
	cyanfs_file_name_t name = { 0 };
	struct cyanfs_file_meta meta;
	uint64_t offset;
	struct cyanfs_file *file;
	int r;
	if (argc < 2)
		return -EINVAL;
	strcpy((char *)(&name), argv[0]);
	r = cyanfs_lookup(disk->super, name, &meta);
	if (r < 0)
		return r;
	if (sscanf(argv[1], "%" PRIu64, &offset) != 1)
		return -EINVAL;
	r = cyanfs_open(disk->super, meta.id, 1, &file);
	if (r < 0)
		return r;
	r = 0;
	while (offset < meta.size) {
		char buffer[CYANFS_EXTENT_SIZE];
		uint64_t count;
	again:
		count = meta.size - offset;
		if (count > CYANFS_EXTENT_SIZE)
			count = CYANFS_EXTENT_SIZE;
		r = read(0, buffer, count);
		if (r > 0) {
			count = r;
		} else if (r == 0) {
			break;
		} else {
			if (errno == EINTR)
				goto again;
			r = -errno;
			break;
		}
		r = do_io(disk, file, io_op_write, offset, count, buffer);
		if (r < 0)
			break;
		offset += count;
	}
	cyanfs_close(file);
	return r;
}

static int __do_fill(struct disk *disk, int v, int argc, const char *argv[])
{
	cyanfs_file_name_t name = { 0 };
	struct cyanfs_file_meta meta;
	uint64_t offset, length;
	struct cyanfs_file *file;
	int r;
	char buffer[CYANFS_EXTENT_SIZE];
	memset(buffer, v, CYANFS_EXTENT_SIZE);
	if (argc < 3)
		return -EINVAL;
	strcpy((char *)(&name), argv[0]);
	r = cyanfs_lookup(disk->super, name, &meta);
	if (r < 0)
		return r;
	if (sscanf(argv[1], "%" PRIu64, &offset) != 1)
		return -EINVAL;
	if (sscanf(argv[2], "%" PRIu64, &length) != 1)
		return -EINVAL;
	r = cyanfs_open(disk->super, meta.id, 1, &file);
	if (r < 0)
		return r;
	if (offset + length > meta.size) {
		r = -EINVAL;
		goto close;
	}
	r = 0;
	while (offset < meta.size && length) {
		uint64_t count = meta.size - offset;
		if (count > CYANFS_EXTENT_SIZE)
			count = CYANFS_EXTENT_SIZE;
		if (count > length)
			count = length;
		r = do_io(disk, file, io_op_write, offset, count, buffer);
		if (r < 0)
			break;
		offset += count;
		length -= count;
	}
close:
	cyanfs_close(file);
	return r;
}

static int do_fill(struct disk *disk, int argc, const char *argv[])
{
	return __do_fill(disk, 0xFF, argc, argv);
}

static int do_zero(struct disk *disk, int argc, const char *argv[])
{
	return __do_fill(disk, 0x00, argc, argv);
}

static int __do_check(struct disk *disk, int v, int argc, const char *argv[])
{
	cyanfs_file_name_t name = { 0 };
	struct cyanfs_file_meta meta;
	uint64_t offset, length;
	struct cyanfs_file *file;
	int r;
	char compare[CYANFS_EXTENT_SIZE];
	char buffer[CYANFS_EXTENT_SIZE];
	memset(compare, v, CYANFS_EXTENT_SIZE);
	if (argc < 3)
		return -EINVAL;
	strcpy((char *)(&name), argv[0]);
	r = cyanfs_lookup(disk->super, name, &meta);
	if (r < 0)
		return r;
	if (sscanf(argv[1], "%" PRIu64, &offset) != 1)
		return -EINVAL;
	if (sscanf(argv[2], "%" PRIu64, &length) != 1)
		return -EINVAL;
	r = cyanfs_open(disk->super, meta.id, 0, &file);
	if (r < 0)
		return r;
	if (offset + length > meta.size) {
		r = -EINVAL;
		goto close;
	}
	r = 0;
	while (offset < meta.size && length) {
		uint64_t count = meta.size - offset;
		if (count > CYANFS_EXTENT_SIZE)
			count = CYANFS_EXTENT_SIZE;
		if (count > length)
			count = length;
		r = do_io(disk, file, io_op_read, offset, count, buffer);
		if (r < 0)
			break;
		offset += count;
		length -= count;
		if (memcmp(buffer, compare, count)) {
			r = -ESTALE;
			break;
		}
	}
close:
	cyanfs_close(file);
	return r;
}

static int do_fill_check(struct disk *disk, int argc, const char *argv[])
{
	return __do_check(disk, 0xFF, argc, argv);
}

static int do_zero_check(struct disk *disk, int argc, const char *argv[])
{
	return __do_check(disk, 0x00, argc, argv);
}

static int do_discard(struct disk *disk, int argc, const char *argv[])
{
	cyanfs_file_name_t name = { 0 };
	struct cyanfs_file_meta meta;
	uint64_t offset, length;
	struct cyanfs_file *file;
	int r;
	if (argc < 3)
		return -EINVAL;
	strcpy((char *)(&name), argv[0]);
	r = cyanfs_lookup(disk->super, name, &meta);
	if (r < 0)
		return r;
	if (sscanf(argv[1], "%" PRIu64, &offset) != 1)
		return -EINVAL;
	if (sscanf(argv[2], "%" PRIu64, &length) != 1)
		return -EINVAL;
	r = cyanfs_open(disk->super, meta.id, 1, &file);
	if (r < 0)
		return r;
	if (offset + length > meta.size) {
		r = -EINVAL;
		goto close;
	}
	r = do_io(disk, file, io_op_discard, offset, length, NULL);
close:
	cyanfs_close(file);
	return r;
}

static int do_sleep(struct disk *disk, int argc, const char *argv[])
{
	uint64_t s;
	if (argc < 1)
		return -EINVAL;
	if (sscanf(argv[0], "%" PRIu64, &s) != 1)
		return -EINVAL;
	sleep(s);
	return 0;
}

static void do_new_task(void *ctx)
{
	struct disk *disk = ctx;
	pthread_mutex_lock(&disk->lock);
	disk->new_task = 1;
	CYANFS_DEBUG("wakeup");
	pthread_cond_signal(&disk->cond);
	pthread_mutex_unlock(&disk->lock);
}

static void *thread_handler(void *v)
{
	struct disk *disk = v;
	int terminated = 0;
	for (; !terminated;) {
		do_loop(v);
		pthread_mutex_lock(&disk->lock);
		CYANFS_DEBUG("go to sleep");
		terminated = disk->terminated;
		if (!terminated) {
			if (disk->new_task)
				disk->new_task = 0;
			else
				pthread_cond_wait(&disk->cond, &disk->lock);
		}
		CYANFS_DEBUG("go to work");
		pthread_mutex_unlock(&disk->lock);
	}
	return NULL;
}

static int openfs(struct disk *disk)
{
	int r;
	if (disk->super)
		return 0;
	disk->super = cyanfs_super_open(disk->size, 0, 1);
	if (!disk->super)
		return -ENOMEM;
	do_loop(disk);
	if (!cyanfs_super_is_ready(disk->super))
		return -EIO;
	disk->new_task = 0;
	disk->terminated = 0;
	pthread_mutex_init(&disk->lock, NULL);
	pthread_cond_init(&disk->cond, NULL);
	r = pthread_create(&disk->thread, NULL, thread_handler, disk);
	if (r < 0)
		return r;
	CYANFS_DEBUG("thread start");
	cyanfs_super_set_new_task_callback(disk->super, disk, do_new_task);
	return 0;
}

static void closefs(struct disk *disk)
{
	void *thread_ret;

	if (!disk->super)
		return;
	pthread_mutex_lock(&disk->lock);
	disk->terminated = 1;
	pthread_cond_signal(&disk->cond);
	pthread_mutex_unlock(&disk->lock);
	pthread_join(disk->thread, &thread_ret);
	CYANFS_DEBUG("thread quit");
	cyanfs_super_flush(disk->super, 1);
	do_loop(disk);
	cyanfs_super_close(disk->super);
	disk->super = NULL;
}

static int do_mount(struct disk *disk, int argc, const char *argv[])
{
	int r = openfs(disk);
	return r;
}

static int do_umount(struct disk *disk, int argc, const char *argv[])
{
	closefs(disk);
	return 0;
}

static int do_batch(struct disk *disk, int argc, const char *argv[]);

struct op_map {
	const char *name;
	disk_fn fn;
	int no_mount;
};

static struct op_map op_maps[] = {
	{
		.name = "enable-debug",
		.fn = do_enable_debug,
		.no_mount = 1,
	},
	{
		.name = "disable-debug",
		.fn = do_disable_debug,
		.no_mount = 1,
	},
	{
		.name = "mkfs",
		.fn = do_mkfs,
		.no_mount = 1,
	},
	{
		.name = "list",
		.fn = do_list,
	},
	{
		.name = "statfs",
		.fn = do_statfs,
	},
	{
		.name = "compact",
		.fn = do_compact,
	},
	{
		.name = "create",
		.fn = do_create,
	},
	{
		.name = "fork",
		.fn = do_fork,
	},
	{
		.name = "rename",
		.fn = do_rename,
	},
	{
		.name = "truncate",
		.fn = do_truncate,
	},
	{
		.name = "delete",
		.fn = do_delete,
	},
	{
		.name = "extents",
		.fn = do_extents,
	},
	{
		.name = "read",
		.fn = do_read,
	},
	{
		.name = "write",
		.fn = do_write,
	},
	{
		.name = "fill",
		.fn = do_fill,
	},
	{
		.name = "fill-check",
		.fn = do_fill_check,
	},
	{
		.name = "zero",
		.fn = do_zero,
	},
	{
		.name = "zero-check",
		.fn = do_zero_check,
	},
	{
		.name = "discard",
		.fn = do_discard,
	},
	{
		.name = "mount",
		.fn = do_mount,
		.no_mount = 1,
	},
	{
		.name = "umount",
		.fn = do_umount,
		.no_mount = 1,
	},
	{
		.name = "sleep",
		.fn = do_sleep,
		.no_mount = 1,
	},
	{
		.name = "batch",
		.fn = do_batch,
		.no_mount = 1,
	},
};

static int call_fn(struct disk *disk, int argc, const char *argv[])
{
	int r, i;
	struct op_map *map = NULL;
	for (i = 0; i < sizeof(op_maps) / sizeof(struct op_map); ++i) {
		if (strcmp(argv[0], op_maps[i].name) == 0) {
			map = &op_maps[i];
		}
	}
	if (!map) {
		fprintf(stderr, "op %s is not support\n", argv[0]);
		return -EINVAL;
	}
	if (!map->no_mount) {
		r = openfs(disk);
		if (r < 0) {
			fprintf(stderr, "cannot open device, err=%d\n", -r);
			return r;
		}
	}
	r = map->fn(disk, argc - 1, argv + 1);
	if (r < 0) {
		fprintf(stderr, "op %s failed, err=%d\n", map->name, -r);
	}
	return r;
}

static int is_space(char ch)
{
	switch (ch) {
	case ' ':
	case '\t':
	case '\n':
	case '\r':
		return 1;
	default:
		return 0;
	}
}

static int do_batch(struct disk *disk, int argc, const char *argv[])
{
	FILE *file = stdin;
	int r = 0;

	if (argc < 1)
		return -EINVAL;
	if (strcmp(argv[0], "-")) {
		file = fopen(argv[0], "r");
		if (!file)
			return -errno;
	}

	while (!r) {
		int c = 0;
		const char *v[1024];
		char *line = NULL;
		char *p;
		size_t n;

		if (getline(&line, &n, file) <= 0) {
			if (line)
				free(line);
			r = -errno;
			break;
		}
		p = line;

		while (*p == ' ' || is_space(*p))
			++p;
		if (!*p)
			goto skip;

		do {
			v[c++] = p;
			while (*p && !is_space(*p))
				++p;
			while (is_space(*p))
				*(p++) = 0;
		} while (*p);

		r = call_fn(disk, c, v);

	skip:
		free(line);
		line = NULL;
	}

	if (file != stdin)
		fclose(file);
	return r;
}

static void usage(const char *exe)
{
	fprintf(stderr, //
		"usage: %s [devpath] [command]\n"
		"commands:\n"
#ifdef CYANFS_DEBUG_ENABLED
		"  enable-debug\n"
		"  disable-debug\n"
#endif
		"  sleep SECOND\n"
		"  mount\n"
		"  umount\n"
		"  mkfs\n"
		"  statfs\n"
		"  list\n"
		"  compact\n"
		"  create FILENAME\n"
		"  fork FILENAME-FROM FILENAME-TO\n"
		"  rename FILENAME-FROM FILENAME-TO\n"
		"  truncate FILENAME SIZE\n"
		"  delete FILENAME\n"
		"  read FILENAME OFFSET [LENGTH]\n"
		"  write FILENAME OFFSET\n"
		"  fill FILENAME OFFSET LENGTH\n"
		"  fill-check FILENAME OFFSET LENGTH\n"
		"  zero FILENAME OFFSET LENGTH\n"
		"  zero-check FILENAME OFFSET LENGTH\n"
		"  discard FILENAME OFFSET LENGTH\n"
		"  extents FILENAME\n"
		"  batch COMMAND-LISTFILE\n",
		exe);
}

int main(int argc, const char *argv[])
{
	struct disk disk;
	int r;

	if (argc < 3) {
		usage(argv[0]);
		return -1;
	}

	disk.fd = open(argv[1], O_RDWR | O_EXCL, 0);
	if (disk.fd < 0) {
		fprintf(stderr, "failed open device %s, err %d\n", argv[1], errno);
		return -1;
	}
	r = stat_device_size(disk.fd, &disk.size);
	if (r < 0) {
		fprintf(stderr, "failed to get device size %s, err %d\n", argv[1], -r);
		return -1;
	}
	disk.super = NULL;
	r = call_fn(&disk, argc - 2, argv + 2);
	closefs(&disk);
	fsync(disk.fd);
	close(disk.fd);

#ifdef CYANFS_DEBUG_ENABLED
	if (cyanfs_atomic_read(&debug_malloc_counter)) {
		fprintf(stderr, "malloc counter error: %d", cyanfs_atomic_read(&debug_malloc_counter));
		return -1;
	}
#endif

	if (r < 0)
		return -1;
	return 0;
}
