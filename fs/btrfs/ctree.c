/*
 * Copyright (C) 2007,2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "print-tree.h"
#include "locking.h"

static int split_node(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, int level);
static int split_leaf(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *ins_key,
		      struct btrfs_path *path, int data_size, int extend);
static int push_node_left(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct extent_buffer *dst,
			  struct extent_buffer *src, int empty);
static int balance_node_right(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct extent_buffer *dst_buf,
			      struct extent_buffer *src_buf);
static void del_ptr(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_path *path, int level, int slot);

struct btrfs_path *btrfs_alloc_path(void)
{
	struct btrfs_path *path;
	path = kmem_cache_zalloc(btrfs_path_cachep, GFP_NOFS);
	return path;
}

noinline void btrfs_set_path_blocking(struct btrfs_path *p)
{
	int i;
	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		if (!p->nodes[i] || !p->locks[i])
			continue;
		btrfs_set_lock_blocking_rw(p->nodes[i], p->locks[i]);
		if (p->locks[i] == BTRFS_READ_LOCK)
			p->locks[i] = BTRFS_READ_LOCK_BLOCKING;
		else if (p->locks[i] == BTRFS_WRITE_LOCK)
			p->locks[i] = BTRFS_WRITE_LOCK_BLOCKING;
	}
}

noinline void btrfs_clear_path_blocking(struct btrfs_path *p,
					struct extent_buffer *held, int held_rw)
{
	int i;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (held) {
		btrfs_set_lock_blocking_rw(held, held_rw);
		if (held_rw == BTRFS_WRITE_LOCK)
			held_rw = BTRFS_WRITE_LOCK_BLOCKING;
		else if (held_rw == BTRFS_READ_LOCK)
			held_rw = BTRFS_READ_LOCK_BLOCKING;
	}
	btrfs_set_path_blocking(p);
#endif

	for (i = BTRFS_MAX_LEVEL - 1; i >= 0; i--) {
		if (p->nodes[i] && p->locks[i]) {
			btrfs_clear_lock_blocking_rw(p->nodes[i], p->locks[i]);
			if (p->locks[i] == BTRFS_WRITE_LOCK_BLOCKING)
				p->locks[i] = BTRFS_WRITE_LOCK;
			else if (p->locks[i] == BTRFS_READ_LOCK_BLOCKING)
				p->locks[i] = BTRFS_READ_LOCK;
		}
	}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (held)
		btrfs_clear_lock_blocking_rw(held, held_rw);
#endif
}

void btrfs_free_path(struct btrfs_path *p)
{
	if (!p)
		return;
	btrfs_release_path(p);
	kmem_cache_free(btrfs_path_cachep, p);
}

noinline void btrfs_release_path(struct btrfs_path *p)
{
	int i;

	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		p->slots[i] = 0;
		if (!p->nodes[i])
			continue;
		if (p->locks[i]) {
			btrfs_tree_unlock_rw(p->nodes[i], p->locks[i]);
			p->locks[i] = 0;
		}
		free_extent_buffer(p->nodes[i]);
		p->nodes[i] = NULL;
	}
}

struct extent_buffer *btrfs_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;

	while (1) {
		rcu_read_lock();
		eb = rcu_dereference(root->node);

		if (atomic_inc_not_zero(&eb->refs)) {
			rcu_read_unlock();
			break;
		}
		rcu_read_unlock();
		synchronize_rcu();
	}
	return eb;
}

struct extent_buffer *btrfs_lock_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;

	while (1) {
		eb = btrfs_root_node(root);
		btrfs_tree_lock(eb);
		if (eb == root->node)
			break;
		btrfs_tree_unlock(eb);
		free_extent_buffer(eb);
	}
	return eb;
}

struct extent_buffer *btrfs_read_lock_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;

	while (1) {
		eb = btrfs_root_node(root);
		btrfs_tree_read_lock(eb);
		if (eb == root->node)
			break;
		btrfs_tree_read_unlock(eb);
		free_extent_buffer(eb);
	}
	return eb;
}

static void add_root_to_dirty_list(struct btrfs_root *root)
{
	spin_lock(&root->fs_info->trans_lock);
	if (root->track_dirty && list_empty(&root->dirty_list)) {
		list_add(&root->dirty_list,
			 &root->fs_info->dirty_cowonly_roots);
	}
	spin_unlock(&root->fs_info->trans_lock);
}

int btrfs_copy_root(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      struct extent_buffer *buf,
		      struct extent_buffer **cow_ret, u64 new_root_objectid)
{
	struct extent_buffer *cow;
	int ret = 0;
	int level;
	struct btrfs_disk_key disk_key;

	WARN_ON(root->ref_cows && trans->transid !=
		root->fs_info->running_transaction->transid);
	WARN_ON(root->ref_cows && trans->transid != root->last_trans);

	level = btrfs_header_level(buf);
	if (level == 0)
		btrfs_item_key(buf, &disk_key, 0);
	else
		btrfs_node_key(buf, &disk_key, 0);

	cow = btrfs_alloc_free_block(trans, root, buf->len, 0,
				     new_root_objectid, &disk_key, level,
				     buf->start, 0, 1);
	if (IS_ERR(cow))
		return PTR_ERR(cow);

	copy_extent_buffer(cow, buf, 0, 0, cow->len);
	btrfs_set_header_bytenr(cow, cow->start);
	btrfs_set_header_generation(cow, trans->transid);
	btrfs_set_header_backref_rev(cow, BTRFS_MIXED_BACKREF_REV);
	btrfs_clear_header_flag(cow, BTRFS_HEADER_FLAG_WRITTEN |
				     BTRFS_HEADER_FLAG_RELOC);
	if (new_root_objectid == BTRFS_TREE_RELOC_OBJECTID)
		btrfs_set_header_flag(cow, BTRFS_HEADER_FLAG_RELOC);
	else
		btrfs_set_header_owner(cow, new_root_objectid);

	write_extent_buffer(cow, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(cow),
			    BTRFS_FSID_SIZE);

	WARN_ON(btrfs_header_generation(buf) > trans->transid);
	if (new_root_objectid == BTRFS_TREE_RELOC_OBJECTID)
		ret = btrfs_inc_ref(trans, root, cow, 1, 1);
	else
		ret = btrfs_inc_ref(trans, root, cow, 0, 1);

	if (ret)
		return ret;

	btrfs_mark_buffer_dirty(cow);
	*cow_ret = cow;
	return 0;
}

int btrfs_block_can_be_shared(struct btrfs_root *root,
			      struct extent_buffer *buf)
{
	if (root->ref_cows &&
	    buf != root->node && buf != root->commit_root &&
	    (btrfs_header_generation(buf) <=
	     btrfs_root_last_snapshot(&root->root_item) ||
	     btrfs_header_flag(buf, BTRFS_HEADER_FLAG_RELOC)))
		return 1;
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (root->ref_cows &&
	    btrfs_header_backref_rev(buf) < BTRFS_MIXED_BACKREF_REV)
		return 1;
#endif
	return 0;
}

static noinline int update_ref_for_cow(struct btrfs_trans_handle *trans,
				       struct btrfs_root *root,
				       struct extent_buffer *buf,
				       struct extent_buffer *cow,
				       int *last_ref)
{
	u64 refs;
	u64 owner;
	u64 flags;
	u64 new_flags = 0;
	int ret;


	if (btrfs_block_can_be_shared(root, buf)) {
		ret = btrfs_lookup_extent_info(trans, root, buf->start,
					       buf->len, &refs, &flags);
		if (ret)
			return ret;
		if (refs == 0) {
			ret = -EROFS;
			btrfs_std_error(root->fs_info, ret);
			return ret;
		}
	} else {
		refs = 1;
		if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID ||
		    btrfs_header_backref_rev(buf) < BTRFS_MIXED_BACKREF_REV)
			flags = BTRFS_BLOCK_FLAG_FULL_BACKREF;
		else
			flags = 0;
	}

	owner = btrfs_header_owner(buf);
	BUG_ON(owner == BTRFS_TREE_RELOC_OBJECTID &&
	       !(flags & BTRFS_BLOCK_FLAG_FULL_BACKREF));

	if (refs > 1) {
		if ((owner == root->root_key.objectid ||
		     root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID) &&
		    !(flags & BTRFS_BLOCK_FLAG_FULL_BACKREF)) {
			ret = btrfs_inc_ref(trans, root, buf, 1, 1);
			BUG_ON(ret); 

			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID) {
				ret = btrfs_dec_ref(trans, root, buf, 0, 1);
				BUG_ON(ret); 
				ret = btrfs_inc_ref(trans, root, cow, 1, 1);
				BUG_ON(ret); 
			}
			new_flags |= BTRFS_BLOCK_FLAG_FULL_BACKREF;
		} else {

			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID)
				ret = btrfs_inc_ref(trans, root, cow, 1, 1);
			else
				ret = btrfs_inc_ref(trans, root, cow, 0, 1);
			BUG_ON(ret); 
		}
		if (new_flags != 0) {
			ret = btrfs_set_disk_extent_flags(trans, root,
							  buf->start,
							  buf->len,
							  new_flags, 0);
			if (ret)
				return ret;
		}
	} else {
		if (flags & BTRFS_BLOCK_FLAG_FULL_BACKREF) {
			if (root->root_key.objectid ==
			    BTRFS_TREE_RELOC_OBJECTID)
				ret = btrfs_inc_ref(trans, root, cow, 1, 1);
			else
				ret = btrfs_inc_ref(trans, root, cow, 0, 1);
			BUG_ON(ret); 
			ret = btrfs_dec_ref(trans, root, buf, 1, 1);
			BUG_ON(ret); 
		}
		clean_tree_block(trans, root, buf);
		*last_ref = 1;
	}
	return 0;
}

static noinline int __btrfs_cow_block(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct extent_buffer *buf,
			     struct extent_buffer *parent, int parent_slot,
			     struct extent_buffer **cow_ret,
			     u64 search_start, u64 empty_size)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *cow;
	int level, ret;
	int last_ref = 0;
	int unlock_orig = 0;
	u64 parent_start;

	if (*cow_ret == buf)
		unlock_orig = 1;

	btrfs_assert_tree_locked(buf);

	WARN_ON(root->ref_cows && trans->transid !=
		root->fs_info->running_transaction->transid);
	WARN_ON(root->ref_cows && trans->transid != root->last_trans);

	level = btrfs_header_level(buf);

	if (level == 0)
		btrfs_item_key(buf, &disk_key, 0);
	else
		btrfs_node_key(buf, &disk_key, 0);

	if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID) {
		if (parent)
			parent_start = parent->start;
		else
			parent_start = 0;
	} else
		parent_start = 0;

	cow = btrfs_alloc_free_block(trans, root, buf->len, parent_start,
				     root->root_key.objectid, &disk_key,
				     level, search_start, empty_size, 1);
	if (IS_ERR(cow))
		return PTR_ERR(cow);

	

	copy_extent_buffer(cow, buf, 0, 0, cow->len);
	btrfs_set_header_bytenr(cow, cow->start);
	btrfs_set_header_generation(cow, trans->transid);
	btrfs_set_header_backref_rev(cow, BTRFS_MIXED_BACKREF_REV);
	btrfs_clear_header_flag(cow, BTRFS_HEADER_FLAG_WRITTEN |
				     BTRFS_HEADER_FLAG_RELOC);
	if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID)
		btrfs_set_header_flag(cow, BTRFS_HEADER_FLAG_RELOC);
	else
		btrfs_set_header_owner(cow, root->root_key.objectid);

	write_extent_buffer(cow, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(cow),
			    BTRFS_FSID_SIZE);

	ret = update_ref_for_cow(trans, root, buf, cow, &last_ref);
	if (ret) {
		btrfs_abort_transaction(trans, root, ret);
		return ret;
	}

	if (root->ref_cows)
		btrfs_reloc_cow_block(trans, root, buf, cow);

	if (buf == root->node) {
		WARN_ON(parent && parent != buf);
		if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID ||
		    btrfs_header_backref_rev(buf) < BTRFS_MIXED_BACKREF_REV)
			parent_start = buf->start;
		else
			parent_start = 0;

		extent_buffer_get(cow);
		rcu_assign_pointer(root->node, cow);

		btrfs_free_tree_block(trans, root, buf, parent_start,
				      last_ref, 1);
		free_extent_buffer(buf);
		add_root_to_dirty_list(root);
	} else {
		if (root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID)
			parent_start = parent->start;
		else
			parent_start = 0;

		WARN_ON(trans->transid != btrfs_header_generation(parent));
		btrfs_set_node_blockptr(parent, parent_slot,
					cow->start);
		btrfs_set_node_ptr_generation(parent, parent_slot,
					      trans->transid);
		btrfs_mark_buffer_dirty(parent);
		btrfs_free_tree_block(trans, root, buf, parent_start,
				      last_ref, 1);
	}
	if (unlock_orig)
		btrfs_tree_unlock(buf);
	free_extent_buffer_stale(buf);
	btrfs_mark_buffer_dirty(cow);
	*cow_ret = cow;
	return 0;
}

static inline int should_cow_block(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct extent_buffer *buf)
{
	
	smp_rmb();

	if (btrfs_header_generation(buf) == trans->transid &&
	    !btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN) &&
	    !(root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID &&
	      btrfs_header_flag(buf, BTRFS_HEADER_FLAG_RELOC)) &&
	    !root->force_cow)
		return 0;
	return 1;
}

/*
 * cows a single block, see __btrfs_cow_block for the real work.
 * This version of it has extra checks so that a block isn't cow'd more than
 * once per transaction, as long as it hasn't been written yet
 */
