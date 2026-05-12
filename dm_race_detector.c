#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include "dm_race_detector.h"

MODULE_AUTHOR("krevetkot");
MODULE_DESCRIPTION("Device-mapper target: block IO data-race detector");
MODULE_LICENSE("GPL v2");

static struct kmem_cache *inflight_node_cache;

static bool check_race_locked(struct race_ctx *ctx,
			       sector_t new_start, sector_t new_last,
			       bool new_is_write)
{
	struct inflight_node *node;
	bool race = false;

	for (node = inflight_it_iter_first(&ctx->tree, new_start, new_last);
	     node != NULL;
	     node = inflight_it_iter_next(node, new_start, new_last)) {

		if (node->is_write) {
			pr_warn("RACE detected: %s [%llu-%llu] overlaps "
				"in-flight WRITE [%llu-%llu] (bio %p)\n",
				new_is_write ? "WRITE" : "READ",
				(unsigned long long)new_start,
				(unsigned long long)new_last,
				(unsigned long long)node->start,
				(unsigned long long)node->last,
				node->bio);
			race = true;
		} else if (new_is_write) {
			pr_warn("RACE detected: WRITE [%llu-%llu] overlaps "
				"in-flight READ [%llu-%llu] (bio %p)\n",
				(unsigned long long)new_start,
				(unsigned long long)new_last,
				(unsigned long long)node->start,
				(unsigned long long)node->last,
				node->bio);
			race = true;
		}
	}
	return race;
}

static int mytarget_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct race_ctx *ctx;
	int ret;

	if (argc != 1) {
		ti->error = "Usage: mytarget <device>";
		return -EINVAL;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ti->error = "Cannot allocate context";
		return -ENOMEM;
	}

	ret = dm_get_device(ti, argv[0],
			    dm_table_get_mode(ti->table), &ctx->dev);
	if (ret) {
		ti->error = "Cannot get underlying device";
		kfree(ctx);
		return ret;
	}

	spin_lock_init(&ctx->lock);
	ctx->tree  = RB_ROOT_CACHED;
	ctx->start = 0;

	ti->private               = ctx;
	ti->num_flush_bios        = 1;
	ti->num_discard_bios      = 1;
	ti->num_secure_erase_bios = 1;

	return 0;
}

static void mytarget_dtr(struct dm_target *ti)
{
	struct race_ctx *ctx = ti->private;

	dm_put_device(ti, ctx->dev);
	kfree(ctx);
}

static int mytarget_end_io(struct dm_target *ti, struct bio *bio,
			   blk_status_t *error)
{
	struct race_ctx      *ctx  = ti->private;
	struct inflight_node *node = bio->bi_private;
	unsigned long         flags;

	if (!node)
		return DM_ENDIO_DONE;

	spin_lock_irqsave(&ctx->lock, flags);
	inflight_it_remove(node, &ctx->tree);
	spin_unlock_irqrestore(&ctx->lock, flags);

	kmem_cache_free(inflight_node_cache, node);
	bio->bi_private = NULL;

	return DM_ENDIO_DONE;
}

static int mytarget_map(struct dm_target *ti, struct bio *bio)
{
	struct race_ctx      *ctx = ti->private;
	struct inflight_node *node;
	sector_t              start, last;
	bool                  is_write;
	unsigned long         flags;

	bio_set_dev(bio, ctx->dev->bdev);
	bio->bi_iter.bi_sector += ctx->start;

	if (!bio_sectors(bio))
		return DM_MAPIO_REMAPPED;

	start    = bio->bi_iter.bi_sector;
	last     = bio_end_sector(bio) - 1;
	is_write = op_is_write(bio_op(bio));

	node = kmem_cache_alloc(inflight_node_cache, GFP_NOIO);
	if (unlikely(!node)) {
		pr_err("cannot allocate inflight_node, skipping race check\n");
		return DM_MAPIO_REMAPPED;
	}

	node->start    = start;
	node->last     = last;
	node->bio      = bio;
	node->is_write = is_write;

	spin_lock_irqsave(&ctx->lock, flags);
	check_race_locked(ctx, start, last, is_write);
	inflight_it_insert(node, &ctx->tree);
	spin_unlock_irqrestore(&ctx->lock, flags);

	bio->bi_private = node;

	return DM_MAPIO_REMAPPED;
}

static struct target_type race_detector_target = {
	.name    = "mytarget",
	.version = { 0, 1, 0 },
	.module  = THIS_MODULE,
	.ctr     = mytarget_ctr,
	.dtr     = mytarget_dtr,
	.map     = mytarget_map,
	.end_io  = mytarget_end_io,
};

static int __init dm_race_detector_init(void)
{
	int ret;

	inflight_node_cache = kmem_cache_create("dm_inflight_node",
						sizeof(struct inflight_node),
						0, SLAB_HWCACHE_ALIGN, NULL);
	if (!inflight_node_cache) {
		pr_err("failed to create slab cache\n");
		return -ENOMEM;
	}

	ret = dm_register_target(&race_detector_target);
	if (ret < 0) {
		pr_err("dm_register_target failed: %d\n", ret);
		kmem_cache_destroy(inflight_node_cache);
		return ret;
	}

	pr_info("loaded (target name: %s)\n", race_detector_target.name);
	return 0;
}

static void __exit dm_race_detector_exit(void)
{
	dm_unregister_target(&race_detector_target);
	kmem_cache_destroy(inflight_node_cache);
	pr_info("unloaded\n");
}

module_init(dm_race_detector_init);
module_exit(dm_race_detector_exit);
