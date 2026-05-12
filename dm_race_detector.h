#ifndef DM_RACE_DETECTOR_H
#define DM_RACE_DETECTOR_H

#include <linux/bio.h>
#include <linux/device-mapper.h>
#include <linux/spinlock.h>
#include <linux/rbtree_augmented.h>
#include <linux/interval_tree_generic.h>

struct inflight_node {
	struct rb_node  rb;
	sector_t        start;
	sector_t        last;
	sector_t        __subtree_last;
	struct bio     *bio;
	bool            is_write;
};

#define START(node) ((node)->start)
#define LAST(node)  ((node)->last)

INTERVAL_TREE_DEFINE(struct inflight_node, rb,
		     sector_t, __subtree_last,
		     START, LAST,
		     static inline, inflight_it)

struct race_ctx {
	struct dm_dev        *dev;
	sector_t              start;
	spinlock_t            lock;
	struct rb_root_cached tree;
};

#endif
