#ifndef __CYANFS_CORE_HEADER__
#define __CYANFS_CORE_HEADER__

#include "base.h"

#define CYANFS_SUPER_HEADER_SIZE 4096ULL
#define CYANFS_SUPER_MAGIC 0x23032420
#define CYANFS_SUPER_BLOCK_SIZE (2 * CYANFS_SUPER_HEADER_SIZE)

#define CYANFS_EXTENT_SHIFT (20ULL)
#define CYANFS_EXTENT_SIZE (1ULL << CYANFS_EXTENT_SHIFT)
#define CYANFS_EXTENT_MASK (CYANFS_EXTENT_SIZE - 1ULL)

#define CYANFS_FILE_ALIGN_SIZE (1ULL << 12)
#define CYANFS_FILE_ALIGN_MASK (CYANFS_FILE_ALIGN_SIZE - 1ULL)

typedef void (*cyanfs_ctx_fn)(void *ctx);

typedef uint64_t cyanfs_file_id_t; // 文件的唯一ID，全局递增
typedef uint64_t cyanfs_journal_seq_t;
typedef uint32_t cyanfs_extent_id;
#define cyanfs_extent_id_invalid (~0)

struct cyanfs_task;
struct cyanfs_file;
struct cyanfs_super;

typedef struct {
	uint8_t data[127];
	uint8_t zero;
} cyanfs_file_name_t;

struct cyanfs_file_meta {
	cyanfs_file_id_t id;
	cyanfs_file_id_t parent_id;
	cyanfs_file_name_t name;
	uint64_t size;
	uint32_t extents;
	uint32_t children;
};

typedef enum {
	CYANFS_MAP_NOP, // 无需向下层发送IO，直接标注完成
	CYANFS_MAP_SUBMIT, // 向下层转发IO
	CYANFS_MAP_REQUEUE, // 排队，延后执行
} cyanfs_map_type_t;

// 文件IO操作
typedef cyanfs_status (*cyanfs_io_spliter)(cyanfs_map_type_t type, uint64_t b_off, uint64_t len, void *ctx);
extern cyanfs_status cyanfs_read(struct cyanfs_file *f, void *ctx, uint64_t f_off, uint64_t len,
				 cyanfs_io_spliter handler);
extern cyanfs_status cyanfs_write(struct cyanfs_file *f, void *ctx, uint64_t f_off, uint64_t len,
				  cyanfs_io_spliter handler);
extern cyanfs_status cyanfs_discard(struct cyanfs_file *f, void *ctx, uint64_t f_off, uint64_t len,
				    cyanfs_io_spliter handler);
// 上层发出的Flush操作（包括REQ_OP_FLUSH或REQ_PREFLUSH），需要经过此函数分配映射动作
// 若结果是requeue，应将操作入队
// 待该操作出队后，无需再调用此函数进行分配，可直接Flush
extern cyanfs_status cyanfs_flush(struct cyanfs_file *f, cyanfs_map_type_t *type);
extern cyanfs_status cyanfs_requeue(struct cyanfs_file *f, void *ctx, cyanfs_ctx_fn fn);
extern uint64_t cyanfs_size(struct cyanfs_file *f);
extern cyanfs_status cyanfs_seek_extent(struct cyanfs_file *f, uint64_t *f_off);

typedef enum {
	CYANFS_TASK_NOP,
	CYANFS_TASK_READ,
	CYANFS_TASK_WRITE,
	CYANFS_TASK_COPY,
	CYANFS_TASK_FLUSH,
	CYANFS_TASK_DISCARD,
	CYANFS_TASK_REQUEUE,
} cyanfs_task_type_t;

typedef void (*cyanfs_task_done_fn)(struct cyanfs_task *t, cyanfs_status err);

struct cyanfs_task {
	struct cyanfs_super *super;
	struct cyanfs_list_head list;
	cyanfs_task_done_fn done;
	cyanfs_task_type_t type;
	union {
		struct { // 完成read、write后（不需要Flush），调用done
			uint64_t b_off;
			uint64_t len;
			void *buf;
		} read, write;
		struct { // 完成copy后（不需要Flush），调用done
			uint64_t b_src_off;
			uint64_t b_dst_off;
			uint64_t len;
		} copy;
		struct { // 向backend设备发送discard，完成后调用done
			uint64_t b_off;
			uint64_t len;
		} discard;
		struct { // 执行ctx，然后调用done
			struct cyanfs_file *file;
			void *ctx;
			cyanfs_ctx_fn fn;
		} requeue;
	};
};

typedef struct {
	uint32_t data[4];
} cyanfs_uuid_t;

struct cyanfs_super_meta {
	cyanfs_uuid_t uuid;
	uint32_t total_extents;
	uint32_t free_extents;
	uint32_t journal_extents;
	uint32_t data_extents;
	uint32_t reserved_extents;
	uint64_t size;
	uint32_t files;
};

struct cyanfs_super_header {
	cyanfs_uuid_t uuid;
	uint32_t magic;
	uint32_t crc32;
	uint64_t version;
	cyanfs_extent_id journal_id;
	cyanfs_journal_seq_t journal_seq;
};

extern struct cyanfs_super *cyanfs_super_open(uint64_t size, int discard, int compact);
extern void cyanfs_super_close(struct cyanfs_super *s);
extern struct cyanfs_task *cyanfs_super_get_task(struct cyanfs_super *s);
extern void cyanfs_super_set_new_task_callback(struct cyanfs_super *s, void *ctx, cyanfs_ctx_fn fn);
extern int cyanfs_super_is_ready(struct cyanfs_super *s);

extern cyanfs_status cyanfs_super_flush(struct cyanfs_super *s, int ondisk);
extern void cyanfs_super_compact(struct cyanfs_super *s);

extern cyanfs_status cyanfs_super_parse(struct cyanfs_super_header *h, void *super_block);
extern void cyanfs_super_make(cyanfs_uuid_t uuid, void *super_block);

extern cyanfs_status cyanfs_statfs(struct cyanfs_super *s, struct cyanfs_super_meta *meta);
extern cyanfs_status cyanfs_stat(struct cyanfs_super *s, cyanfs_file_id_t id, struct cyanfs_file_meta *meta);
extern cyanfs_status cyanfs_open(struct cyanfs_super *s, cyanfs_file_id_t id, int write, struct cyanfs_file **out);
extern cyanfs_status cyanfs_create(struct cyanfs_super *s, cyanfs_file_name_t name, cyanfs_file_id_t *id);
extern cyanfs_status cyanfs_fork(struct cyanfs_super *s, cyanfs_file_id_t from, cyanfs_file_name_t name_to,
				 cyanfs_file_id_t *id_to);
extern cyanfs_status cyanfs_rename(struct cyanfs_super *s, cyanfs_file_id_t id, cyanfs_file_name_t name);
extern cyanfs_status cyanfs_truncate(struct cyanfs_super *s, cyanfs_file_id_t id, uint64_t size);
extern cyanfs_status cyanfs_delete(struct cyanfs_super *s, cyanfs_file_id_t id);
extern void cyanfs_close(struct cyanfs_file *f);

const static struct cyanfs_file_meta cyanfs_list_init_iter = { 0 };
extern cyanfs_status cyanfs_list(struct cyanfs_super *s, struct cyanfs_file_meta *iter);
extern cyanfs_status cyanfs_lookup(struct cyanfs_super *s, cyanfs_file_name_t name, struct cyanfs_file_meta *meta);

#endif