noinline int btrfs_cow_block(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct extent_buffer *buf,
		    struct extent_buffer *parent, int parent_slot,
		    struct extent_buffer **cow_ret)
{
	u64 search_start;
	int ret;

	if (trans->transaction != root->fs_info->running_transaction) {
		printk(KERN_CRIT "trans %llu running %llu\n",
		       (unsigned long long)trans->transid,
		       (unsigned long long)
		       root->fs_info->running_transaction->transid);
		WARN_ON(1);
	}
	if (trans->transid != root->fs_info->generation) {
		printk(KERN_CRIT "trans %llu running %llu\n",
		       (unsigned long long)trans->transid,
		       (unsigned long long)root->fs_info->generation);
		WARN_ON(1);
	}

	if (!should_cow_block(trans, root, buf)) {
		*cow_ret = buf;
		return 0;
	}

	search_start = buf->start & ~((u64)(1024 * 1024 * 1024) - 1);

	if (parent)
		btrfs_set_lock_blocking(parent);
	btrfs_set_lock_blocking(buf);

	ret = __btrfs_cow_block(trans, root, buf, parent,
				 parent_slot, cow_ret, search_start, 0);

	trace_btrfs_cow_block(root, buf, *cow_ret);

	return ret;
}

static int close_blocks(u64 blocknr, u64 other, u32 blocksize)
{
	if (blocknr < other && other - (blocknr + blocksize) < 32768)
		return 1;
	if (blocknr > other && blocknr - (other + blocksize) < 32768)
		return 1;
	return 0;
}

static int comp_keys(struct btrfs_disk_key *disk, struct btrfs_key *k2)
{
	struct btrfs_key k1;

	btrfs_disk_key_to_cpu(&k1, disk);

	return btrfs_comp_cpu_keys(&k1, k2);
}

int btrfs_comp_cpu_keys(struct btrfs_key *k1, struct btrfs_key *k2)
{
	if (k1->objectid > k2->objectid)
		return 1;
	if (k1->objectid < k2->objectid)
		return -1;
	if (k1->type > k2->type)
		return 1;
	if (k1->type < k2->type)
		return -1;
	if (k1->offset > k2->offset)
		return 1;
	if (k1->offset < k2->offset)
		return -1;
	return 0;
}

int btrfs_realloc_node(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct extent_buffer *parent,
		       int start_slot, int cache_only, u64 *last_ret,
		       struct btrfs_key *progress)
{
	struct extent_buffer *cur;
	u64 blocknr;
	u64 gen;
	u64 search_start = *last_ret;
	u64 last_block = 0;
	u64 other;
	u32 parent_nritems;
	int end_slot;
	int i;
	int err = 0;
	int parent_level;
	int uptodate;
	u32 blocksize;
	int progress_passed = 0;
	struct btrfs_disk_key disk_key;

	parent_level = btrfs_header_level(parent);
	if (cache_only && parent_level != 1)
		return 0;

	if (trans->transaction != root->fs_info->running_transaction)
		WARN_ON(1);
	if (trans->transid != root->fs_info->generation)
		WARN_ON(1);

	parent_nritems = btrfs_header_nritems(parent);
	blocksize = btrfs_level_size(root, parent_level - 1);
	end_slot = parent_nritems;

	if (parent_nritems == 1)
		return 0;

	btrfs_set_lock_blocking(parent);

	for (i = start_slot; i < end_slot; i++) {
		int close = 1;

		btrfs_node_key(parent, &disk_key, i);
		if (!progress_passed && comp_keys(&disk_key, progress) < 0)
			continue;

		progress_passed = 1;
		blocknr = btrfs_node_blockptr(parent, i);
		gen = btrfs_node_ptr_generation(parent, i);
		if (last_block == 0)
			last_block = blocknr;

		if (i > 0) {
			other = btrfs_node_blockptr(parent, i - 1);
			close = close_blocks(blocknr, other, blocksize);
		}
		if (!close && i < end_slot - 2) {
			other = btrfs_node_blockptr(parent, i + 1);
			close = close_blocks(blocknr, other, blocksize);
		}
		if (close) {
			last_block = blocknr;
			continue;
		}

		cur = btrfs_find_tree_block(root, blocknr, blocksize);
		if (cur)
			uptodate = btrfs_buffer_uptodate(cur, gen, 0);
		else
			uptodate = 0;
		if (!cur || !uptodate) {
			if (cache_only) {
				free_extent_buffer(cur);
				continue;
			}
			if (!cur) {
				cur = read_tree_block(root, blocknr,
							 blocksize, gen);
				if (!cur)
					return -EIO;
			} else if (!uptodate) {
				btrfs_read_buffer(cur, gen);
			}
		}
		if (search_start == 0)
			search_start = last_block;

		btrfs_tree_lock(cur);
		btrfs_set_lock_blocking(cur);
		err = __btrfs_cow_block(trans, root, cur, parent, i,
					&cur, search_start,
					min(16 * blocksize,
					    (end_slot - i) * blocksize));
		if (err) {
			btrfs_tree_unlock(cur);
			free_extent_buffer(cur);
			break;
		}
		search_start = cur->start;
		last_block = cur->start;
		*last_ret = search_start;
		btrfs_tree_unlock(cur);
		free_extent_buffer(cur);
	}
	return err;
}

static inline unsigned int leaf_data_end(struct btrfs_root *root,
					 struct extent_buffer *leaf)
{
	u32 nr = btrfs_header_nritems(leaf);
	if (nr == 0)
		return BTRFS_LEAF_DATA_SIZE(root);
	return btrfs_item_offset_nr(leaf, nr - 1);
}


static noinline int generic_bin_search(struct extent_buffer *eb,
				       unsigned long p,
				       int item_size, struct btrfs_key *key,
				       int max, int *slot)
{
	int low = 0;
	int high = max;
	int mid;
	int ret;
	struct btrfs_disk_key *tmp = NULL;
	struct btrfs_disk_key unaligned;
	unsigned long offset;
	char *kaddr = NULL;
	unsigned long map_start = 0;
	unsigned long map_len = 0;
	int err;

	while (low < high) {
		mid = (low + high) / 2;
		offset = p + mid * item_size;

		if (!kaddr || offset < map_start ||
		    (offset + sizeof(struct btrfs_disk_key)) >
		    map_start + map_len) {

			err = map_private_extent_buffer(eb, offset,
						sizeof(struct btrfs_disk_key),
						&kaddr, &map_start, &map_len);

			if (!err) {
				tmp = (struct btrfs_disk_key *)(kaddr + offset -
							map_start);
			} else {
				read_extent_buffer(eb, &unaligned,
						   offset, sizeof(unaligned));
				tmp = &unaligned;
			}

		} else {
			tmp = (struct btrfs_disk_key *)(kaddr + offset -
							map_start);
		}
		ret = comp_keys(tmp, key);

		if (ret < 0)
			low = mid + 1;
		else if (ret > 0)
			high = mid;
		else {
			*slot = mid;
			return 0;
		}
	}
	*slot = low;
	return 1;
}

static int bin_search(struct extent_buffer *eb, struct btrfs_key *key,
		      int level, int *slot)
{
	if (level == 0) {
		return generic_bin_search(eb,
					  offsetof(struct btrfs_leaf, items),
					  sizeof(struct btrfs_item),
					  key, btrfs_header_nritems(eb),
					  slot);
	} else {
		return generic_bin_search(eb,
					  offsetof(struct btrfs_node, ptrs),
					  sizeof(struct btrfs_key_ptr),
					  key, btrfs_header_nritems(eb),
					  slot);
	}
	return -1;
}

int btrfs_bin_search(struct extent_buffer *eb, struct btrfs_key *key,
		     int level, int *slot)
{
	return bin_search(eb, key, level, slot);
}

static void root_add_used(struct btrfs_root *root, u32 size)
{
	spin_lock(&root->accounting_lock);
	btrfs_set_root_used(&root->root_item,
			    btrfs_root_used(&root->root_item) + size);
	spin_unlock(&root->accounting_lock);
}

static void root_sub_used(struct btrfs_root *root, u32 size)
{
	spin_lock(&root->accounting_lock);
	btrfs_set_root_used(&root->root_item,
			    btrfs_root_used(&root->root_item) - size);
	spin_unlock(&root->accounting_lock);
}

static noinline struct extent_buffer *read_node_slot(struct btrfs_root *root,
				   struct extent_buffer *parent, int slot)
{
	int level = btrfs_header_level(parent);
	if (slot < 0)
		return NULL;
	if (slot >= btrfs_header_nritems(parent))
		return NULL;

	BUG_ON(level == 0);

	return read_tree_block(root, btrfs_node_blockptr(parent, slot),
		       btrfs_level_size(root, level - 1),
		       btrfs_node_ptr_generation(parent, slot));
}

static noinline int balance_level(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 struct btrfs_path *path, int level)
{
	struct extent_buffer *right = NULL;
	struct extent_buffer *mid;
	struct extent_buffer *left = NULL;
	struct extent_buffer *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];
	u64 orig_ptr;

	if (level == 0)
		return 0;

	mid = path->nodes[level];

	WARN_ON(path->locks[level] != BTRFS_WRITE_LOCK &&
		path->locks[level] != BTRFS_WRITE_LOCK_BLOCKING);
	WARN_ON(btrfs_header_generation(mid) != trans->transid);

	orig_ptr = btrfs_node_blockptr(mid, orig_slot);

	if (level < BTRFS_MAX_LEVEL - 1) {
		parent = path->nodes[level + 1];
		pslot = path->slots[level + 1];
	}

	if (!parent) {
		struct extent_buffer *child;

		if (btrfs_header_nritems(mid) != 1)
			return 0;

		
		child = read_node_slot(root, mid, 0);
		if (!child) {
			ret = -EROFS;
			btrfs_std_error(root->fs_info, ret);
			goto enospc;
		}

		btrfs_tree_lock(child);
		btrfs_set_lock_blocking(child);
		ret = btrfs_cow_block(trans, root, child, mid, 0, &child);
		if (ret) {
			btrfs_tree_unlock(child);
			free_extent_buffer(child);
			goto enospc;
		}

		rcu_assign_pointer(root->node, child);

		add_root_to_dirty_list(root);
		btrfs_tree_unlock(child);

		path->locks[level] = 0;
		path->nodes[level] = NULL;
		clean_tree_block(trans, root, mid);
		btrfs_tree_unlock(mid);
		
		free_extent_buffer(mid);

		root_sub_used(root, mid->len);
		btrfs_free_tree_block(trans, root, mid, 0, 1, 0);
		
		free_extent_buffer_stale(mid);
		return 0;
	}
	if (btrfs_header_nritems(mid) >
	    BTRFS_NODEPTRS_PER_BLOCK(root) / 4)
		return 0;

	btrfs_header_nritems(mid);

	left = read_node_slot(root, parent, pslot - 1);
	if (left) {
		btrfs_tree_lock(left);
		btrfs_set_lock_blocking(left);
		wret = btrfs_cow_block(trans, root, left,
				       parent, pslot - 1, &left);
		if (wret) {
			ret = wret;
			goto enospc;
		}
	}
	right = read_node_slot(root, parent, pslot + 1);
	if (right) {
		btrfs_tree_lock(right);
		btrfs_set_lock_blocking(right);
		wret = btrfs_cow_block(trans, root, right,
				       parent, pslot + 1, &right);
		if (wret) {
			ret = wret;
			goto enospc;
		}
	}

	
	if (left) {
		orig_slot += btrfs_header_nritems(left);
		wret = push_node_left(trans, root, left, mid, 1);
		if (wret < 0)
			ret = wret;
		btrfs_header_nritems(mid);
	}

	if (right) {
		wret = push_node_left(trans, root, mid, right, 1);
		if (wret < 0 && wret != -ENOSPC)
			ret = wret;
		if (btrfs_header_nritems(right) == 0) {
			clean_tree_block(trans, root, right);
			btrfs_tree_unlock(right);
			del_ptr(trans, root, path, level + 1, pslot + 1);
			root_sub_used(root, right->len);
			btrfs_free_tree_block(trans, root, right, 0, 1, 0);
			free_extent_buffer_stale(right);
			right = NULL;
		} else {
			struct btrfs_disk_key right_key;
			btrfs_node_key(right, &right_key, 0);
			btrfs_set_node_key(parent, &right_key, pslot + 1);
			btrfs_mark_buffer_dirty(parent);
		}
	}
	if (btrfs_header_nritems(mid) == 1) {
		if (!left) {
			ret = -EROFS;
			btrfs_std_error(root->fs_info, ret);
			goto enospc;
		}
		wret = balance_node_right(trans, root, mid, left);
		if (wret < 0) {
			ret = wret;
			goto enospc;
		}
		if (wret == 1) {
			wret = push_node_left(trans, root, left, mid, 1);
			if (wret < 0)
				ret = wret;
		}
		BUG_ON(wret == 1);
	}
	if (btrfs_header_nritems(mid) == 0) {
		clean_tree_block(trans, root, mid);
		btrfs_tree_unlock(mid);
		del_ptr(trans, root, path, level + 1, pslot);
		root_sub_used(root, mid->len);
		btrfs_free_tree_block(trans, root, mid, 0, 1, 0);
		free_extent_buffer_stale(mid);
		mid = NULL;
	} else {
		
		struct btrfs_disk_key mid_key;
		btrfs_node_key(mid, &mid_key, 0);
		btrfs_set_node_key(parent, &mid_key, pslot);
		btrfs_mark_buffer_dirty(parent);
	}

	
	if (left) {
		if (btrfs_header_nritems(left) > orig_slot) {
			extent_buffer_get(left);
			
			path->nodes[level] = left;
			path->slots[level + 1] -= 1;
			path->slots[level] = orig_slot;
			if (mid) {
				btrfs_tree_unlock(mid);
				free_extent_buffer(mid);
			}
		} else {
			orig_slot -= btrfs_header_nritems(left);
			path->slots[level] = orig_slot;
		}
	}
	
	if (orig_ptr !=
	    btrfs_node_blockptr(path->nodes[level], path->slots[level]))
		BUG();
