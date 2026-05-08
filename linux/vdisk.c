#include "cyanfs.h"

#define CYANFS_BLOCK_DEVICE_PRINTK(level, cbd, fmt, args...)                                                           \
	CYANFS_RAW_##level("(cyanfs %d:%d %d): " fmt, MAJOR((cbd)->backend->dev->bd_dev),                              \
			   MINOR((cbd)->backend->dev->bd_dev), (cbd)->id, ##args)
#define CYANFS_BLOCK_DEVICE_INFO(cbd, fmt, args...) CYANFS_BLOCK_DEVICE_PRINTK(INFO, cbd, fmt, ##args)
#define CYANFS_BLOCK_DEVICE_DEBUG(cbd, fmt, args...) CYANFS_BLOCK_DEVICE_PRINTK(DEBUG, cbd, fmt, ##args)

static DEFINE_IDR(cyanfs_idr);
static DEFINE_MUTEX(cyanfs_idr_mutex);

static int cyanfs_idr_alloc(struct cyanfs_block_device *cbd)
{
	int r;
	mutex_lock(&cyanfs_idr_mutex);
	r = idr_alloc(&cyanfs_idr, cbd, 0, 0, GFP_KERNEL);
	mutex_unlock(&cyanfs_idr_mutex);
	if (r < 0)
		return r;
	cbd->id = r;
	return 0;
}

static void cyanfs_idr_free(struct cyanfs_block_device *cbd)
{
	mutex_lock(&cyanfs_idr_mutex);
	idr_remove(&cyanfs_idr, cbd->id);
	mutex_unlock(&cyanfs_idr_mutex);
}

static inline struct gendisk *cyanfs_bio_to_gendisk(struct bio *bio)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
	return bio->bi_disk;
#else
	return bio->bi_bdev->bd_disk;
#endif
}

static void cyanfs_block_device_submit_bio(struct bio *bio)
{
	struct cyanfs_block_device *cbd = cyanfs_bio_to_gendisk(bio)->private_data;
	cyanfs_file_submit_bio(cbd->file, cbd->backend, bio);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
static blk_qc_t cyanfs_block_device_make_request(struct request_queue *q, struct bio *bio)
{
	cyanfs_block_device_submit_bio(bio);
	return BLK_QC_T_NONE;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 14, 0) && OPENEULER_MAJOR != 2203

static struct gendisk *blk_alloc_disk(int node)
{
	struct request_queue *q;
	struct gendisk *disk;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
	q = blk_alloc_queue_node(GFP_KERNEL, node, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
	q = blk_alloc_queue_node(GFP_KERNEL, node);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
	q = blk_alloc_queue(cyanfs_block_device_make_request, node);
#else
	q = blk_alloc_queue(node);
#endif
	if (!q)
		return NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
	blk_queue_make_request(q, cyanfs_block_device_make_request);
#endif

	disk = alloc_disk_node(0, node);
	if (!disk) {
		blk_cleanup_queue(q);
		return NULL;
	}
	disk->queue = q;
	return disk;
}

static void blk_cleanup_disk(struct gendisk *disk)
{
	blk_cleanup_queue(disk->queue);
	put_disk(disk);
}

#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
#define blk_cleanup_disk(disk) put_disk(disk)
#endif

static ssize_t cyanfs_block_device_backend_attr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct cyanfs_block_device *cbd = disk->private_data;
	struct cyanfs_backend *backend = cbd->backend;
	return sprintf(buf, "%pg\n", backend->dev);
}

static struct device_attribute cyanfs_block_device_backend_attr = {
	.attr = { .name = "cyanfs_backend", .mode = 0444 },
	.show = cyanfs_block_device_backend_attr_show,
};

static ssize_t cyanfs_block_device_file_attr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct cyanfs_block_device *cbd = disk->private_data;
	return sprintf(buf, "%llu\n", cbd->file_id);
}

static struct device_attribute cyanfs_block_device_file_attr = {
	.attr = { .name = "cyanfs_file", .mode = 0444 },
	.show = cyanfs_block_device_file_attr_show,
};