enospc:
	if (right) {
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}
	if (left) {
		if (path->nodes[level] != left)
			btrfs_tree_unlock(left);
		free_extent_buffer(left);
	}
	return ret;
}

static noinline int push_nodes_for_insert(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path, int level)
{
	struct extent_buffer *right = NULL;
	struct extent_buffer *mid;
	struct extent_buffer *left = NULL;
	struct extent_buffer *parent = NULL;
	int ret = 0;
	int wret;
	int pslot;
	int orig_slot = path->slots[level];

	if (level == 0)
		return 1;

	mid = path->nodes[level];
	WARN_ON(btrfs_header_generation(mid) != trans->transid);

	if (level < BTRFS_MAX_LEVEL - 1) {
		parent = path->nodes[level + 1];
		pslot = path->slots[level + 1];
	}

	if (!parent)
		return 1;

	left = read_node_slot(root, parent, pslot - 1);

	
	if (left) {
		u32 left_nr;

		btrfs_tree_lock(left);
		btrfs_set_lock_blocking(left);

		left_nr = btrfs_header_nritems(left);
		if (left_nr >= BTRFS_NODEPTRS_PER_BLOCK(root) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, left, parent,
					      pslot - 1, &left);
			if (ret)
				wret = 1;
			else {
				wret = push_node_left(trans, root,
						      left, mid, 0);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			struct btrfs_disk_key disk_key;
			orig_slot += left_nr;
			btrfs_node_key(mid, &disk_key, 0);
			btrfs_set_node_key(parent, &disk_key, pslot);
			btrfs_mark_buffer_dirty(parent);
			if (btrfs_header_nritems(left) > orig_slot) {
				path->nodes[level] = left;
				path->slots[level + 1] -= 1;
				path->slots[level] = orig_slot;
				btrfs_tree_unlock(mid);
				free_extent_buffer(mid);
			} else {
				orig_slot -=
					btrfs_header_nritems(left);
				path->slots[level] = orig_slot;
				btrfs_tree_unlock(left);
				free_extent_buffer(left);
			}
			return 0;
		}
		btrfs_tree_unlock(left);
		free_extent_buffer(left);
	}
	right = read_node_slot(root, parent, pslot + 1);

	if (right) {
		u32 right_nr;

		btrfs_tree_lock(right);
		btrfs_set_lock_blocking(right);

		right_nr = btrfs_header_nritems(right);
		if (right_nr >= BTRFS_NODEPTRS_PER_BLOCK(root) - 1) {
			wret = 1;
		} else {
			ret = btrfs_cow_block(trans, root, right,
					      parent, pslot + 1,
					      &right);
			if (ret)
				wret = 1;
			else {
				wret = balance_node_right(trans, root,
							  right, mid);
			}
		}
		if (wret < 0)
			ret = wret;
		if (wret == 0) {
			struct btrfs_disk_key disk_key;

			btrfs_node_key(right, &disk_key, 0);
			btrfs_set_node_key(parent, &disk_key, pslot + 1);
			btrfs_mark_buffer_dirty(parent);

			if (btrfs_header_nritems(mid) <= orig_slot) {
				path->nodes[level] = right;
				path->slots[level + 1] += 1;
				path->slots[level] = orig_slot -
					btrfs_header_nritems(mid);
				btrfs_tree_unlock(mid);
				free_extent_buffer(mid);
			} else {
				btrfs_tree_unlock(right);
				free_extent_buffer(right);
			}
			return 0;
		}
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}
	return 1;
}

static void reada_for_search(struct btrfs_root *root,
			     struct btrfs_path *path,
			     int level, int slot, u64 objectid)
{
	struct extent_buffer *node;
	struct btrfs_disk_key disk_key;
	u32 nritems;
	u64 search;
	u64 target;
	u64 nread = 0;
	u64 gen;
	int direction = path->reada;
	struct extent_buffer *eb;
	u32 nr;
	u32 blocksize;
	u32 nscan = 0;

	if (level != 1)
		return;

	if (!path->nodes[level])
		return;

	node = path->nodes[level];

	search = btrfs_node_blockptr(node, slot);
	blocksize = btrfs_level_size(root, level - 1);
	eb = btrfs_find_tree_block(root, search, blocksize);
	if (eb) {
		free_extent_buffer(eb);
		return;
	}

	target = search;

	nritems = btrfs_header_nritems(node);
	nr = slot;

	while (1) {
		if (direction < 0) {
			if (nr == 0)
				break;
			nr--;
		} else if (direction > 0) {
			nr++;
			if (nr >= nritems)
				break;
		}
		if (path->reada < 0 && objectid) {
			btrfs_node_key(node, &disk_key, nr);
			if (btrfs_disk_key_objectid(&disk_key) != objectid)
				break;
		}
		search = btrfs_node_blockptr(node, nr);
		if ((search <= target && target - search <= 65536) ||
		    (search > target && search - target <= 65536)) {
			gen = btrfs_node_ptr_generation(node, nr);
			readahead_tree_block(root, search, blocksize, gen);
			nread += blocksize;
		}
		nscan++;
		if ((nread > 65536 || nscan > 32))
			break;
	}
}

static noinline int reada_for_balance(struct btrfs_root *root,
				      struct btrfs_path *path, int level)
{
	int slot;
	int nritems;
	struct extent_buffer *parent;
	struct extent_buffer *eb;
	u64 gen;
	u64 block1 = 0;
	u64 block2 = 0;
	int ret = 0;
	int blocksize;

	parent = path->nodes[level + 1];
	if (!parent)
		return 0;

	nritems = btrfs_header_nritems(parent);
	slot = path->slots[level + 1];
	blocksize = btrfs_level_size(root, level);

	if (slot > 0) {
		block1 = btrfs_node_blockptr(parent, slot - 1);
		gen = btrfs_node_ptr_generation(parent, slot - 1);
		eb = btrfs_find_tree_block(root, block1, blocksize);
		if (eb && btrfs_buffer_uptodate(eb, gen, 1) != 0)
			block1 = 0;
		free_extent_buffer(eb);
	}
	if (slot + 1 < nritems) {
		block2 = btrfs_node_blockptr(parent, slot + 1);
		gen = btrfs_node_ptr_generation(parent, slot + 1);
		eb = btrfs_find_tree_block(root, block2, blocksize);
		if (eb && btrfs_buffer_uptodate(eb, gen, 1) != 0)
			block2 = 0;
		free_extent_buffer(eb);
	}
	if (block1 || block2) {
		ret = -EAGAIN;

		
		btrfs_release_path(path);

		
		if (block1)
			readahead_tree_block(root, block1, blocksize, 0);
		if (block2)
			readahead_tree_block(root, block2, blocksize, 0);

		if (block1) {
			eb = read_tree_block(root, block1, blocksize, 0);
			free_extent_buffer(eb);
		}
		if (block2) {
			eb = read_tree_block(root, block2, blocksize, 0);
			free_extent_buffer(eb);
		}
	}
	return ret;
}


static noinline void unlock_up(struct btrfs_path *path, int level,
			       int lowest_unlock, int min_write_lock_level,
			       int *write_lock_level)
{
	int i;
	int skip_level = level;
	int no_skips = 0;
	struct extent_buffer *t;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		if (!path->nodes[i])
			break;
		if (!path->locks[i])
			break;
		if (!no_skips && path->slots[i] == 0) {
			skip_level = i + 1;
			continue;
		}
		if (!no_skips && path->keep_locks) {
			u32 nritems;
			t = path->nodes[i];
			nritems = btrfs_header_nritems(t);
			if (nritems < 1 || path->slots[i] >= nritems - 1) {
				skip_level = i + 1;
				continue;
			}
		}
		if (skip_level < i && i >= lowest_unlock)
			no_skips = 1;

		t = path->nodes[i];
		if (i >= lowest_unlock && i > skip_level && path->locks[i]) {
			btrfs_tree_unlock_rw(t, path->locks[i]);
			path->locks[i] = 0;
			if (write_lock_level &&
			    i > min_write_lock_level &&
			    i <= *write_lock_level) {
				*write_lock_level = i - 1;
			}
		}
	}
}

noinline void btrfs_unlock_up_safe(struct btrfs_path *path, int level)
{
	int i;

	if (path->keep_locks)
		return;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		if (!path->nodes[i])
			continue;
		if (!path->locks[i])
			continue;
		btrfs_tree_unlock_rw(path->nodes[i], path->locks[i]);
		path->locks[i] = 0;
	}
}

static int
read_block_for_search(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct btrfs_path *p,
		       struct extent_buffer **eb_ret, int level, int slot,
		       struct btrfs_key *key)
{
	u64 blocknr;
	u64 gen;
	u32 blocksize;
	struct extent_buffer *b = *eb_ret;
	struct extent_buffer *tmp;
	int ret;

	blocknr = btrfs_node_blockptr(b, slot);
	gen = btrfs_node_ptr_generation(b, slot);
	blocksize = btrfs_level_size(root, level - 1);

	tmp = btrfs_find_tree_block(root, blocknr, blocksize);
	if (tmp) {
		
		if (btrfs_buffer_uptodate(tmp, 0, 1) > 0) {
			if (btrfs_buffer_uptodate(tmp, gen, 1) > 0) {
				*eb_ret = tmp;
				return 0;
			}
			free_extent_buffer(tmp);
			btrfs_set_path_blocking(p);

			
			tmp = read_tree_block(root, blocknr, blocksize, gen);
			if (tmp && btrfs_buffer_uptodate(tmp, gen, 0) > 0) {
				*eb_ret = tmp;
				return 0;
			}
			free_extent_buffer(tmp);
			btrfs_release_path(p);
			return -EIO;
		}
	}

	btrfs_unlock_up_safe(p, level + 1);
	btrfs_set_path_blocking(p);

	free_extent_buffer(tmp);
	if (p->reada)
		reada_for_search(root, p, level, slot, key->objectid);

	btrfs_release_path(p);

	ret = -EAGAIN;
	tmp = read_tree_block(root, blocknr, blocksize, 0);
	if (tmp) {
		if (!btrfs_buffer_uptodate(tmp, 0, 0))
			ret = -EIO;
		free_extent_buffer(tmp);
	}
	return ret;
}

static int
setup_nodes_for_search(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct btrfs_path *p,
		       struct extent_buffer *b, int level, int ins_len,
		       int *write_lock_level)
{
	int ret;
	if ((p->search_for_split || ins_len > 0) && btrfs_header_nritems(b) >=
	    BTRFS_NODEPTRS_PER_BLOCK(root) - 3) {
		int sret;

		if (*write_lock_level < level + 1) {
			*write_lock_level = level + 1;
			btrfs_release_path(p);
			goto again;
		}

		sret = reada_for_balance(root, p, level);
		if (sret)
			goto again;

		btrfs_set_path_blocking(p);
		sret = split_node(trans, root, p, level);
		btrfs_clear_path_blocking(p, NULL, 0);

		BUG_ON(sret > 0);
		if (sret) {
			ret = sret;
			goto done;
		}
		b = p->nodes[level];
	} else if (ins_len < 0 && btrfs_header_nritems(b) <
		   BTRFS_NODEPTRS_PER_BLOCK(root) / 2) {
		int sret;

		if (*write_lock_level < level + 1) {
			*write_lock_level = level + 1;
			btrfs_release_path(p);
			goto again;
		}

		sret = reada_for_balance(root, p, level);
		if (sret)
			goto again;

		btrfs_set_path_blocking(p);
		sret = balance_level(trans, root, p, level);
		btrfs_clear_path_blocking(p, NULL, 0);

		if (sret) {
			ret = sret;
			goto done;
		}
		b = p->nodes[level];
		if (!b) {
			btrfs_release_path(p);
			goto again;
		}
		BUG_ON(btrfs_header_nritems(b) == 1);
	}
	return 0;

again:
	ret = -EAGAIN;
done:
	return ret;
}

int btrfs_search_slot(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_path *p, int
		      ins_len, int cow)
{
	struct extent_buffer *b;
	int slot;
	int ret;
	int err;
	int level;
	int lowest_unlock = 1;
	int root_lock;
	
	int write_lock_level = 0;
	u8 lowest_level = 0;
	int min_write_lock_level;

	lowest_level = p->lowest_level;
	WARN_ON(lowest_level && ins_len > 0);
	WARN_ON(p->nodes[0] != NULL);

	if (ins_len < 0) {
		lowest_unlock = 2;

		write_lock_level = 2;
	} else if (ins_len > 0) {
		write_lock_level = 1;
	}

	if (!cow)
		write_lock_level = -1;

	if (cow && (p->keep_locks || p->lowest_level))
		write_lock_level = BTRFS_MAX_LEVEL;

	min_write_lock_level = write_lock_level;

again:
	root_lock = BTRFS_READ_LOCK;
	level = 0;
	if (p->search_commit_root) {
		b = root->commit_root;
		extent_buffer_get(b);
		level = btrfs_header_level(b);
		if (!p->skip_locking)
			btrfs_tree_read_lock(b);
	} else {
		if (p->skip_locking) {
			b = btrfs_root_node(root);
			level = btrfs_header_level(b);
		} else {
			b = btrfs_read_lock_root_node(root);
			level = btrfs_header_level(b);
			if (level <= write_lock_level) {
				
				btrfs_tree_read_unlock(b);
				free_extent_buffer(b);
				b = btrfs_lock_root_node(root);
				root_lock = BTRFS_WRITE_LOCK;

				
				level = btrfs_header_level(b);
			}
		}
	}
	p->nodes[level] = b;
	if (!p->skip_locking)
		p->locks[level] = root_lock;

	while (b) {
		level = btrfs_header_level(b);

		if (cow) {
			if (!should_cow_block(trans, root, b))
				goto cow_done;

			btrfs_set_path_blocking(p);

			if (level + 1 > write_lock_level) {
				write_lock_level = level + 1;
				btrfs_release_path(p);
				goto again;
			}

			err = btrfs_cow_block(trans, root, b,
					      p->nodes[level + 1],
					      p->slots[level + 1], &b);
			if (err) {
				ret = err;
				goto done;
			}
		}
cow_done:
		BUG_ON(!cow && ins_len);

		p->nodes[level] = b;
		btrfs_clear_path_blocking(p, NULL, 0);

		if (!cow)
			btrfs_unlock_up_safe(p, level + 1);

		ret = bin_search(b, key, level, &slot);

		if (level != 0) {
			int dec = 0;
			if (ret && slot > 0) {
				dec = 1;
				slot -= 1;
			}
			p->slots[level] = slot;
			err = setup_nodes_for_search(trans, root, p, b, level,
					     ins_len, &write_lock_level);
			if (err == -EAGAIN)
				goto again;
			if (err) {
				ret = err;
				goto done;
			}
			b = p->nodes[level];
			slot = p->slots[level];

			if (slot == 0 && cow &&
			    write_lock_level < level + 1) {
				write_lock_level = level + 1;
				btrfs_release_path(p);
				goto again;
			}

			unlock_up(p, level, lowest_unlock,
				  min_write_lock_level, &write_lock_level);

			if (level == lowest_level) {
				if (dec)
					p->slots[level]++;
				goto done;
			}

			err = read_block_for_search(trans, root, p,
						    &b, level, slot, key);
			if (err == -EAGAIN)
				goto again;
			if (err) {
				ret = err;
				goto done;
			}

			if (!p->skip_locking) {
				level = btrfs_header_level(b);
				if (level <= write_lock_level) {
					err = btrfs_try_tree_write_lock(b);
					if (!err) {
						btrfs_set_path_blocking(p);
						btrfs_tree_lock(b);
						btrfs_clear_path_blocking(p, b,
								  BTRFS_WRITE_LOCK);
					}
					p->locks[level] = BTRFS_WRITE_LOCK;
				} else {
					err = btrfs_try_tree_read_lock(b);
					if (!err) {
						btrfs_set_path_blocking(p);
						btrfs_tree_read_lock(b);
						btrfs_clear_path_blocking(p, b,
								  BTRFS_READ_LOCK);
					}
					p->locks[level] = BTRFS_READ_LOCK;
				}
				p->nodes[level] = b;
			}
		} else {
			p->slots[level] = slot;
			if (ins_len > 0 &&
			    btrfs_leaf_free_space(root, b) < ins_len) {
				if (write_lock_level < 1) {
					write_lock_level = 1;
					btrfs_release_path(p);
					goto again;
				}

				btrfs_set_path_blocking(p);
				err = split_leaf(trans, root, key,
						 p, ins_len, ret == 0);
				btrfs_clear_path_blocking(p, NULL, 0);

				BUG_ON(err > 0);
				if (err) {
					ret = err;
					goto done;
				}
			}
			if (!p->search_for_split)
				unlock_up(p, level, lowest_unlock,
					  min_write_lock_level, &write_lock_level);
			goto done;
		}
	}
	ret = 1;
done:
	if (!p->leave_spinning)
		btrfs_set_path_blocking(p);
	if (ret < 0)
		btrfs_release_path(p);
	return ret;
}

static void fixup_low_keys(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, struct btrfs_path *path,
			   struct btrfs_disk_key *key, int level)
{
	int i;
	struct extent_buffer *t;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		int tslot = path->slots[i];
		if (!path->nodes[i])
			break;
		t = path->nodes[i];
		btrfs_set_node_key(t, key, tslot);
		btrfs_mark_buffer_dirty(path->nodes[i]);
		if (tslot != 0)
			break;
	}
}

void btrfs_set_item_key_safe(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, struct btrfs_path *path,
			     struct btrfs_key *new_key)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *eb;
	int slot;

	eb = path->nodes[0];
	slot = path->slots[0];
	if (slot > 0) {
		btrfs_item_key(eb, &disk_key, slot - 1);
		BUG_ON(comp_keys(&disk_key, new_key) >= 0);
	}
	if (slot < btrfs_header_nritems(eb) - 1) {
		btrfs_item_key(eb, &disk_key, slot + 1);
		BUG_ON(comp_keys(&disk_key, new_key) <= 0);
	}

	btrfs_cpu_key_to_disk(&disk_key, new_key);
	btrfs_set_item_key(eb, &disk_key, slot);
	btrfs_mark_buffer_dirty(eb);
	if (slot == 0)
		fixup_low_keys(trans, root, path, &disk_key, 1);
}

static int push_node_left(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, struct extent_buffer *dst,
			  struct extent_buffer *src, int empty)
{
	int push_items = 0;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	src_nritems = btrfs_header_nritems(src);
	dst_nritems = btrfs_header_nritems(dst);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(root) - dst_nritems;
	WARN_ON(btrfs_header_generation(src) != trans->transid);
	WARN_ON(btrfs_header_generation(dst) != trans->transid);

	if (!empty && src_nritems <= 8)
		return 1;

	if (push_items <= 0)
		return 1;

	if (empty) {
		push_items = min(src_nritems, push_items);
		if (push_items < src_nritems) {
			if (src_nritems - push_items < 8) {
				if (push_items <= 8)
					return 1;
				push_items -= 8;
			}
		}
	} else
		push_items = min(src_nritems - 8, push_items);

	copy_extent_buffer(dst, src,
			   btrfs_node_key_ptr_offset(dst_nritems),
			   btrfs_node_key_ptr_offset(0),
			   push_items * sizeof(struct btrfs_key_ptr));

	if (push_items < src_nritems) {
		memmove_extent_buffer(src, btrfs_node_key_ptr_offset(0),
				      btrfs_node_key_ptr_offset(push_items),
				      (src_nritems - push_items) *
				      sizeof(struct btrfs_key_ptr));
	}
	btrfs_set_header_nritems(src, src_nritems - push_items);
	btrfs_set_header_nritems(dst, dst_nritems + push_items);
	btrfs_mark_buffer_dirty(src);
	btrfs_mark_buffer_dirty(dst);

	return ret;
}

static int balance_node_right(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct extent_buffer *dst,
			      struct extent_buffer *src)
{
	int push_items = 0;
	int max_push;
	int src_nritems;
	int dst_nritems;
	int ret = 0;

	WARN_ON(btrfs_header_generation(src) != trans->transid);
	WARN_ON(btrfs_header_generation(dst) != trans->transid);

	src_nritems = btrfs_header_nritems(src);
	dst_nritems = btrfs_header_nritems(dst);
	push_items = BTRFS_NODEPTRS_PER_BLOCK(root) - dst_nritems;
	if (push_items <= 0)
		return 1;

	if (src_nritems < 4)
		return 1;

	max_push = src_nritems / 2 + 1;
	
	if (max_push >= src_nritems)
		return 1;

	if (max_push < push_items)
		push_items = max_push;

	memmove_extent_buffer(dst, btrfs_node_key_ptr_offset(push_items),
				      btrfs_node_key_ptr_offset(0),
				      (dst_nritems) *
				      sizeof(struct btrfs_key_ptr));

	copy_extent_buffer(dst, src,
			   btrfs_node_key_ptr_offset(0),
			   btrfs_node_key_ptr_offset(src_nritems - push_items),
			   push_items * sizeof(struct btrfs_key_ptr));

	btrfs_set_header_nritems(src, src_nritems - push_items);
	btrfs_set_header_nritems(dst, dst_nritems + push_items);

	btrfs_mark_buffer_dirty(src);
	btrfs_mark_buffer_dirty(dst);

	return ret;
}