static struct attribute *cyanfs_block_device_attrs[] = {
	&cyanfs_block_device_backend_attr.attr,
	&cyanfs_block_device_file_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(cyanfs_block_device);

static void cyanfs_block_device_free_work(struct work_struct *work)
{
	struct cyanfs_block_device *cbd = container_of(work, struct cyanfs_block_device, work);
	struct cyanfs_backend *backend = cbd->backend;

	CYANFS_BLOCK_DEVICE_DEBUG(cbd, "close");
	del_gendisk(cbd->disk);
	blk_cleanup_disk(cbd->disk);
	cyanfs_close(cbd->file);
	cyanfs_idr_free(cbd);
	kfree(cbd);
	cyanfs_backend_put(backend);
}

static void cyanfs_block_device_free(struct kref *ref)
{
	struct cyanfs_block_device *cbd = container_of(ref, struct cyanfs_block_device, ref);
	// block operations的release函数中，不能直接释放gendisk
	schedule_work(&cbd->work);
}

void cyanfs_block_device_get(struct cyanfs_block_device *cbd)
{
	kref_get(&cbd->ref);
}

void cyanfs_block_device_put(struct cyanfs_block_device *cbd)
{
	kref_put(&cbd->ref, cyanfs_block_device_free);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
static int cyanfs_block_device_open(struct block_device *dev, fmode_t mode)
#else
static int cyanfs_block_device_open(struct gendisk *disk, blk_mode_t mode)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
	struct gendisk *disk = dev->bd_disk;
#endif
	struct cyanfs_block_device *cbd = disk->private_data;
	cyanfs_block_device_get(cbd);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
static void cyanfs_block_device_release(struct gendisk *disk, fmode_t mode)
#else
static void cyanfs_block_device_release(struct gendisk *disk)
#endif
{
	struct cyanfs_block_device *cbd = disk->private_data;
	cyanfs_block_device_put(cbd);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0) && LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
static blk_qc_t __cyanfs_block_device_submit_bio(struct bio *bio)
{
	cyanfs_block_device_submit_bio(bio);
	return BLK_QC_T_NONE;
}
#endif

static const struct block_device_operations cyanfs_block_device_ops = {
	.owner = THIS_MODULE,
	.open = cyanfs_block_device_open,
	.release = cyanfs_block_device_release,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
	.submit_bio = cyanfs_block_device_submit_bio,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
	.submit_bio = __cyanfs_block_device_submit_bio,
#endif
};

struct cyanfs_block_device *cyanfs_block_device_alloc(struct cyanfs_backend *backend, cyanfs_file_id_t id, int write)
{
	int error;
	struct cyanfs_block_device *cbd;

	cbd = kzalloc(sizeof(struct cyanfs_block_device), GFP_KERNEL);
	if (!cbd) {
		error = -ENOMEM;
		goto out;
	}
	cbd->backend = backend;
	cbd->file_id = id;
	kref_init(&cbd->ref);
	INIT_WORK(&cbd->work, cyanfs_block_device_free_work);
	cyanfs_backend_get(backend);

	error = cyanfs_idr_alloc(cbd);
	if (error < 0)
		goto cbd_out;

	error = cyanfs_open(backend->super, id, write, &cbd->file);
	if (error < 0)
		goto idr_out;

	if (!cyanfs_size(cbd->file)) {
		error = -EINVAL;
		goto file_out;
	}

	cbd->disk = blk_alloc_disk(NUMA_NO_NODE);
	if (!cbd->disk) {
		error = -ENOMEM;
		goto file_out;
	}

	cbd->disk->major = cyanfs_major;
	cbd->disk->first_minor = cbd->id * DISK_MAX_PARTS;
	cbd->disk->minors = DISK_MAX_PARTS;
	cbd->disk->fops = &cyanfs_block_device_ops;
	cbd->disk->private_data = cbd;
	snprintf(cbd->disk->disk_name, sizeof(cbd->disk->disk_name), CYANFS_DEVNAME "%d", cbd->id);
	set_capacity(cbd->disk, cyanfs_size(cbd->file) >> SECTOR_SHIFT);
	set_disk_ro(cbd->disk, !write);
	blk_queue_flag_set(QUEUE_FLAG_NONROT, cbd->disk->queue);
	blk_queue_physical_block_size(cbd->disk->queue, bdev_physical_block_size(backend->dev));
	blk_queue_logical_block_size(cbd->disk->queue, SECTOR_SIZE);
	blk_queue_write_cache(cbd->disk->queue, true, false);
	blk_queue_max_hw_sectors(cbd->disk->queue, queue_max_hw_sectors(bdev_get_queue(backend->dev)));
	blk_queue_dma_alignment(cbd->disk->queue, queue_dma_alignment(bdev_get_queue(backend->dev)));
	cbd->disk->queue->limits.discard_granularity = CYANFS_EXTENT_SIZE;
	blk_queue_max_discard_sectors(cbd->disk->queue, CYANFS_EXTENT_SIZE >> SECTOR_SHIFT);

	error = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
	disk_to_dev(cbd->disk)->groups = cyanfs_block_device_groups;
	add_disk(cbd->disk);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
	device_add_disk(NULL, cbd->disk, cyanfs_block_device_groups);
#else
	error = device_add_disk(NULL, cbd->disk, cyanfs_block_device_groups);
#endif
	if (error < 0)
		goto disk_out;

	CYANFS_BLOCK_DEVICE_DEBUG(cbd, "open");

	return cbd;

disk_out:
	blk_cleanup_disk(cbd->disk);
file_out:
	cyanfs_close(cbd->file);
idr_out:
	cyanfs_idr_free(cbd);
cbd_out:
	cyanfs_backend_put(backend);
	kfree(cbd);
out:
	return ERR_PTR(error);
}