static noinline int insert_new_root(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct btrfs_path *path, int level)
{
	u64 lower_gen;
	struct extent_buffer *lower;
	struct extent_buffer *c;
	struct extent_buffer *old;
	struct btrfs_disk_key lower_key;

	BUG_ON(path->nodes[level]);
	BUG_ON(path->nodes[level-1] != root->node);

	lower = path->nodes[level-1];
	if (level == 1)
		btrfs_item_key(lower, &lower_key, 0);
	else
		btrfs_node_key(lower, &lower_key, 0);

	c = btrfs_alloc_free_block(trans, root, root->nodesize, 0,
				   root->root_key.objectid, &lower_key,
				   level, root->node->start, 0, 0);
	if (IS_ERR(c))
		return PTR_ERR(c);

	root_add_used(root, root->nodesize);

	memset_extent_buffer(c, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_nritems(c, 1);
	btrfs_set_header_level(c, level);
	btrfs_set_header_bytenr(c, c->start);
	btrfs_set_header_generation(c, trans->transid);
	btrfs_set_header_backref_rev(c, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(c, root->root_key.objectid);

	write_extent_buffer(c, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(c),
			    BTRFS_FSID_SIZE);

	write_extent_buffer(c, root->fs_info->chunk_tree_uuid,
			    (unsigned long)btrfs_header_chunk_tree_uuid(c),
			    BTRFS_UUID_SIZE);

	btrfs_set_node_key(c, &lower_key, 0);
	btrfs_set_node_blockptr(c, 0, lower->start);
	lower_gen = btrfs_header_generation(lower);
	WARN_ON(lower_gen != trans->transid);

	btrfs_set_node_ptr_generation(c, 0, lower_gen);

	btrfs_mark_buffer_dirty(c);

	old = root->node;
	rcu_assign_pointer(root->node, c);

	
	free_extent_buffer(old);

	add_root_to_dirty_list(root);
	extent_buffer_get(c);
	path->nodes[level] = c;
	path->locks[level] = BTRFS_WRITE_LOCK;
	path->slots[level] = 0;
	return 0;
}

static void insert_ptr(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct btrfs_path *path,
		       struct btrfs_disk_key *key, u64 bytenr,
		       int slot, int level)
{
	struct extent_buffer *lower;
	int nritems;

	BUG_ON(!path->nodes[level]);
	btrfs_assert_tree_locked(path->nodes[level]);
	lower = path->nodes[level];
	nritems = btrfs_header_nritems(lower);
	BUG_ON(slot > nritems);
	BUG_ON(nritems == BTRFS_NODEPTRS_PER_BLOCK(root));
	if (slot != nritems) {
		memmove_extent_buffer(lower,
			      btrfs_node_key_ptr_offset(slot + 1),
			      btrfs_node_key_ptr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_key_ptr));
	}
	btrfs_set_node_key(lower, key, slot);
	btrfs_set_node_blockptr(lower, slot, bytenr);
	WARN_ON(trans->transid == 0);
	btrfs_set_node_ptr_generation(lower, slot, trans->transid);
	btrfs_set_header_nritems(lower, nritems + 1);
	btrfs_mark_buffer_dirty(lower);
}

static noinline int split_node(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_path *path, int level)
{
	struct extent_buffer *c;
	struct extent_buffer *split;
	struct btrfs_disk_key disk_key;
	int mid;
	int ret;
	u32 c_nritems;

	c = path->nodes[level];
	WARN_ON(btrfs_header_generation(c) != trans->transid);
	if (c == root->node) {
		
		ret = insert_new_root(trans, root, path, level + 1);
		if (ret)
			return ret;
	} else {
		ret = push_nodes_for_insert(trans, root, path, level);
		c = path->nodes[level];
		if (!ret && btrfs_header_nritems(c) <
		    BTRFS_NODEPTRS_PER_BLOCK(root) - 3)
			return 0;
		if (ret < 0)
			return ret;
	}

	c_nritems = btrfs_header_nritems(c);
	mid = (c_nritems + 1) / 2;
	btrfs_node_key(c, &disk_key, mid);

	split = btrfs_alloc_free_block(trans, root, root->nodesize, 0,
					root->root_key.objectid,
					&disk_key, level, c->start, 0, 0);
	if (IS_ERR(split))
		return PTR_ERR(split);

	root_add_used(root, root->nodesize);

	memset_extent_buffer(split, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_level(split, btrfs_header_level(c));
	btrfs_set_header_bytenr(split, split->start);
	btrfs_set_header_generation(split, trans->transid);
	btrfs_set_header_backref_rev(split, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(split, root->root_key.objectid);
	write_extent_buffer(split, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(split),
			    BTRFS_FSID_SIZE);
	write_extent_buffer(split, root->fs_info->chunk_tree_uuid,
			    (unsigned long)btrfs_header_chunk_tree_uuid(split),
			    BTRFS_UUID_SIZE);


	copy_extent_buffer(split, c,
			   btrfs_node_key_ptr_offset(0),
			   btrfs_node_key_ptr_offset(mid),
			   (c_nritems - mid) * sizeof(struct btrfs_key_ptr));
	btrfs_set_header_nritems(split, c_nritems - mid);
	btrfs_set_header_nritems(c, mid);
	ret = 0;

	btrfs_mark_buffer_dirty(c);
	btrfs_mark_buffer_dirty(split);

	insert_ptr(trans, root, path, &disk_key, split->start,
		   path->slots[level + 1] + 1, level + 1);

	if (path->slots[level] >= mid) {
		path->slots[level] -= mid;
		btrfs_tree_unlock(c);
		free_extent_buffer(c);
		path->nodes[level] = split;
		path->slots[level + 1] += 1;
	} else {
		btrfs_tree_unlock(split);
		free_extent_buffer(split);
	}
	return ret;
}

static int leaf_space_used(struct extent_buffer *l, int start, int nr)
{
	int data_len;
	int nritems = btrfs_header_nritems(l);
	int end = min(nritems, start + nr) - 1;

	if (!nr)
		return 0;
	data_len = btrfs_item_end_nr(l, start);
	data_len = data_len - btrfs_item_offset_nr(l, end);
	data_len += sizeof(struct btrfs_item) * nr;
	WARN_ON(data_len < 0);
	return data_len;
}

noinline int btrfs_leaf_free_space(struct btrfs_root *root,
				   struct extent_buffer *leaf)
{
	int nritems = btrfs_header_nritems(leaf);
	int ret;
	ret = BTRFS_LEAF_DATA_SIZE(root) - leaf_space_used(leaf, 0, nritems);
	if (ret < 0) {
		printk(KERN_CRIT "leaf free space ret %d, leaf data size %lu, "
		       "used %d nritems %d\n",
		       ret, (unsigned long) BTRFS_LEAF_DATA_SIZE(root),
		       leaf_space_used(leaf, 0, nritems), nritems);
	}
	return ret;
}

static noinline int __push_leaf_right(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      int data_size, int empty,
				      struct extent_buffer *right,
				      int free_space, u32 left_nritems,
				      u32 min_slot)
{
	struct extent_buffer *left = path->nodes[0];
	struct extent_buffer *upper = path->nodes[1];
	struct btrfs_map_token token;
	struct btrfs_disk_key disk_key;
	int slot;
	u32 i;
	int push_space = 0;
	int push_items = 0;
	struct btrfs_item *item;
	u32 nr;
	u32 right_nritems;
	u32 data_end;
	u32 this_item_size;

	btrfs_init_map_token(&token);

	if (empty)
		nr = 0;
	else
		nr = max_t(u32, 1, min_slot);

	if (path->slots[0] >= left_nritems)
		push_space += data_size;

	slot = path->slots[1];
	i = left_nritems - 1;
	while (i >= nr) {
		item = btrfs_item_nr(left, i);

		if (!empty && push_items > 0) {
			if (path->slots[0] > i)
				break;
			if (path->slots[0] == i) {
				int space = btrfs_leaf_free_space(root, left);
				if (space + push_space * 2 > free_space)
					break;
			}
		}

		if (path->slots[0] == i)
			push_space += data_size;

		this_item_size = btrfs_item_size(left, item);
		if (this_item_size + sizeof(*item) + push_space > free_space)
			break;

		push_items++;
		push_space += this_item_size + sizeof(*item);
		if (i == 0)
			break;
		i--;
	}

	if (push_items == 0)
		goto out_unlock;

	if (!empty && push_items == left_nritems)
		WARN_ON(1);

	
	right_nritems = btrfs_header_nritems(right);

	push_space = btrfs_item_end_nr(left, left_nritems - push_items);
	push_space -= leaf_data_end(root, left);

	
	data_end = leaf_data_end(root, right);
	memmove_extent_buffer(right,
			      btrfs_leaf_data(right) + data_end - push_space,
			      btrfs_leaf_data(right) + data_end,
			      BTRFS_LEAF_DATA_SIZE(root) - data_end);

	
	copy_extent_buffer(right, left, btrfs_leaf_data(right) +
		     BTRFS_LEAF_DATA_SIZE(root) - push_space,
		     btrfs_leaf_data(left) + leaf_data_end(root, left),
		     push_space);

	memmove_extent_buffer(right, btrfs_item_nr_offset(push_items),
			      btrfs_item_nr_offset(0),
			      right_nritems * sizeof(struct btrfs_item));

	
	copy_extent_buffer(right, left, btrfs_item_nr_offset(0),
		   btrfs_item_nr_offset(left_nritems - push_items),
		   push_items * sizeof(struct btrfs_item));

	
	right_nritems += push_items;
	btrfs_set_header_nritems(right, right_nritems);
	push_space = BTRFS_LEAF_DATA_SIZE(root);
	for (i = 0; i < right_nritems; i++) {
		item = btrfs_item_nr(right, i);
		push_space -= btrfs_token_item_size(right, item, &token);
		btrfs_set_token_item_offset(right, item, push_space, &token);
	}

	left_nritems -= push_items;
	btrfs_set_header_nritems(left, left_nritems);

	if (left_nritems)
		btrfs_mark_buffer_dirty(left);
	else
		clean_tree_block(trans, root, left);

	btrfs_mark_buffer_dirty(right);

	btrfs_item_key(right, &disk_key, 0);
	btrfs_set_node_key(upper, &disk_key, slot + 1);
	btrfs_mark_buffer_dirty(upper);

	
	if (path->slots[0] >= left_nritems) {
		path->slots[0] -= left_nritems;
		if (btrfs_header_nritems(path->nodes[0]) == 0)
			clean_tree_block(trans, root, path->nodes[0]);
		btrfs_tree_unlock(path->nodes[0]);
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = right;
		path->slots[1] += 1;
	} else {
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}
	return 0;

out_unlock:
	btrfs_tree_unlock(right);
	free_extent_buffer(right);
	return 1;
}

static int push_leaf_right(struct btrfs_trans_handle *trans, struct btrfs_root
			   *root, struct btrfs_path *path,
			   int min_data_size, int data_size,
			   int empty, u32 min_slot)
{
	struct extent_buffer *left = path->nodes[0];
	struct extent_buffer *right;
	struct extent_buffer *upper;
	int slot;
	int free_space;
	u32 left_nritems;
	int ret;

	if (!path->nodes[1])
		return 1;

	slot = path->slots[1];
	upper = path->nodes[1];
	if (slot >= btrfs_header_nritems(upper) - 1)
		return 1;

	btrfs_assert_tree_locked(path->nodes[1]);

	right = read_node_slot(root, upper, slot + 1);
	if (right == NULL)
		return 1;

	btrfs_tree_lock(right);
	btrfs_set_lock_blocking(right);

	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size)
		goto out_unlock;

	
	ret = btrfs_cow_block(trans, root, right, upper,
			      slot + 1, &right);
	if (ret)
		goto out_unlock;

	free_space = btrfs_leaf_free_space(root, right);
	if (free_space < data_size)
		goto out_unlock;

	left_nritems = btrfs_header_nritems(left);
	if (left_nritems == 0)
		goto out_unlock;

	return __push_leaf_right(trans, root, path, min_data_size, empty,
				right, free_space, left_nritems, min_slot);
out_unlock:
	btrfs_tree_unlock(right);
	free_extent_buffer(right);
	return 1;
}

static noinline int __push_leaf_left(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct btrfs_path *path, int data_size,
				     int empty, struct extent_buffer *left,
				     int free_space, u32 right_nritems,
				     u32 max_slot)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *right = path->nodes[0];
	int i;
	int push_space = 0;
	int push_items = 0;
	struct btrfs_item *item;
	u32 old_left_nritems;
	u32 nr;
	int ret = 0;
	u32 this_item_size;
	u32 old_left_item_size;
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	if (empty)
		nr = min(right_nritems, max_slot);
	else
		nr = min(right_nritems - 1, max_slot);

	for (i = 0; i < nr; i++) {
		item = btrfs_item_nr(right, i);

		if (!empty && push_items > 0) {
			if (path->slots[0] < i)
				break;
			if (path->slots[0] == i) {
				int space = btrfs_leaf_free_space(root, right);
				if (space + push_space * 2 > free_space)
					break;
			}
		}

		if (path->slots[0] == i)
			push_space += data_size;

		this_item_size = btrfs_item_size(right, item);
		if (this_item_size + sizeof(*item) + push_space > free_space)
			break;

		push_items++;
		push_space += this_item_size + sizeof(*item);
	}

	if (push_items == 0) {
		ret = 1;
		goto out;
	}
	if (!empty && push_items == btrfs_header_nritems(right))
		WARN_ON(1);

	
	copy_extent_buffer(left, right,
			   btrfs_item_nr_offset(btrfs_header_nritems(left)),
			   btrfs_item_nr_offset(0),
			   push_items * sizeof(struct btrfs_item));

	push_space = BTRFS_LEAF_DATA_SIZE(root) -
		     btrfs_item_offset_nr(right, push_items - 1);

	copy_extent_buffer(left, right, btrfs_leaf_data(left) +
		     leaf_data_end(root, left) - push_space,
		     btrfs_leaf_data(right) +
		     btrfs_item_offset_nr(right, push_items - 1),
		     push_space);
	old_left_nritems = btrfs_header_nritems(left);
	BUG_ON(old_left_nritems <= 0);

	old_left_item_size = btrfs_item_offset_nr(left, old_left_nritems - 1);
	for (i = old_left_nritems; i < old_left_nritems + push_items; i++) {
		u32 ioff;

		item = btrfs_item_nr(left, i);

		ioff = btrfs_token_item_offset(left, item, &token);
		btrfs_set_token_item_offset(left, item,
		      ioff - (BTRFS_LEAF_DATA_SIZE(root) - old_left_item_size),
		      &token);
	}
	btrfs_set_header_nritems(left, old_left_nritems + push_items);

	
	if (push_items > right_nritems) {
		printk(KERN_CRIT "push items %d nr %u\n", push_items,
		       right_nritems);
		WARN_ON(1);
	}

	if (push_items < right_nritems) {
		push_space = btrfs_item_offset_nr(right, push_items - 1) -
						  leaf_data_end(root, right);
		memmove_extent_buffer(right, btrfs_leaf_data(right) +
				      BTRFS_LEAF_DATA_SIZE(root) - push_space,
				      btrfs_leaf_data(right) +
				      leaf_data_end(root, right), push_space);

		memmove_extent_buffer(right, btrfs_item_nr_offset(0),
			      btrfs_item_nr_offset(push_items),
			     (btrfs_header_nritems(right) - push_items) *
			     sizeof(struct btrfs_item));
	}
	right_nritems -= push_items;
	btrfs_set_header_nritems(right, right_nritems);
	push_space = BTRFS_LEAF_DATA_SIZE(root);
	for (i = 0; i < right_nritems; i++) {
		item = btrfs_item_nr(right, i);

		push_space = push_space - btrfs_token_item_size(right,
								item, &token);
		btrfs_set_token_item_offset(right, item, push_space, &token);
	}

	btrfs_mark_buffer_dirty(left);
	if (right_nritems)
		btrfs_mark_buffer_dirty(right);
	else
		clean_tree_block(trans, root, right);

	btrfs_item_key(right, &disk_key, 0);
	fixup_low_keys(trans, root, path, &disk_key, 1);

	
	if (path->slots[0] < push_items) {
		path->slots[0] += old_left_nritems;
		btrfs_tree_unlock(path->nodes[0]);
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = left;
		path->slots[1] -= 1;
	} else {
		btrfs_tree_unlock(left);
		free_extent_buffer(left);
		path->slots[0] -= push_items;
	}
	BUG_ON(path->slots[0] < 0);
	return ret;
out:
	btrfs_tree_unlock(left);
	free_extent_buffer(left);
	return ret;
}

static int push_leaf_left(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct btrfs_path *path, int min_data_size,
			  int data_size, int empty, u32 max_slot)
{
	struct extent_buffer *right = path->nodes[0];
	struct extent_buffer *left;
	int slot;
	int free_space;
	u32 right_nritems;
	int ret = 0;

	slot = path->slots[1];
	if (slot == 0)
		return 1;
	if (!path->nodes[1])
		return 1;

	right_nritems = btrfs_header_nritems(right);
	if (right_nritems == 0)
		return 1;

	btrfs_assert_tree_locked(path->nodes[1]);

	left = read_node_slot(root, path->nodes[1], slot - 1);
	if (left == NULL)
		return 1;

	btrfs_tree_lock(left);
	btrfs_set_lock_blocking(left);

	free_space = btrfs_leaf_free_space(root, left);
	if (free_space < data_size) {
		ret = 1;
		goto out;
	}

	
	ret = btrfs_cow_block(trans, root, left,
			      path->nodes[1], slot - 1, &left);
	if (ret) {
		
		if (ret == -ENOSPC)
			ret = 1;
		goto out;
	}

	free_space = btrfs_leaf_free_space(root, left);
	if (free_space < data_size) {
		ret = 1;
		goto out;
	}

	return __push_leaf_left(trans, root, path, min_data_size,
			       empty, left, free_space, right_nritems,
			       max_slot);
out:
	btrfs_tree_unlock(left);
	free_extent_buffer(left);
	return ret;
}

static noinline void copy_for_split(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root,
				    struct btrfs_path *path,
				    struct extent_buffer *l,
				    struct extent_buffer *right,
				    int slot, int mid, int nritems)
{
	int data_copy_size;
	int rt_data_off;
	int i;
	struct btrfs_disk_key disk_key;
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	nritems = nritems - mid;
	btrfs_set_header_nritems(right, nritems);
	data_copy_size = btrfs_item_end_nr(l, mid) - leaf_data_end(root, l);

	copy_extent_buffer(right, l, btrfs_item_nr_offset(0),
			   btrfs_item_nr_offset(mid),
			   nritems * sizeof(struct btrfs_item));

	copy_extent_buffer(right, l,
		     btrfs_leaf_data(right) + BTRFS_LEAF_DATA_SIZE(root) -
		     data_copy_size, btrfs_leaf_data(l) +
		     leaf_data_end(root, l), data_copy_size);

	rt_data_off = BTRFS_LEAF_DATA_SIZE(root) -
		      btrfs_item_end_nr(l, mid);

	for (i = 0; i < nritems; i++) {
		struct btrfs_item *item = btrfs_item_nr(right, i);
		u32 ioff;

		ioff = btrfs_token_item_offset(right, item, &token);
		btrfs_set_token_item_offset(right, item,
					    ioff + rt_data_off, &token);
	}

	btrfs_set_header_nritems(l, mid);
	btrfs_item_key(right, &disk_key, 0);
	insert_ptr(trans, root, path, &disk_key, right->start,
		   path->slots[1] + 1, 1);

	btrfs_mark_buffer_dirty(right);
	btrfs_mark_buffer_dirty(l);
	BUG_ON(path->slots[0] != slot);

	if (mid <= slot) {
		btrfs_tree_unlock(path->nodes[0]);
		free_extent_buffer(path->nodes[0]);
		path->nodes[0] = right;
		path->slots[0] -= mid;
		path->slots[1] += 1;
	} else {
		btrfs_tree_unlock(right);
		free_extent_buffer(right);
	}

	BUG_ON(path->slots[0] < 0);
}

static noinline int push_for_double_split(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  int data_size)
{
	int ret;
	int progress = 0;
	int slot;
	u32 nritems;

	slot = path->slots[0];

	ret = push_leaf_right(trans, root, path, 1, data_size, 0, slot);
	if (ret < 0)
		return ret;

	if (ret == 0)
		progress++;

	nritems = btrfs_header_nritems(path->nodes[0]);
	if (path->slots[0] == 0 || path->slots[0] == nritems)
		return 0;

	if (btrfs_leaf_free_space(root, path->nodes[0]) >= data_size)
		return 0;

	
	slot = path->slots[0];
	ret = push_leaf_left(trans, root, path, 1, data_size, 0, slot);
	if (ret < 0)
		return ret;

	if (ret == 0)
		progress++;

	if (progress)
		return 0;
	return 1;
}

static noinline int split_leaf(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_key *ins_key,
			       struct btrfs_path *path, int data_size,
			       int extend)
{
	struct btrfs_disk_key disk_key;
	struct extent_buffer *l;
	u32 nritems;
	int mid;
	int slot;
	struct extent_buffer *right;
	int ret = 0;
	int wret;
	int split;
	int num_doubles = 0;
	int tried_avoid_double = 0;

	l = path->nodes[0];
	slot = path->slots[0];
	if (extend && data_size + btrfs_item_size_nr(l, slot) +
	    sizeof(struct btrfs_item) > BTRFS_LEAF_DATA_SIZE(root))
		return -EOVERFLOW;

	
	if (data_size) {
		wret = push_leaf_right(trans, root, path, data_size,
				       data_size, 0, 0);
		if (wret < 0)
			return wret;
		if (wret) {
			wret = push_leaf_left(trans, root, path, data_size,
					      data_size, 0, (u32)-1);
			if (wret < 0)
				return wret;
		}
		l = path->nodes[0];

		
		if (btrfs_leaf_free_space(root, l) >= data_size)
			return 0;
	}

	if (!path->nodes[1]) {
		ret = insert_new_root(trans, root, path, 1);
		if (ret)
			return ret;
	}
again:
	split = 1;
	l = path->nodes[0];
	slot = path->slots[0];
	nritems = btrfs_header_nritems(l);
	mid = (nritems + 1) / 2;

	if (mid <= slot) {
		if (nritems == 1 ||
		    leaf_space_used(l, mid, nritems - mid) + data_size >
			BTRFS_LEAF_DATA_SIZE(root)) {
			if (slot >= nritems) {
				split = 0;
			} else {
				mid = slot;
				if (mid != nritems &&
				    leaf_space_used(l, mid, nritems - mid) +
				    data_size > BTRFS_LEAF_DATA_SIZE(root)) {
					if (data_size && !tried_avoid_double)
						goto push_for_double;
					split = 2;
				}
			}
		}
	} else {
		if (leaf_space_used(l, 0, mid) + data_size >
			BTRFS_LEAF_DATA_SIZE(root)) {
			if (!extend && data_size && slot == 0) {
				split = 0;
			} else if ((extend || !data_size) && slot == 0) {
				mid = 1;
			} else {
				mid = slot;
				if (mid != nritems &&
				    leaf_space_used(l, mid, nritems - mid) +
				    data_size > BTRFS_LEAF_DATA_SIZE(root)) {
					if (data_size && !tried_avoid_double)
						goto push_for_double;
					split = 2 ;
				}
			}
		}
	}

	if (split == 0)
		btrfs_cpu_key_to_disk(&disk_key, ins_key);
	else
		btrfs_item_key(l, &disk_key, mid);

	right = btrfs_alloc_free_block(trans, root, root->leafsize, 0,
					root->root_key.objectid,
					&disk_key, 0, l->start, 0, 0);
	if (IS_ERR(right))
		return PTR_ERR(right);

	root_add_used(root, root->leafsize);

	memset_extent_buffer(right, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_bytenr(right, right->start);
	btrfs_set_header_generation(right, trans->transid);
	btrfs_set_header_backref_rev(right, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(right, root->root_key.objectid);
	btrfs_set_header_level(right, 0);
	write_extent_buffer(right, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(right),
			    BTRFS_FSID_SIZE);

	write_extent_buffer(right, root->fs_info->chunk_tree_uuid,
			    (unsigned long)btrfs_header_chunk_tree_uuid(right),
			    BTRFS_UUID_SIZE);

	if (split == 0) {
		if (mid <= slot) {
			btrfs_set_header_nritems(right, 0);
			insert_ptr(trans, root, path, &disk_key, right->start,
				   path->slots[1] + 1, 1);
			btrfs_tree_unlock(path->nodes[0]);
			free_extent_buffer(path->nodes[0]);
			path->nodes[0] = right;
			path->slots[0] = 0;
			path->slots[1] += 1;
		} else {
			btrfs_set_header_nritems(right, 0);
			insert_ptr(trans, root, path, &disk_key, right->start,
					  path->slots[1], 1);
			btrfs_tree_unlock(path->nodes[0]);
			free_extent_buffer(path->nodes[0]);
			path->nodes[0] = right;
			path->slots[0] = 0;
			if (path->slots[1] == 0)
				fixup_low_keys(trans, root, path,
					       &disk_key, 1);
		}
		btrfs_mark_buffer_dirty(right);
		return ret;
	}

	copy_for_split(trans, root, path, l, right, slot, mid, nritems);

	if (split == 2) {
		BUG_ON(num_doubles != 0);
		num_doubles++;
		goto again;
	}

	return 0;

push_for_double:
	push_for_double_split(trans, root, path, data_size);
	tried_avoid_double = 1;
	if (btrfs_leaf_free_space(root, path->nodes[0]) >= data_size)
		return 0;
	goto again;
}

static noinline int setup_leaf_for_split(struct btrfs_trans_handle *trans,
					 struct btrfs_root *root,
					 struct btrfs_path *path, int ins_len)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *fi;
	u64 extent_len = 0;
	u32 item_size;
	int ret;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

	BUG_ON(key.type != BTRFS_EXTENT_DATA_KEY &&
	       key.type != BTRFS_EXTENT_CSUM_KEY);

	if (btrfs_leaf_free_space(root, leaf) >= ins_len)
		return 0;

	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	if (key.type == BTRFS_EXTENT_DATA_KEY) {
		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		extent_len = btrfs_file_extent_num_bytes(leaf, fi);
	}
	btrfs_release_path(path);

	path->keep_locks = 1;
	path->search_for_split = 1;
	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	path->search_for_split = 0;
	if (ret < 0)
		goto err;

	ret = -EAGAIN;
	leaf = path->nodes[0];
	
	if (ret > 0 || item_size != btrfs_item_size_nr(leaf, path->slots[0]))
		goto err;

	
	if (btrfs_leaf_free_space(root, path->nodes[0]) >= ins_len)
		goto err;

	if (key.type == BTRFS_EXTENT_DATA_KEY) {
		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		if (extent_len != btrfs_file_extent_num_bytes(leaf, fi))
			goto err;
	}

	btrfs_set_path_blocking(path);
	ret = split_leaf(trans, root, &key, path, ins_len, 1);
	if (ret)
		goto err;

	path->keep_locks = 0;
	btrfs_unlock_up_safe(path, 1);
	return 0;
err:
	path->keep_locks = 0;
	return ret;
}

static noinline int split_item(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_path *path,
			       struct btrfs_key *new_key,
			       unsigned long split_offset)
{
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	struct btrfs_item *new_item;
	int slot;
	char *buf;
	u32 nritems;
	u32 item_size;
	u32 orig_offset;
	struct btrfs_disk_key disk_key;

	leaf = path->nodes[0];
	BUG_ON(btrfs_leaf_free_space(root, leaf) < sizeof(struct btrfs_item));

	btrfs_set_path_blocking(path);

	item = btrfs_item_nr(leaf, path->slots[0]);
	orig_offset = btrfs_item_offset(leaf, item);
	item_size = btrfs_item_size(leaf, item);

	buf = kmalloc(item_size, GFP_NOFS);
	if (!buf)
		return -ENOMEM;

	read_extent_buffer(leaf, buf, btrfs_item_ptr_offset(leaf,
			    path->slots[0]), item_size);

	slot = path->slots[0] + 1;
	nritems = btrfs_header_nritems(leaf);
	if (slot != nritems) {
		
		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot + 1),
				btrfs_item_nr_offset(slot),
				(nritems - slot) * sizeof(struct btrfs_item));
	}

	btrfs_cpu_key_to_disk(&disk_key, new_key);
	btrfs_set_item_key(leaf, &disk_key, slot);

	new_item = btrfs_item_nr(leaf, slot);

	btrfs_set_item_offset(leaf, new_item, orig_offset);
	btrfs_set_item_size(leaf, new_item, item_size - split_offset);

	btrfs_set_item_offset(leaf, item,
			      orig_offset + item_size - split_offset);
	btrfs_set_item_size(leaf, item, split_offset);

	btrfs_set_header_nritems(leaf, nritems + 1);

	
	write_extent_buffer(leaf, buf,
			    btrfs_item_ptr_offset(leaf, path->slots[0]),
			    split_offset);

	
	write_extent_buffer(leaf, buf + split_offset,
			    btrfs_item_ptr_offset(leaf, slot),
			    item_size - split_offset);
	btrfs_mark_buffer_dirty(leaf);

	BUG_ON(btrfs_leaf_free_space(root, leaf) < 0);
	kfree(buf);
	return 0;
}

int btrfs_split_item(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root,
		     struct btrfs_path *path,
		     struct btrfs_key *new_key,
		     unsigned long split_offset)
{
	int ret;
	ret = setup_leaf_for_split(trans, root, path,
				   sizeof(struct btrfs_item));
	if (ret)
		return ret;

	ret = split_item(trans, root, path, new_key, split_offset);
	return ret;
}

int btrfs_duplicate_item(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 struct btrfs_path *path,
			 struct btrfs_key *new_key)
{
	struct extent_buffer *leaf;
	int ret;
	u32 item_size;

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	ret = setup_leaf_for_split(trans, root, path,
				   item_size + sizeof(struct btrfs_item));
	if (ret)
		return ret;

	path->slots[0]++;
	setup_items_for_insert(trans, root, path, new_key, &item_size,
			       item_size, item_size +
			       sizeof(struct btrfs_item), 1);
	leaf = path->nodes[0];
	memcpy_extent_buffer(leaf,
			     btrfs_item_ptr_offset(leaf, path->slots[0]),
			     btrfs_item_ptr_offset(leaf, path->slots[0] - 1),
			     item_size);
	return 0;
}

void btrfs_truncate_item(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 struct btrfs_path *path,
			 u32 new_size, int from_end)
{
	int slot;
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	u32 nritems;
	unsigned int data_end;
	unsigned int old_data_start;
	unsigned int old_size;
	unsigned int size_diff;
	int i;
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	leaf = path->nodes[0];
	slot = path->slots[0];

	old_size = btrfs_item_size_nr(leaf, slot);
	if (old_size == new_size)
		return;

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	old_data_start = btrfs_item_offset_nr(leaf, slot);

	size_diff = old_size - new_size;

	BUG_ON(slot < 0);
	BUG_ON(slot >= nritems);

	
	for (i = slot; i < nritems; i++) {
		u32 ioff;
		item = btrfs_item_nr(leaf, i);

		ioff = btrfs_token_item_offset(leaf, item, &token);
		btrfs_set_token_item_offset(leaf, item,
					    ioff + size_diff, &token);
	}

	
	if (from_end) {
		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + size_diff, btrfs_leaf_data(leaf) +
			      data_end, old_data_start + new_size - data_end);
	} else {
		struct btrfs_disk_key disk_key;
		u64 offset;

		btrfs_item_key(leaf, &disk_key, slot);

		if (btrfs_disk_key_type(&disk_key) == BTRFS_EXTENT_DATA_KEY) {
			unsigned long ptr;
			struct btrfs_file_extent_item *fi;

			fi = btrfs_item_ptr(leaf, slot,
					    struct btrfs_file_extent_item);
			fi = (struct btrfs_file_extent_item *)(
			     (unsigned long)fi - size_diff);

			if (btrfs_file_extent_type(leaf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE) {
				ptr = btrfs_item_ptr_offset(leaf, slot);
				memmove_extent_buffer(leaf, ptr,
				      (unsigned long)fi,
				      offsetof(struct btrfs_file_extent_item,
						 disk_bytenr));
			}
		}

		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + size_diff, btrfs_leaf_data(leaf) +
			      data_end, old_data_start - data_end);

		offset = btrfs_disk_key_offset(&disk_key);
		btrfs_set_disk_key_offset(&disk_key, offset + size_diff);
		btrfs_set_item_key(leaf, &disk_key, slot);
		if (slot == 0)
			fixup_low_keys(trans, root, path, &disk_key, 1);
	}

	item = btrfs_item_nr(leaf, slot);
	btrfs_set_item_size(leaf, item, new_size);
	btrfs_mark_buffer_dirty(leaf);

	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
}

void btrfs_extend_item(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct btrfs_path *path,
		       u32 data_size)
{
	int slot;
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	u32 nritems;
	unsigned int data_end;
	unsigned int old_data;
	unsigned int old_size;
	int i;
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	leaf = path->nodes[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) < data_size) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
	slot = path->slots[0];
	old_data = btrfs_item_end_nr(leaf, slot);

	BUG_ON(slot < 0);
	if (slot >= nritems) {
		btrfs_print_leaf(root, leaf);
		printk(KERN_CRIT "slot %d too large, nritems %d\n",
		       slot, nritems);
		BUG_ON(1);
	}

	
	for (i = slot; i < nritems; i++) {
		u32 ioff;
		item = btrfs_item_nr(leaf, i);

		ioff = btrfs_token_item_offset(leaf, item, &token);
		btrfs_set_token_item_offset(leaf, item,
					    ioff - data_size, &token);
	}

	
	memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
		      data_end - data_size, btrfs_leaf_data(leaf) +
		      data_end, old_data - data_end);

	data_end = old_data;
	old_size = btrfs_item_size_nr(leaf, slot);
	item = btrfs_item_nr(leaf, slot);
	btrfs_set_item_size(leaf, item, old_size + data_size);
	btrfs_mark_buffer_dirty(leaf);

	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
}

int btrfs_insert_some_items(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path,
			    struct btrfs_key *cpu_key, u32 *data_size,
			    int nr)
{
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	int ret = 0;
	int slot;
	int i;
	u32 nritems;
	u32 total_data = 0;
	u32 total_size = 0;
	unsigned int data_end;
	struct btrfs_disk_key disk_key;
	struct btrfs_key found_key;
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	for (i = 0; i < nr; i++) {
		if (total_size + data_size[i] + sizeof(struct btrfs_item) >
		    BTRFS_LEAF_DATA_SIZE(root)) {
			break;
			nr = i;
		}
		total_data += data_size[i];
		total_size += data_size[i] + sizeof(struct btrfs_item);
	}
	BUG_ON(nr == 0);

	ret = btrfs_search_slot(trans, root, cpu_key, path, total_size, 1);
	if (ret == 0)
		return -EEXIST;
	if (ret < 0)
		goto out;

	leaf = path->nodes[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) < total_size) {
		for (i = nr; i >= 0; i--) {
			total_data -= data_size[i];
			total_size -= data_size[i] + sizeof(struct btrfs_item);
			if (total_size < btrfs_leaf_free_space(root, leaf))
				break;
		}
		nr = i;
	}

	slot = path->slots[0];
	BUG_ON(slot < 0);

	if (slot != nritems) {
		unsigned int old_data = btrfs_item_end_nr(leaf, slot);

		item = btrfs_item_nr(leaf, slot);
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		
		total_data = data_size[0];
		for (i = 1; i < nr; i++) {
			if (btrfs_comp_cpu_keys(&found_key, cpu_key + i) <= 0)
				break;
			total_data += data_size[i];
		}
		nr = i;

		if (old_data < data_end) {
			btrfs_print_leaf(root, leaf);
			printk(KERN_CRIT "slot %d old_data %d data_end %d\n",
			       slot, old_data, data_end);
			BUG_ON(1);
		}
		
		for (i = slot; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(leaf, i);
			ioff = btrfs_token_item_offset(leaf, item, &token);
			btrfs_set_token_item_offset(leaf, item,
						    ioff - total_data, &token);
		}
		
		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot + nr),
			      btrfs_item_nr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_item));

		
		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end - total_data, btrfs_leaf_data(leaf) +
			      data_end, old_data - data_end);
		data_end = old_data;
	} else {
		nr = 1;
	}

	
	for (i = 0; i < nr; i++) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key + i);
		btrfs_set_item_key(leaf, &disk_key, slot + i);
		item = btrfs_item_nr(leaf, slot + i);
		btrfs_set_token_item_offset(leaf, item,
					    data_end - data_size[i], &token);
		data_end -= data_size[i];
		btrfs_set_token_item_size(leaf, item, data_size[i], &token);
	}
	btrfs_set_header_nritems(leaf, nritems + nr);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
	if (slot == 0) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key);
		fixup_low_keys(trans, root, path, &disk_key, 1);
	}

	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
out:
	if (!ret)
		ret = nr;
	return ret;
}

void setup_items_for_insert(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root, struct btrfs_path *path,
			    struct btrfs_key *cpu_key, u32 *data_size,
			    u32 total_data, u32 total_size, int nr)
{
	struct btrfs_item *item;
	int i;
	u32 nritems;
	unsigned int data_end;
	struct btrfs_disk_key disk_key;
	struct extent_buffer *leaf;
	int slot;
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	leaf = path->nodes[0];
	slot = path->slots[0];

	nritems = btrfs_header_nritems(leaf);
	data_end = leaf_data_end(root, leaf);

	if (btrfs_leaf_free_space(root, leaf) < total_size) {
		btrfs_print_leaf(root, leaf);
		printk(KERN_CRIT "not enough freespace need %u have %d\n",
		       total_size, btrfs_leaf_free_space(root, leaf));
		BUG();
	}

	if (slot != nritems) {
		unsigned int old_data = btrfs_item_end_nr(leaf, slot);

		if (old_data < data_end) {
			btrfs_print_leaf(root, leaf);
			printk(KERN_CRIT "slot %d old_data %d data_end %d\n",
			       slot, old_data, data_end);
			BUG_ON(1);
		}
		
		for (i = slot; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(leaf, i);
			ioff = btrfs_token_item_offset(leaf, item, &token);
			btrfs_set_token_item_offset(leaf, item,
						    ioff - total_data, &token);
		}
		
		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot + nr),
			      btrfs_item_nr_offset(slot),
			      (nritems - slot) * sizeof(struct btrfs_item));

		
		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end - total_data, btrfs_leaf_data(leaf) +
			      data_end, old_data - data_end);
		data_end = old_data;
	}

	
	for (i = 0; i < nr; i++) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key + i);
		btrfs_set_item_key(leaf, &disk_key, slot + i);
		item = btrfs_item_nr(leaf, slot + i);
		btrfs_set_token_item_offset(leaf, item,
					    data_end - data_size[i], &token);
		data_end -= data_size[i];
		btrfs_set_token_item_size(leaf, item, data_size[i], &token);
	}

	btrfs_set_header_nritems(leaf, nritems + nr);

	if (slot == 0) {
		btrfs_cpu_key_to_disk(&disk_key, cpu_key);
		fixup_low_keys(trans, root, path, &disk_key, 1);
	}
	btrfs_unlock_up_safe(path, 1);
	btrfs_mark_buffer_dirty(leaf);

	if (btrfs_leaf_free_space(root, leaf) < 0) {
		btrfs_print_leaf(root, leaf);
		BUG();
	}
}

int btrfs_insert_empty_items(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path,
			    struct btrfs_key *cpu_key, u32 *data_size,
			    int nr)
{
	int ret = 0;
	int slot;
	int i;
	u32 total_size = 0;
	u32 total_data = 0;

	for (i = 0; i < nr; i++)
		total_data += data_size[i];

	total_size = total_data + (nr * sizeof(struct btrfs_item));
	ret = btrfs_search_slot(trans, root, cpu_key, path, total_size, 1);
	if (ret == 0)
		return -EEXIST;
	if (ret < 0)
		return ret;

	slot = path->slots[0];
	BUG_ON(slot < 0);

	setup_items_for_insert(trans, root, path, cpu_key, data_size,
			       total_data, total_size, nr);
	return 0;
}

int btrfs_insert_item(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *cpu_key, void *data, u32
		      data_size)
{
	int ret = 0;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	unsigned long ptr;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	ret = btrfs_insert_empty_item(trans, root, path, cpu_key, data_size);
	if (!ret) {
		leaf = path->nodes[0];
		ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
		write_extent_buffer(leaf, data, ptr, data_size);
		btrfs_mark_buffer_dirty(leaf);
	}
	btrfs_free_path(path);
	return ret;
}

static void del_ptr(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		    struct btrfs_path *path, int level, int slot)
{
	struct extent_buffer *parent = path->nodes[level];
	u32 nritems;

	nritems = btrfs_header_nritems(parent);
	if (slot != nritems - 1) {
		memmove_extent_buffer(parent,
			      btrfs_node_key_ptr_offset(slot),
			      btrfs_node_key_ptr_offset(slot + 1),
			      sizeof(struct btrfs_key_ptr) *
			      (nritems - slot - 1));
	}
	nritems--;
	btrfs_set_header_nritems(parent, nritems);
	if (nritems == 0 && parent == root->node) {
		BUG_ON(btrfs_header_level(root->node) != 1);
		
		btrfs_set_header_level(root->node, 0);
	} else if (slot == 0) {
		struct btrfs_disk_key disk_key;

		btrfs_node_key(parent, &disk_key, 0);
		fixup_low_keys(trans, root, path, &disk_key, level + 1);
	}
	btrfs_mark_buffer_dirty(parent);
}

static noinline void btrfs_del_leaf(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root,
				    struct btrfs_path *path,
				    struct extent_buffer *leaf)
{
	WARN_ON(btrfs_header_generation(leaf) != trans->transid);
	del_ptr(trans, root, path, 1, path->slots[1]);

	btrfs_unlock_up_safe(path, 0);

	root_sub_used(root, leaf->len);

	extent_buffer_get(leaf);
	btrfs_free_tree_block(trans, root, leaf, 0, 1, 0);
	free_extent_buffer_stale(leaf);
}
int btrfs_del_items(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		    struct btrfs_path *path, int slot, int nr)
{
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	int last_off;
	int dsize = 0;
	int ret = 0;
	int wret;
	int i;
	u32 nritems;
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	leaf = path->nodes[0];
	last_off = btrfs_item_offset_nr(leaf, slot + nr - 1);

	for (i = 0; i < nr; i++)
		dsize += btrfs_item_size_nr(leaf, slot + i);

	nritems = btrfs_header_nritems(leaf);

	if (slot + nr != nritems) {
		int data_end = leaf_data_end(root, leaf);

		memmove_extent_buffer(leaf, btrfs_leaf_data(leaf) +
			      data_end + dsize,
			      btrfs_leaf_data(leaf) + data_end,
			      last_off - data_end);

		for (i = slot + nr; i < nritems; i++) {
			u32 ioff;

			item = btrfs_item_nr(leaf, i);
			ioff = btrfs_token_item_offset(leaf, item, &token);
			btrfs_set_token_item_offset(leaf, item,
						    ioff + dsize, &token);
		}

		memmove_extent_buffer(leaf, btrfs_item_nr_offset(slot),
			      btrfs_item_nr_offset(slot + nr),
			      sizeof(struct btrfs_item) *
			      (nritems - slot - nr));
	}
	btrfs_set_header_nritems(leaf, nritems - nr);
	nritems -= nr;

	
	if (nritems == 0) {
		if (leaf == root->node) {
			btrfs_set_header_level(leaf, 0);
		} else {
			btrfs_set_path_blocking(path);
			clean_tree_block(trans, root, leaf);
			btrfs_del_leaf(trans, root, path, leaf);
		}
	} else {
		int used = leaf_space_used(leaf, 0, nritems);
		if (slot == 0) {
			struct btrfs_disk_key disk_key;

			btrfs_item_key(leaf, &disk_key, 0);
			fixup_low_keys(trans, root, path, &disk_key, 1);
		}

		
		if (used < BTRFS_LEAF_DATA_SIZE(root) / 3) {
			slot = path->slots[1];
			extent_buffer_get(leaf);

			btrfs_set_path_blocking(path);
			wret = push_leaf_left(trans, root, path, 1, 1,
					      1, (u32)-1);
			if (wret < 0 && wret != -ENOSPC)
				ret = wret;

			if (path->nodes[0] == leaf &&
			    btrfs_header_nritems(leaf)) {
				wret = push_leaf_right(trans, root, path, 1,
						       1, 1, 0);
				if (wret < 0 && wret != -ENOSPC)
					ret = wret;
			}

			if (btrfs_header_nritems(leaf) == 0) {
				path->slots[1] = slot;
				btrfs_del_leaf(trans, root, path, leaf);
				free_extent_buffer(leaf);
				ret = 0;
			} else {
				if (path->nodes[0] == leaf)
					btrfs_mark_buffer_dirty(leaf);
				free_extent_buffer(leaf);
			}
		} else {
			btrfs_mark_buffer_dirty(leaf);
		}
	}
	return ret;
}

int btrfs_prev_leaf(struct btrfs_root *root, struct btrfs_path *path)
{
	struct btrfs_key key;
	struct btrfs_disk_key found_key;
	int ret;

	btrfs_item_key_to_cpu(path->nodes[0], &key, 0);

	if (key.offset > 0)
		key.offset--;
	else if (key.type > 0)
		key.type--;
	else if (key.objectid > 0)
		key.objectid--;
	else
		return 1;

	btrfs_release_path(path);
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		return ret;
	btrfs_item_key(path->nodes[0], &found_key, 0);
	ret = comp_keys(&found_key, &key);
	if (ret < 0)
		return 0;
	return 1;
}

int btrfs_search_forward(struct btrfs_root *root, struct btrfs_key *min_key,
			 struct btrfs_key *max_key,
			 struct btrfs_path *path, int cache_only,
			 u64 min_trans)
{
	struct extent_buffer *cur;
	struct btrfs_key found_key;
	int slot;
	int sret;
	u32 nritems;
	int level;
	int ret = 1;

	WARN_ON(!path->keep_locks);
again:
	cur = btrfs_read_lock_root_node(root);
	level = btrfs_header_level(cur);
	WARN_ON(path->nodes[level]);
	path->nodes[level] = cur;
	path->locks[level] = BTRFS_READ_LOCK;

	if (btrfs_header_generation(cur) < min_trans) {
		ret = 1;
		goto out;
	}
	while (1) {
		nritems = btrfs_header_nritems(cur);
		level = btrfs_header_level(cur);
		sret = bin_search(cur, min_key, level, &slot);

		
		if (level == path->lowest_level) {
			if (slot >= nritems)
				goto find_next_key;
			ret = 0;
			path->slots[level] = slot;
			btrfs_item_key_to_cpu(cur, &found_key, slot);
			goto out;
		}
		if (sret && slot > 0)
			slot--;
		while (slot < nritems) {
			u64 blockptr;
			u64 gen;
			struct extent_buffer *tmp;
			struct btrfs_disk_key disk_key;

			blockptr = btrfs_node_blockptr(cur, slot);
			gen = btrfs_node_ptr_generation(cur, slot);
			if (gen < min_trans) {
				slot++;
				continue;
			}
			if (!cache_only)
				break;

			if (max_key) {
				btrfs_node_key(cur, &disk_key, slot);
				if (comp_keys(&disk_key, max_key) >= 0) {
					ret = 1;
					goto out;
				}
			}

			tmp = btrfs_find_tree_block(root, blockptr,
					    btrfs_level_size(root, level - 1));

			if (tmp && btrfs_buffer_uptodate(tmp, gen, 1) > 0) {
				free_extent_buffer(tmp);
				break;
			}
			if (tmp)
				free_extent_buffer(tmp);
			slot++;
		}
find_next_key:
		if (slot >= nritems) {
			path->slots[level] = slot;
			btrfs_set_path_blocking(path);
			sret = btrfs_find_next_key(root, path, min_key, level,
						  cache_only, min_trans);
			if (sret == 0) {
				btrfs_release_path(path);
				goto again;
			} else {
				goto out;
			}
		}
		
		btrfs_node_key_to_cpu(cur, &found_key, slot);
		path->slots[level] = slot;
		if (level == path->lowest_level) {
			ret = 0;
			unlock_up(path, level, 1, 0, NULL);
			goto out;
		}
		btrfs_set_path_blocking(path);
		cur = read_node_slot(root, cur, slot);
		BUG_ON(!cur); 

		btrfs_tree_read_lock(cur);

		path->locks[level - 1] = BTRFS_READ_LOCK;
		path->nodes[level - 1] = cur;
		unlock_up(path, level, 1, 0, NULL);
		btrfs_clear_path_blocking(path, NULL, 0);
	}
out:
	if (ret == 0)
		memcpy(min_key, &found_key, sizeof(found_key));
	btrfs_set_path_blocking(path);
	return ret;
}

int btrfs_find_next_key(struct btrfs_root *root, struct btrfs_path *path,
			struct btrfs_key *key, int level,
			int cache_only, u64 min_trans)
{
	int slot;
	struct extent_buffer *c;

	WARN_ON(!path->keep_locks);
	while (level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level])
			return 1;

		slot = path->slots[level] + 1;
		c = path->nodes[level];
next:
		if (slot >= btrfs_header_nritems(c)) {
			int ret;
			int orig_lowest;
			struct btrfs_key cur_key;
			if (level + 1 >= BTRFS_MAX_LEVEL ||
			    !path->nodes[level + 1])
				return 1;

			if (path->locks[level + 1]) {
				level++;
				continue;
			}

			slot = btrfs_header_nritems(c) - 1;
			if (level == 0)
				btrfs_item_key_to_cpu(c, &cur_key, slot);
			else
				btrfs_node_key_to_cpu(c, &cur_key, slot);

			orig_lowest = path->lowest_level;
			btrfs_release_path(path);
			path->lowest_level = level;
			ret = btrfs_search_slot(NULL, root, &cur_key, path,
						0, 0);
			path->lowest_level = orig_lowest;
			if (ret < 0)
				return ret;

			c = path->nodes[level];
			slot = path->slots[level];
			if (ret == 0)
				slot++;
			goto next;
		}

		if (level == 0)
			btrfs_item_key_to_cpu(c, key, slot);
		else {
			u64 blockptr = btrfs_node_blockptr(c, slot);
			u64 gen = btrfs_node_ptr_generation(c, slot);

			if (cache_only) {
				struct extent_buffer *cur;
				cur = btrfs_find_tree_block(root, blockptr,
					    btrfs_level_size(root, level - 1));
				if (!cur ||
				    btrfs_buffer_uptodate(cur, gen, 1) <= 0) {
					slot++;
					if (cur)
						free_extent_buffer(cur);
					goto next;
				}
				free_extent_buffer(cur);
			}
			if (gen < min_trans) {
				slot++;
				goto next;
			}
			btrfs_node_key_to_cpu(c, key, slot);
		}
		return 0;
	}
	return 1;
}

int btrfs_next_leaf(struct btrfs_root *root, struct btrfs_path *path)
{
	int slot;
	int level;
	struct extent_buffer *c;
	struct extent_buffer *next;
	struct btrfs_key key;
	u32 nritems;
	int ret;
	int old_spinning = path->leave_spinning;
	int next_rw_lock = 0;

	nritems = btrfs_header_nritems(path->nodes[0]);
	if (nritems == 0)
		return 1;

	btrfs_item_key_to_cpu(path->nodes[0], &key, nritems - 1);
again:
	level = 1;
	next = NULL;
	next_rw_lock = 0;
	btrfs_release_path(path);

	path->keep_locks = 1;
	path->leave_spinning = 1;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	path->keep_locks = 0;

	if (ret < 0)
		return ret;

	nritems = btrfs_header_nritems(path->nodes[0]);
	if (nritems > 0 && path->slots[0] < nritems - 1) {
		if (ret == 0)
			path->slots[0]++;
		ret = 0;
		goto done;
	}

	while (level < BTRFS_MAX_LEVEL) {
		if (!path->nodes[level]) {
			ret = 1;
			goto done;
		}

		slot = path->slots[level] + 1;
		c = path->nodes[level];
		if (slot >= btrfs_header_nritems(c)) {
			level++;
			if (level == BTRFS_MAX_LEVEL) {
				ret = 1;
				goto done;
			}
			continue;
		}

		if (next) {
			btrfs_tree_unlock_rw(next, next_rw_lock);
			free_extent_buffer(next);
		}

		next = c;
		next_rw_lock = path->locks[level];
		ret = read_block_for_search(NULL, root, path, &next, level,
					    slot, &key);
		if (ret == -EAGAIN)
			goto again;

		if (ret < 0) {
			btrfs_release_path(path);
			goto done;
		}

		if (!path->skip_locking) {
			ret = btrfs_try_tree_read_lock(next);
			if (!ret) {
				btrfs_set_path_blocking(path);
				btrfs_tree_read_lock(next);
				btrfs_clear_path_blocking(path, next,
							  BTRFS_READ_LOCK);
			}
			next_rw_lock = BTRFS_READ_LOCK;
		}
		break;
	}
	path->slots[level] = slot;
	while (1) {
		level--;
		c = path->nodes[level];
		if (path->locks[level])
			btrfs_tree_unlock_rw(c, path->locks[level]);

		free_extent_buffer(c);
		path->nodes[level] = next;
		path->slots[level] = 0;
		if (!path->skip_locking)
			path->locks[level] = next_rw_lock;
		if (!level)
			break;

		ret = read_block_for_search(NULL, root, path, &next, level,
					    0, &key);
		if (ret == -EAGAIN)
			goto again;

		if (ret < 0) {
			btrfs_release_path(path);
			goto done;
		}

		if (!path->skip_locking) {
			ret = btrfs_try_tree_read_lock(next);
			if (!ret) {
				btrfs_set_path_blocking(path);
				btrfs_tree_read_lock(next);
				btrfs_clear_path_blocking(path, next,
							  BTRFS_READ_LOCK);
			}
			next_rw_lock = BTRFS_READ_LOCK;
		}
	}
	ret = 0;
done:
	unlock_up(path, 0, 1, 0, NULL);
	path->leave_spinning = old_spinning;
	if (!old_spinning)
		btrfs_set_path_blocking(path);

	return ret;
}

int btrfs_previous_item(struct btrfs_root *root,
			struct btrfs_path *path, u64 min_objectid,
			int type)
{
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	u32 nritems;
	int ret;

	while (1) {
		if (path->slots[0] == 0) {
			btrfs_set_path_blocking(path);
			ret = btrfs_prev_leaf(root, path);
			if (ret != 0)
				return ret;
		} else {
			path->slots[0]--;
		}
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (nritems == 0)
			return 1;
		if (path->slots[0] == nritems)
			path->slots[0]--;

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid < min_objectid)
			break;
		if (found_key.type == type)
			return 0;
		if (found_key.objectid == min_objectid &&
		    found_key.type < type)
			break;
	}
	return 1;
}
