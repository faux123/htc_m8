/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmglue.c
 *
 * Code which implements an OCFS2 specific interface to our DLM.
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/quotaops.h>

#define MLOG_MASK_PREFIX ML_DLM_GLUE
#include <cluster/masklog.h>

#include "ocfs2.h"
#include "ocfs2_lockingver.h"

#include "alloc.h"
#include "dcache.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "heartbeat.h"
#include "inode.h"
#include "journal.h"
#include "stackglue.h"
#include "slot_map.h"
#include "super.h"
#include "uptodate.h"
#include "quota.h"
#include "refcounttree.h"

#include "buffer_head_io.h"

struct ocfs2_mask_waiter {
	struct list_head	mw_item;
	int			mw_status;
	struct completion	mw_complete;
	unsigned long		mw_mask;
	unsigned long		mw_goal;
#ifdef CONFIG_OCFS2_FS_STATS
	ktime_t			mw_lock_start;
#endif
};

static struct ocfs2_super *ocfs2_get_dentry_osb(struct ocfs2_lock_res *lockres);
static struct ocfs2_super *ocfs2_get_inode_osb(struct ocfs2_lock_res *lockres);
static struct ocfs2_super *ocfs2_get_file_osb(struct ocfs2_lock_res *lockres);
static struct ocfs2_super *ocfs2_get_qinfo_osb(struct ocfs2_lock_res *lockres);

enum ocfs2_unblock_action {
	UNBLOCK_CONTINUE	= 0, 
	UNBLOCK_CONTINUE_POST	= 1, 
	UNBLOCK_STOP_POST	= 2, 
};

struct ocfs2_unblock_ctl {
	int requeue;
	enum ocfs2_unblock_action unblock_action;
};

struct lock_class_key lockdep_keys[OCFS2_NUM_LOCK_TYPES];

static int ocfs2_check_meta_downconvert(struct ocfs2_lock_res *lockres,
					int new_level);
static void ocfs2_set_meta_lvb(struct ocfs2_lock_res *lockres);

static int ocfs2_data_convert_worker(struct ocfs2_lock_res *lockres,
				     int blocking);

static int ocfs2_dentry_convert_worker(struct ocfs2_lock_res *lockres,
				       int blocking);

static void ocfs2_dentry_post_unlock(struct ocfs2_super *osb,
				     struct ocfs2_lock_res *lockres);

static void ocfs2_set_qinfo_lvb(struct ocfs2_lock_res *lockres);

static int ocfs2_check_refcount_downconvert(struct ocfs2_lock_res *lockres,
					    int new_level);
static int ocfs2_refcount_convert_worker(struct ocfs2_lock_res *lockres,
					 int blocking);

#define mlog_meta_lvb(__level, __lockres) ocfs2_dump_meta_lvb_info(__level, __PRETTY_FUNCTION__, __LINE__, __lockres)

static void ocfs2_dump_meta_lvb_info(u64 level,
				     const char *function,
				     unsigned int line,
				     struct ocfs2_lock_res *lockres)
{
	struct ocfs2_meta_lvb *lvb = ocfs2_dlm_lvb(&lockres->l_lksb);

	mlog(level, "LVB information for %s (called from %s:%u):\n",
	     lockres->l_name, function, line);
	mlog(level, "version: %u, clusters: %u, generation: 0x%x\n",
	     lvb->lvb_version, be32_to_cpu(lvb->lvb_iclusters),
	     be32_to_cpu(lvb->lvb_igeneration));
	mlog(level, "size: %llu, uid %u, gid %u, mode 0x%x\n",
	     (unsigned long long)be64_to_cpu(lvb->lvb_isize),
	     be32_to_cpu(lvb->lvb_iuid), be32_to_cpu(lvb->lvb_igid),
	     be16_to_cpu(lvb->lvb_imode));
	mlog(level, "nlink %u, atime_packed 0x%llx, ctime_packed 0x%llx, "
	     "mtime_packed 0x%llx iattr 0x%x\n", be16_to_cpu(lvb->lvb_inlink),
	     (long long)be64_to_cpu(lvb->lvb_iatime_packed),
	     (long long)be64_to_cpu(lvb->lvb_ictime_packed),
	     (long long)be64_to_cpu(lvb->lvb_imtime_packed),
	     be32_to_cpu(lvb->lvb_iattr));
}


struct ocfs2_lock_res_ops {
	struct ocfs2_super * (*get_osb)(struct ocfs2_lock_res *);

	void (*post_unlock)(struct ocfs2_super *, struct ocfs2_lock_res *);

	int (*check_downconvert)(struct ocfs2_lock_res *, int);

	void (*set_lvb)(struct ocfs2_lock_res *);

	int (*downconvert_worker)(struct ocfs2_lock_res *, int);

	int flags;
};

#define LOCK_TYPE_REQUIRES_REFRESH 0x1

#define LOCK_TYPE_USES_LVB		0x2

static struct ocfs2_lock_res_ops ocfs2_inode_rw_lops = {
	.get_osb	= ocfs2_get_inode_osb,
	.flags		= 0,
};

static struct ocfs2_lock_res_ops ocfs2_inode_inode_lops = {
	.get_osb	= ocfs2_get_inode_osb,
	.check_downconvert = ocfs2_check_meta_downconvert,
	.set_lvb	= ocfs2_set_meta_lvb,
	.downconvert_worker = ocfs2_data_convert_worker,
	.flags		= LOCK_TYPE_REQUIRES_REFRESH|LOCK_TYPE_USES_LVB,
};

static struct ocfs2_lock_res_ops ocfs2_super_lops = {
	.flags		= LOCK_TYPE_REQUIRES_REFRESH,
};

static struct ocfs2_lock_res_ops ocfs2_rename_lops = {
	.flags		= 0,
};

static struct ocfs2_lock_res_ops ocfs2_nfs_sync_lops = {
	.flags		= 0,
};

static struct ocfs2_lock_res_ops ocfs2_orphan_scan_lops = {
	.flags		= LOCK_TYPE_REQUIRES_REFRESH|LOCK_TYPE_USES_LVB,
};

static struct ocfs2_lock_res_ops ocfs2_dentry_lops = {
	.get_osb	= ocfs2_get_dentry_osb,
	.post_unlock	= ocfs2_dentry_post_unlock,
	.downconvert_worker = ocfs2_dentry_convert_worker,
	.flags		= 0,
};

static struct ocfs2_lock_res_ops ocfs2_inode_open_lops = {
	.get_osb	= ocfs2_get_inode_osb,
	.flags		= 0,
};

static struct ocfs2_lock_res_ops ocfs2_flock_lops = {
	.get_osb	= ocfs2_get_file_osb,
	.flags		= 0,
};

static struct ocfs2_lock_res_ops ocfs2_qinfo_lops = {
	.set_lvb	= ocfs2_set_qinfo_lvb,
	.get_osb	= ocfs2_get_qinfo_osb,
	.flags		= LOCK_TYPE_REQUIRES_REFRESH | LOCK_TYPE_USES_LVB,
};

static struct ocfs2_lock_res_ops ocfs2_refcount_block_lops = {
	.check_downconvert = ocfs2_check_refcount_downconvert,
	.downconvert_worker = ocfs2_refcount_convert_worker,
	.flags		= 0,
};

static inline int ocfs2_is_inode_lock(struct ocfs2_lock_res *lockres)
{
	return lockres->l_type == OCFS2_LOCK_TYPE_META ||
		lockres->l_type == OCFS2_LOCK_TYPE_RW ||
		lockres->l_type == OCFS2_LOCK_TYPE_OPEN;
}

static inline struct ocfs2_lock_res *ocfs2_lksb_to_lock_res(struct ocfs2_dlm_lksb *lksb)
{
	return container_of(lksb, struct ocfs2_lock_res, l_lksb);
}

static inline struct inode *ocfs2_lock_res_inode(struct ocfs2_lock_res *lockres)
{
	BUG_ON(!ocfs2_is_inode_lock(lockres));

	return (struct inode *) lockres->l_priv;
}

static inline struct ocfs2_dentry_lock *ocfs2_lock_res_dl(struct ocfs2_lock_res *lockres)
{
	BUG_ON(lockres->l_type != OCFS2_LOCK_TYPE_DENTRY);

	return (struct ocfs2_dentry_lock *)lockres->l_priv;
}

static inline struct ocfs2_mem_dqinfo *ocfs2_lock_res_qinfo(struct ocfs2_lock_res *lockres)
{
	BUG_ON(lockres->l_type != OCFS2_LOCK_TYPE_QINFO);

	return (struct ocfs2_mem_dqinfo *)lockres->l_priv;
}

static inline struct ocfs2_refcount_tree *
ocfs2_lock_res_refcount_tree(struct ocfs2_lock_res *res)
{
	return container_of(res, struct ocfs2_refcount_tree, rf_lockres);
}

static inline struct ocfs2_super *ocfs2_get_lockres_osb(struct ocfs2_lock_res *lockres)
{
	if (lockres->l_ops->get_osb)
		return lockres->l_ops->get_osb(lockres);

	return (struct ocfs2_super *)lockres->l_priv;
}

static int ocfs2_lock_create(struct ocfs2_super *osb,
			     struct ocfs2_lock_res *lockres,
			     int level,
			     u32 dlm_flags);
static inline int ocfs2_may_continue_on_blocked_lock(struct ocfs2_lock_res *lockres,
						     int wanted);
static void __ocfs2_cluster_unlock(struct ocfs2_super *osb,
				   struct ocfs2_lock_res *lockres,
				   int level, unsigned long caller_ip);
static inline void ocfs2_cluster_unlock(struct ocfs2_super *osb,
					struct ocfs2_lock_res *lockres,
					int level)
{
	__ocfs2_cluster_unlock(osb, lockres, level, _RET_IP_);
}

static inline void ocfs2_generic_handle_downconvert_action(struct ocfs2_lock_res *lockres);
static inline void ocfs2_generic_handle_convert_action(struct ocfs2_lock_res *lockres);
static inline void ocfs2_generic_handle_attach_action(struct ocfs2_lock_res *lockres);
static int ocfs2_generic_handle_bast(struct ocfs2_lock_res *lockres, int level);
static void ocfs2_schedule_blocked_lock(struct ocfs2_super *osb,
					struct ocfs2_lock_res *lockres);
static inline void ocfs2_recover_from_dlm_error(struct ocfs2_lock_res *lockres,
						int convert);
#define ocfs2_log_dlm_error(_func, _err, _lockres) do {					\
	if ((_lockres)->l_type != OCFS2_LOCK_TYPE_DENTRY)				\
		mlog(ML_ERROR, "DLM error %d while calling %s on resource %s\n",	\
		     _err, _func, _lockres->l_name);					\
	else										\
		mlog(ML_ERROR, "DLM error %d while calling %s on resource %.*s%08x\n",	\
		     _err, _func, OCFS2_DENTRY_LOCK_INO_START - 1, (_lockres)->l_name,	\
		     (unsigned int)ocfs2_get_dentry_lock_ino(_lockres));		\
} while (0)
static int ocfs2_downconvert_thread(void *arg);
static void ocfs2_downconvert_on_unlock(struct ocfs2_super *osb,
					struct ocfs2_lock_res *lockres);
static int ocfs2_inode_lock_update(struct inode *inode,
				  struct buffer_head **bh);
static void ocfs2_drop_osb_locks(struct ocfs2_super *osb);
static inline int ocfs2_highest_compat_lock_level(int level);
static unsigned int ocfs2_prepare_downconvert(struct ocfs2_lock_res *lockres,
					      int new_level);
static int ocfs2_downconvert_lock(struct ocfs2_super *osb,
				  struct ocfs2_lock_res *lockres,
				  int new_level,
				  int lvb,
				  unsigned int generation);
static int ocfs2_prepare_cancel_convert(struct ocfs2_super *osb,
				        struct ocfs2_lock_res *lockres);
static int ocfs2_cancel_convert(struct ocfs2_super *osb,
				struct ocfs2_lock_res *lockres);


static void ocfs2_build_lock_name(enum ocfs2_lock_type type,
				  u64 blkno,
				  u32 generation,
				  char *name)
{
	int len;

	BUG_ON(type >= OCFS2_NUM_LOCK_TYPES);

	len = snprintf(name, OCFS2_LOCK_ID_MAX_LEN, "%c%s%016llx%08x",
		       ocfs2_lock_type_char(type), OCFS2_LOCK_ID_PAD,
		       (long long)blkno, generation);

	BUG_ON(len != (OCFS2_LOCK_ID_MAX_LEN - 1));

	mlog(0, "built lock resource with name: %s\n", name);
}

static DEFINE_SPINLOCK(ocfs2_dlm_tracking_lock);

static void ocfs2_add_lockres_tracking(struct ocfs2_lock_res *res,
				       struct ocfs2_dlm_debug *dlm_debug)
{
	mlog(0, "Add tracking for lockres %s\n", res->l_name);

	spin_lock(&ocfs2_dlm_tracking_lock);
	list_add(&res->l_debug_list, &dlm_debug->d_lockres_tracking);
	spin_unlock(&ocfs2_dlm_tracking_lock);
}

static void ocfs2_remove_lockres_tracking(struct ocfs2_lock_res *res)
{
	spin_lock(&ocfs2_dlm_tracking_lock);
	if (!list_empty(&res->l_debug_list))
		list_del_init(&res->l_debug_list);
	spin_unlock(&ocfs2_dlm_tracking_lock);
}

#ifdef CONFIG_OCFS2_FS_STATS
static void ocfs2_init_lock_stats(struct ocfs2_lock_res *res)
{
	res->l_lock_refresh = 0;
	memset(&res->l_lock_prmode, 0, sizeof(struct ocfs2_lock_stats));
	memset(&res->l_lock_exmode, 0, sizeof(struct ocfs2_lock_stats));
}

static void ocfs2_update_lock_stats(struct ocfs2_lock_res *res, int level,
				    struct ocfs2_mask_waiter *mw, int ret)
{
	u32 usec;
	ktime_t kt;
	struct ocfs2_lock_stats *stats;

	if (level == LKM_PRMODE)
		stats = &res->l_lock_prmode;
	else if (level == LKM_EXMODE)
		stats = &res->l_lock_exmode;
	else
		return;

	kt = ktime_sub(ktime_get(), mw->mw_lock_start);
	usec = ktime_to_us(kt);

	stats->ls_gets++;
	stats->ls_total += ktime_to_ns(kt);
	
	if (unlikely(stats->ls_gets) == 0) {
		stats->ls_gets++;
		stats->ls_total = ktime_to_ns(kt);
	}

	if (stats->ls_max < usec)
		stats->ls_max = usec;

	if (ret)
		stats->ls_fail++;
}

static inline void ocfs2_track_lock_refresh(struct ocfs2_lock_res *lockres)
{
	lockres->l_lock_refresh++;
}

static inline void ocfs2_init_start_time(struct ocfs2_mask_waiter *mw)
{
	mw->mw_lock_start = ktime_get();
}
#else
static inline void ocfs2_init_lock_stats(struct ocfs2_lock_res *res)
{
}
static inline void ocfs2_update_lock_stats(struct ocfs2_lock_res *res,
			   int level, struct ocfs2_mask_waiter *mw, int ret)
{
}
static inline void ocfs2_track_lock_refresh(struct ocfs2_lock_res *lockres)
{
}
static inline void ocfs2_init_start_time(struct ocfs2_mask_waiter *mw)
{
}
#endif

static void ocfs2_lock_res_init_common(struct ocfs2_super *osb,
				       struct ocfs2_lock_res *res,
				       enum ocfs2_lock_type type,
				       struct ocfs2_lock_res_ops *ops,
				       void *priv)
{
	res->l_type          = type;
	res->l_ops           = ops;
	res->l_priv          = priv;

	res->l_level         = DLM_LOCK_IV;
	res->l_requested     = DLM_LOCK_IV;
	res->l_blocking      = DLM_LOCK_IV;
	res->l_action        = OCFS2_AST_INVALID;
	res->l_unlock_action = OCFS2_UNLOCK_INVALID;

	res->l_flags         = OCFS2_LOCK_INITIALIZED;

	ocfs2_add_lockres_tracking(res, osb->osb_dlm_debug);

	ocfs2_init_lock_stats(res);
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (type != OCFS2_LOCK_TYPE_OPEN)
		lockdep_init_map(&res->l_lockdep_map, ocfs2_lock_type_strings[type],
				 &lockdep_keys[type], 0);
	else
		res->l_lockdep_map.key = NULL;
#endif
}

void ocfs2_lock_res_init_once(struct ocfs2_lock_res *res)
{
	
	memset(res, 0, sizeof(struct ocfs2_lock_res));
	spin_lock_init(&res->l_lock);
	init_waitqueue_head(&res->l_event);
	INIT_LIST_HEAD(&res->l_blocked_list);
	INIT_LIST_HEAD(&res->l_mask_waiters);
}

void ocfs2_inode_lock_res_init(struct ocfs2_lock_res *res,
			       enum ocfs2_lock_type type,
			       unsigned int generation,
			       struct inode *inode)
{
	struct ocfs2_lock_res_ops *ops;

	switch(type) {
		case OCFS2_LOCK_TYPE_RW:
			ops = &ocfs2_inode_rw_lops;
			break;
		case OCFS2_LOCK_TYPE_META:
			ops = &ocfs2_inode_inode_lops;
			break;
		case OCFS2_LOCK_TYPE_OPEN:
			ops = &ocfs2_inode_open_lops;
			break;
		default:
			mlog_bug_on_msg(1, "type: %d\n", type);
			ops = NULL; 
			break;
	};

	ocfs2_build_lock_name(type, OCFS2_I(inode)->ip_blkno,
			      generation, res->l_name);
	ocfs2_lock_res_init_common(OCFS2_SB(inode->i_sb), res, type, ops, inode);
}

static struct ocfs2_super *ocfs2_get_inode_osb(struct ocfs2_lock_res *lockres)
{
	struct inode *inode = ocfs2_lock_res_inode(lockres);

	return OCFS2_SB(inode->i_sb);
}

static struct ocfs2_super *ocfs2_get_qinfo_osb(struct ocfs2_lock_res *lockres)
{
	struct ocfs2_mem_dqinfo *info = lockres->l_priv;

	return OCFS2_SB(info->dqi_gi.dqi_sb);
}

static struct ocfs2_super *ocfs2_get_file_osb(struct ocfs2_lock_res *lockres)
{
	struct ocfs2_file_private *fp = lockres->l_priv;

	return OCFS2_SB(fp->fp_file->f_mapping->host->i_sb);
}

static __u64 ocfs2_get_dentry_lock_ino(struct ocfs2_lock_res *lockres)
{
	__be64 inode_blkno_be;

	memcpy(&inode_blkno_be, &lockres->l_name[OCFS2_DENTRY_LOCK_INO_START],
	       sizeof(__be64));

	return be64_to_cpu(inode_blkno_be);
}

static struct ocfs2_super *ocfs2_get_dentry_osb(struct ocfs2_lock_res *lockres)
{
	struct ocfs2_dentry_lock *dl = lockres->l_priv;

	return OCFS2_SB(dl->dl_inode->i_sb);
}

void ocfs2_dentry_lock_res_init(struct ocfs2_dentry_lock *dl,
				u64 parent, struct inode *inode)
{
	int len;
	u64 inode_blkno = OCFS2_I(inode)->ip_blkno;
	__be64 inode_blkno_be = cpu_to_be64(inode_blkno);
	struct ocfs2_lock_res *lockres = &dl->dl_lockres;

	ocfs2_lock_res_init_once(lockres);

	len = snprintf(lockres->l_name, OCFS2_DENTRY_LOCK_INO_START,
		       "%c%016llx",
		       ocfs2_lock_type_char(OCFS2_LOCK_TYPE_DENTRY),
		       (long long)parent);

	BUG_ON(len != (OCFS2_DENTRY_LOCK_INO_START - 1));

	memcpy(&lockres->l_name[OCFS2_DENTRY_LOCK_INO_START], &inode_blkno_be,
	       sizeof(__be64));

	ocfs2_lock_res_init_common(OCFS2_SB(inode->i_sb), lockres,
				   OCFS2_LOCK_TYPE_DENTRY, &ocfs2_dentry_lops,
				   dl);
}

static void ocfs2_super_lock_res_init(struct ocfs2_lock_res *res,
				      struct ocfs2_super *osb)
{
	ocfs2_lock_res_init_once(res);
	ocfs2_build_lock_name(OCFS2_LOCK_TYPE_SUPER, OCFS2_SUPER_BLOCK_BLKNO,
			      0, res->l_name);
	ocfs2_lock_res_init_common(osb, res, OCFS2_LOCK_TYPE_SUPER,
				   &ocfs2_super_lops, osb);
}

static void ocfs2_rename_lock_res_init(struct ocfs2_lock_res *res,
				       struct ocfs2_super *osb)
{
	ocfs2_lock_res_init_once(res);
	ocfs2_build_lock_name(OCFS2_LOCK_TYPE_RENAME, 0, 0, res->l_name);
	ocfs2_lock_res_init_common(osb, res, OCFS2_LOCK_TYPE_RENAME,
				   &ocfs2_rename_lops, osb);
}

static void ocfs2_nfs_sync_lock_res_init(struct ocfs2_lock_res *res,
					 struct ocfs2_super *osb)
{
	ocfs2_lock_res_init_once(res);
	ocfs2_build_lock_name(OCFS2_LOCK_TYPE_NFS_SYNC, 0, 0, res->l_name);
	ocfs2_lock_res_init_common(osb, res, OCFS2_LOCK_TYPE_NFS_SYNC,
				   &ocfs2_nfs_sync_lops, osb);
}

static void ocfs2_orphan_scan_lock_res_init(struct ocfs2_lock_res *res,
					    struct ocfs2_super *osb)
{
	ocfs2_lock_res_init_once(res);
	ocfs2_build_lock_name(OCFS2_LOCK_TYPE_ORPHAN_SCAN, 0, 0, res->l_name);
	ocfs2_lock_res_init_common(osb, res, OCFS2_LOCK_TYPE_ORPHAN_SCAN,
				   &ocfs2_orphan_scan_lops, osb);
}

void ocfs2_file_lock_res_init(struct ocfs2_lock_res *lockres,
			      struct ocfs2_file_private *fp)
{
	struct inode *inode = fp->fp_file->f_mapping->host;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	ocfs2_lock_res_init_once(lockres);
	ocfs2_build_lock_name(OCFS2_LOCK_TYPE_FLOCK, oi->ip_blkno,
			      inode->i_generation, lockres->l_name);
	ocfs2_lock_res_init_common(OCFS2_SB(inode->i_sb), lockres,
				   OCFS2_LOCK_TYPE_FLOCK, &ocfs2_flock_lops,
				   fp);
	lockres->l_flags |= OCFS2_LOCK_NOCACHE;
}

void ocfs2_qinfo_lock_res_init(struct ocfs2_lock_res *lockres,
			       struct ocfs2_mem_dqinfo *info)
{
	ocfs2_lock_res_init_once(lockres);
	ocfs2_build_lock_name(OCFS2_LOCK_TYPE_QINFO, info->dqi_gi.dqi_type,
			      0, lockres->l_name);
	ocfs2_lock_res_init_common(OCFS2_SB(info->dqi_gi.dqi_sb), lockres,
				   OCFS2_LOCK_TYPE_QINFO, &ocfs2_qinfo_lops,
				   info);
}

void ocfs2_refcount_lock_res_init(struct ocfs2_lock_res *lockres,
				  struct ocfs2_super *osb, u64 ref_blkno,
				  unsigned int generation)
{
	ocfs2_lock_res_init_once(lockres);
	ocfs2_build_lock_name(OCFS2_LOCK_TYPE_REFCOUNT, ref_blkno,
			      generation, lockres->l_name);
	ocfs2_lock_res_init_common(osb, lockres, OCFS2_LOCK_TYPE_REFCOUNT,
				   &ocfs2_refcount_block_lops, osb);
}

void ocfs2_lock_res_free(struct ocfs2_lock_res *res)
{
	if (!(res->l_flags & OCFS2_LOCK_INITIALIZED))
		return;

	ocfs2_remove_lockres_tracking(res);

	mlog_bug_on_msg(!list_empty(&res->l_blocked_list),
			"Lockres %s is on the blocked list\n",
			res->l_name);
	mlog_bug_on_msg(!list_empty(&res->l_mask_waiters),
			"Lockres %s has mask waiters pending\n",
			res->l_name);
	mlog_bug_on_msg(spin_is_locked(&res->l_lock),
			"Lockres %s is locked\n",
			res->l_name);
	mlog_bug_on_msg(res->l_ro_holders,
			"Lockres %s has %u ro holders\n",
			res->l_name, res->l_ro_holders);
	mlog_bug_on_msg(res->l_ex_holders,
			"Lockres %s has %u ex holders\n",
			res->l_name, res->l_ex_holders);

	
	memset(&res->l_lksb, 0, sizeof(res->l_lksb));

	res->l_flags = 0UL;
}

static inline void ocfs2_inc_holders(struct ocfs2_lock_res *lockres,
				     int level)
{
	BUG_ON(!lockres);

	switch(level) {
	case DLM_LOCK_EX:
		lockres->l_ex_holders++;
		break;
	case DLM_LOCK_PR:
		lockres->l_ro_holders++;
		break;
	default:
		BUG();
	}
}

static inline void ocfs2_dec_holders(struct ocfs2_lock_res *lockres,
				     int level)
{
	BUG_ON(!lockres);

	switch(level) {
	case DLM_LOCK_EX:
		BUG_ON(!lockres->l_ex_holders);
		lockres->l_ex_holders--;
		break;
	case DLM_LOCK_PR:
		BUG_ON(!lockres->l_ro_holders);
		lockres->l_ro_holders--;
		break;
	default:
		BUG();
	}
}

static inline int ocfs2_highest_compat_lock_level(int level)
{
	int new_level = DLM_LOCK_EX;

	if (level == DLM_LOCK_EX)
		new_level = DLM_LOCK_NL;
	else if (level == DLM_LOCK_PR)
		new_level = DLM_LOCK_PR;
	return new_level;
}

static void lockres_set_flags(struct ocfs2_lock_res *lockres,
			      unsigned long newflags)
{
	struct ocfs2_mask_waiter *mw, *tmp;

 	assert_spin_locked(&lockres->l_lock);

	lockres->l_flags = newflags;

	list_for_each_entry_safe(mw, tmp, &lockres->l_mask_waiters, mw_item) {
		if ((lockres->l_flags & mw->mw_mask) != mw->mw_goal)
			continue;

		list_del_init(&mw->mw_item);
		mw->mw_status = 0;
		complete(&mw->mw_complete);
	}
}
static void lockres_or_flags(struct ocfs2_lock_res *lockres, unsigned long or)
{
	lockres_set_flags(lockres, lockres->l_flags | or);
}
static void lockres_clear_flags(struct ocfs2_lock_res *lockres,
				unsigned long clear)
{
	lockres_set_flags(lockres, lockres->l_flags & ~clear);
}

static inline void ocfs2_generic_handle_downconvert_action(struct ocfs2_lock_res *lockres)
{
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BUSY));
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_ATTACHED));
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BLOCKED));
	BUG_ON(lockres->l_blocking <= DLM_LOCK_NL);

	lockres->l_level = lockres->l_requested;
	if (lockres->l_level <=
	    ocfs2_highest_compat_lock_level(lockres->l_blocking)) {
		lockres->l_blocking = DLM_LOCK_NL;
		lockres_clear_flags(lockres, OCFS2_LOCK_BLOCKED);
	}
	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);
}

static inline void ocfs2_generic_handle_convert_action(struct ocfs2_lock_res *lockres)
{
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BUSY));
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_ATTACHED));

	if (lockres->l_level == DLM_LOCK_NL &&
	    lockres->l_ops->flags & LOCK_TYPE_REQUIRES_REFRESH)
		lockres_or_flags(lockres, OCFS2_LOCK_NEEDS_REFRESH);

	lockres->l_level = lockres->l_requested;

	lockres_or_flags(lockres, OCFS2_LOCK_UPCONVERT_FINISHING);

	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);
}

static inline void ocfs2_generic_handle_attach_action(struct ocfs2_lock_res *lockres)
{
	BUG_ON((!(lockres->l_flags & OCFS2_LOCK_BUSY)));
	BUG_ON(lockres->l_flags & OCFS2_LOCK_ATTACHED);

	if (lockres->l_requested > DLM_LOCK_NL &&
	    !(lockres->l_flags & OCFS2_LOCK_LOCAL) &&
	    lockres->l_ops->flags & LOCK_TYPE_REQUIRES_REFRESH)
		lockres_or_flags(lockres, OCFS2_LOCK_NEEDS_REFRESH);

	lockres->l_level = lockres->l_requested;
	lockres_or_flags(lockres, OCFS2_LOCK_ATTACHED);
	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);
}

static int ocfs2_generic_handle_bast(struct ocfs2_lock_res *lockres,
				     int level)
{
	int needs_downconvert = 0;

	assert_spin_locked(&lockres->l_lock);

	if (level > lockres->l_blocking) {
		if (ocfs2_highest_compat_lock_level(level) <
		    ocfs2_highest_compat_lock_level(lockres->l_blocking))
			needs_downconvert = 1;

		lockres->l_blocking = level;
	}

	mlog(ML_BASTS, "lockres %s, block %d, level %d, l_block %d, dwn %d\n",
	     lockres->l_name, level, lockres->l_level, lockres->l_blocking,
	     needs_downconvert);

	if (needs_downconvert)
		lockres_or_flags(lockres, OCFS2_LOCK_BLOCKED);
	mlog(0, "needs_downconvert = %d\n", needs_downconvert);
	return needs_downconvert;
}


static void __lockres_clear_pending(struct ocfs2_lock_res *lockres,
				    unsigned int generation,
				    struct ocfs2_super *osb)
{
	assert_spin_locked(&lockres->l_lock);

	if (!(lockres->l_flags & OCFS2_LOCK_PENDING) ||
	    (lockres->l_pending_gen != generation))
		return;

	lockres_clear_flags(lockres, OCFS2_LOCK_PENDING);
	lockres->l_pending_gen++;

	if (lockres->l_flags & OCFS2_LOCK_BLOCKED)
		ocfs2_wake_downconvert_thread(osb);
}

static void lockres_clear_pending(struct ocfs2_lock_res *lockres,
				  unsigned int generation,
				  struct ocfs2_super *osb)
{
	unsigned long flags;

	spin_lock_irqsave(&lockres->l_lock, flags);
	__lockres_clear_pending(lockres, generation, osb);
	spin_unlock_irqrestore(&lockres->l_lock, flags);
}

static unsigned int lockres_set_pending(struct ocfs2_lock_res *lockres)
{
	assert_spin_locked(&lockres->l_lock);
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BUSY));

	lockres_or_flags(lockres, OCFS2_LOCK_PENDING);

	return lockres->l_pending_gen;
}

static void ocfs2_blocking_ast(struct ocfs2_dlm_lksb *lksb, int level)
{
	struct ocfs2_lock_res *lockres = ocfs2_lksb_to_lock_res(lksb);
	struct ocfs2_super *osb = ocfs2_get_lockres_osb(lockres);
	int needs_downconvert;
	unsigned long flags;

	BUG_ON(level <= DLM_LOCK_NL);

	mlog(ML_BASTS, "BAST fired for lockres %s, blocking %d, level %d, "
	     "type %s\n", lockres->l_name, level, lockres->l_level,
	     ocfs2_lock_type_string(lockres->l_type));

	if (lockres->l_flags & OCFS2_LOCK_NOCACHE)
		return;

	spin_lock_irqsave(&lockres->l_lock, flags);
	needs_downconvert = ocfs2_generic_handle_bast(lockres, level);
	if (needs_downconvert)
		ocfs2_schedule_blocked_lock(osb, lockres);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	wake_up(&lockres->l_event);

	ocfs2_wake_downconvert_thread(osb);
}

static void ocfs2_locking_ast(struct ocfs2_dlm_lksb *lksb)
{
	struct ocfs2_lock_res *lockres = ocfs2_lksb_to_lock_res(lksb);
	struct ocfs2_super *osb = ocfs2_get_lockres_osb(lockres);
	unsigned long flags;
	int status;

	spin_lock_irqsave(&lockres->l_lock, flags);

	status = ocfs2_dlm_lock_status(&lockres->l_lksb);

	if (status == -EAGAIN) {
		lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);
		goto out;
	}

	if (status) {
		mlog(ML_ERROR, "lockres %s: lksb status value of %d!\n",
		     lockres->l_name, status);
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		return;
	}

	mlog(ML_BASTS, "AST fired for lockres %s, action %d, unlock %d, "
	     "level %d => %d\n", lockres->l_name, lockres->l_action,
	     lockres->l_unlock_action, lockres->l_level, lockres->l_requested);

	switch(lockres->l_action) {
	case OCFS2_AST_ATTACH:
		ocfs2_generic_handle_attach_action(lockres);
		lockres_clear_flags(lockres, OCFS2_LOCK_LOCAL);
		break;
	case OCFS2_AST_CONVERT:
		ocfs2_generic_handle_convert_action(lockres);
		break;
	case OCFS2_AST_DOWNCONVERT:
		ocfs2_generic_handle_downconvert_action(lockres);
		break;
	default:
		mlog(ML_ERROR, "lockres %s: AST fired with invalid action: %u, "
		     "flags 0x%lx, unlock: %u\n",
		     lockres->l_name, lockres->l_action, lockres->l_flags,
		     lockres->l_unlock_action);
		BUG();
	}
out:
	lockres->l_action = OCFS2_AST_INVALID;

	
	if (lockres->l_unlock_action == OCFS2_UNLOCK_CANCEL_CONVERT)
		lockres->l_unlock_action = OCFS2_UNLOCK_INVALID;

	__lockres_clear_pending(lockres, lockres->l_pending_gen,  osb);

	wake_up(&lockres->l_event);
	spin_unlock_irqrestore(&lockres->l_lock, flags);
}

static void ocfs2_unlock_ast(struct ocfs2_dlm_lksb *lksb, int error)
{
	struct ocfs2_lock_res *lockres = ocfs2_lksb_to_lock_res(lksb);
	unsigned long flags;

	mlog(ML_BASTS, "UNLOCK AST fired for lockres %s, action = %d\n",
	     lockres->l_name, lockres->l_unlock_action);

	spin_lock_irqsave(&lockres->l_lock, flags);
	if (error) {
		mlog(ML_ERROR, "Dlm passes error %d for lock %s, "
		     "unlock_action %d\n", error, lockres->l_name,
		     lockres->l_unlock_action);
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		return;
	}

	switch(lockres->l_unlock_action) {
	case OCFS2_UNLOCK_CANCEL_CONVERT:
		mlog(0, "Cancel convert success for %s\n", lockres->l_name);
		lockres->l_action = OCFS2_AST_INVALID;
		if (lockres->l_flags & OCFS2_LOCK_BLOCKED)
			ocfs2_wake_downconvert_thread(ocfs2_get_lockres_osb(lockres));
		break;
	case OCFS2_UNLOCK_DROP_LOCK:
		lockres->l_level = DLM_LOCK_IV;
		break;
	default:
		BUG();
	}

	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);
	lockres->l_unlock_action = OCFS2_UNLOCK_INVALID;
	wake_up(&lockres->l_event);
	spin_unlock_irqrestore(&lockres->l_lock, flags);
}

static struct ocfs2_locking_protocol lproto = {
	.lp_max_version = {
		.pv_major = OCFS2_LOCKING_PROTOCOL_MAJOR,
		.pv_minor = OCFS2_LOCKING_PROTOCOL_MINOR,
	},
	.lp_lock_ast		= ocfs2_locking_ast,
	.lp_blocking_ast	= ocfs2_blocking_ast,
	.lp_unlock_ast		= ocfs2_unlock_ast,
};

void ocfs2_set_locking_protocol(void)
{
	ocfs2_stack_glue_set_max_proto_version(&lproto.lp_max_version);
}

static inline void ocfs2_recover_from_dlm_error(struct ocfs2_lock_res *lockres,
						int convert)
{
	unsigned long flags;

	spin_lock_irqsave(&lockres->l_lock, flags);
	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);
	lockres_clear_flags(lockres, OCFS2_LOCK_UPCONVERT_FINISHING);
	if (convert)
		lockres->l_action = OCFS2_AST_INVALID;
	else
		lockres->l_unlock_action = OCFS2_UNLOCK_INVALID;
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	wake_up(&lockres->l_event);
}

static int ocfs2_lock_create(struct ocfs2_super *osb,
			     struct ocfs2_lock_res *lockres,
			     int level,
			     u32 dlm_flags)
{
	int ret = 0;
	unsigned long flags;
	unsigned int gen;

	mlog(0, "lock %s, level = %d, flags = %u\n", lockres->l_name, level,
	     dlm_flags);

	spin_lock_irqsave(&lockres->l_lock, flags);
	if ((lockres->l_flags & OCFS2_LOCK_ATTACHED) ||
	    (lockres->l_flags & OCFS2_LOCK_BUSY)) {
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		goto bail;
	}

	lockres->l_action = OCFS2_AST_ATTACH;
	lockres->l_requested = level;
	lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
	gen = lockres_set_pending(lockres);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	ret = ocfs2_dlm_lock(osb->cconn,
			     level,
			     &lockres->l_lksb,
			     dlm_flags,
			     lockres->l_name,
			     OCFS2_LOCK_ID_MAX_LEN - 1);
	lockres_clear_pending(lockres, gen, osb);
	if (ret) {
		ocfs2_log_dlm_error("ocfs2_dlm_lock", ret, lockres);
		ocfs2_recover_from_dlm_error(lockres, 1);
	}

	mlog(0, "lock %s, return from ocfs2_dlm_lock\n", lockres->l_name);

bail:
	return ret;
}

static inline int ocfs2_check_wait_flag(struct ocfs2_lock_res *lockres,
					int flag)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&lockres->l_lock, flags);
	ret = lockres->l_flags & flag;
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	return ret;
}

static inline void ocfs2_wait_on_busy_lock(struct ocfs2_lock_res *lockres)

{
	wait_event(lockres->l_event,
		   !ocfs2_check_wait_flag(lockres, OCFS2_LOCK_BUSY));
}

static inline void ocfs2_wait_on_refreshing_lock(struct ocfs2_lock_res *lockres)

{
	wait_event(lockres->l_event,
		   !ocfs2_check_wait_flag(lockres, OCFS2_LOCK_REFRESHING));
}

static inline int ocfs2_may_continue_on_blocked_lock(struct ocfs2_lock_res *lockres,
						     int wanted)
{
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BLOCKED));

	return wanted <= ocfs2_highest_compat_lock_level(lockres->l_blocking);
}

static void ocfs2_init_mask_waiter(struct ocfs2_mask_waiter *mw)
{
	INIT_LIST_HEAD(&mw->mw_item);
	init_completion(&mw->mw_complete);
	ocfs2_init_start_time(mw);
}

static int ocfs2_wait_for_mask(struct ocfs2_mask_waiter *mw)
{
	wait_for_completion(&mw->mw_complete);
	
	INIT_COMPLETION(mw->mw_complete);
	return mw->mw_status;
}

static void lockres_add_mask_waiter(struct ocfs2_lock_res *lockres,
				    struct ocfs2_mask_waiter *mw,
				    unsigned long mask,
				    unsigned long goal)
{
	BUG_ON(!list_empty(&mw->mw_item));

	assert_spin_locked(&lockres->l_lock);

	list_add_tail(&mw->mw_item, &lockres->l_mask_waiters);
	mw->mw_mask = mask;
	mw->mw_goal = goal;
}

static int lockres_remove_mask_waiter(struct ocfs2_lock_res *lockres,
				      struct ocfs2_mask_waiter *mw)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&lockres->l_lock, flags);
	if (!list_empty(&mw->mw_item)) {
		if ((lockres->l_flags & mw->mw_mask) != mw->mw_goal)
			ret = -EBUSY;

		list_del_init(&mw->mw_item);
		init_completion(&mw->mw_complete);
	}
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	return ret;

}

static int ocfs2_wait_for_mask_interruptible(struct ocfs2_mask_waiter *mw,
					     struct ocfs2_lock_res *lockres)
{
	int ret;

	ret = wait_for_completion_interruptible(&mw->mw_complete);
	if (ret)
		lockres_remove_mask_waiter(lockres, mw);
	else
		ret = mw->mw_status;
	
	INIT_COMPLETION(mw->mw_complete);
	return ret;
}

static int __ocfs2_cluster_lock(struct ocfs2_super *osb,
				struct ocfs2_lock_res *lockres,
				int level,
				u32 lkm_flags,
				int arg_flags,
				int l_subclass,
				unsigned long caller_ip)
{
	struct ocfs2_mask_waiter mw;
	int wait, catch_signals = !(osb->s_mount_opt & OCFS2_MOUNT_NOINTR);
	int ret = 0; 
	unsigned long flags;
	unsigned int gen;
	int noqueue_attempted = 0;

	ocfs2_init_mask_waiter(&mw);

	if (lockres->l_ops->flags & LOCK_TYPE_USES_LVB)
		lkm_flags |= DLM_LKF_VALBLK;

again:
	wait = 0;

	spin_lock_irqsave(&lockres->l_lock, flags);

	if (catch_signals && signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto unlock;
	}

	mlog_bug_on_msg(lockres->l_flags & OCFS2_LOCK_FREEING,
			"Cluster lock called on freeing lockres %s! flags "
			"0x%lx\n", lockres->l_name, lockres->l_flags);

	if (lockres->l_flags & OCFS2_LOCK_BUSY &&
	    level > lockres->l_level) {
		lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BUSY, 0);
		wait = 1;
		goto unlock;
	}

	if (lockres->l_flags & OCFS2_LOCK_UPCONVERT_FINISHING) {
		if (level <= lockres->l_level)
			goto update_holders;
	}

	if (lockres->l_flags & OCFS2_LOCK_BLOCKED &&
	    !ocfs2_may_continue_on_blocked_lock(lockres, level)) {
		lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BLOCKED, 0);
		wait = 1;
		goto unlock;
	}

	if (level > lockres->l_level) {
		if (noqueue_attempted > 0) {
			ret = -EAGAIN;
			goto unlock;
		}
		if (lkm_flags & DLM_LKF_NOQUEUE)
			noqueue_attempted = 1;

		if (lockres->l_action != OCFS2_AST_INVALID)
			mlog(ML_ERROR, "lockres %s has action %u pending\n",
			     lockres->l_name, lockres->l_action);

		if (!(lockres->l_flags & OCFS2_LOCK_ATTACHED)) {
			lockres->l_action = OCFS2_AST_ATTACH;
			lkm_flags &= ~DLM_LKF_CONVERT;
		} else {
			lockres->l_action = OCFS2_AST_CONVERT;
			lkm_flags |= DLM_LKF_CONVERT;
		}

		lockres->l_requested = level;
		lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
		gen = lockres_set_pending(lockres);
		spin_unlock_irqrestore(&lockres->l_lock, flags);

		BUG_ON(level == DLM_LOCK_IV);
		BUG_ON(level == DLM_LOCK_NL);

		mlog(ML_BASTS, "lockres %s, convert from %d to %d\n",
		     lockres->l_name, lockres->l_level, level);

		
		ret = ocfs2_dlm_lock(osb->cconn,
				     level,
				     &lockres->l_lksb,
				     lkm_flags,
				     lockres->l_name,
				     OCFS2_LOCK_ID_MAX_LEN - 1);
		lockres_clear_pending(lockres, gen, osb);
		if (ret) {
			if (!(lkm_flags & DLM_LKF_NOQUEUE) ||
			    (ret != -EAGAIN)) {
				ocfs2_log_dlm_error("ocfs2_dlm_lock",
						    ret, lockres);
			}
			ocfs2_recover_from_dlm_error(lockres, 1);
			goto out;
		}

		mlog(0, "lock %s, successful return from ocfs2_dlm_lock\n",
		     lockres->l_name);

		catch_signals = 0;

		
		goto again;
	}

update_holders:
	
	ocfs2_inc_holders(lockres, level);

	ret = 0;
unlock:
	lockres_clear_flags(lockres, OCFS2_LOCK_UPCONVERT_FINISHING);

	spin_unlock_irqrestore(&lockres->l_lock, flags);
out:
	if (wait && arg_flags & OCFS2_LOCK_NONBLOCK &&
	    mw.mw_mask & (OCFS2_LOCK_BUSY|OCFS2_LOCK_BLOCKED)) {
		wait = 0;
		if (lockres_remove_mask_waiter(lockres, &mw))
			ret = -EAGAIN;
		else
			goto again;
	}
	if (wait) {
		ret = ocfs2_wait_for_mask(&mw);
		if (ret == 0)
			goto again;
		mlog_errno(ret);
	}
	ocfs2_update_lock_stats(lockres, level, &mw, ret);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (!ret && lockres->l_lockdep_map.key != NULL) {
		if (level == DLM_LOCK_PR)
			rwsem_acquire_read(&lockres->l_lockdep_map, l_subclass,
				!!(arg_flags & OCFS2_META_LOCK_NOQUEUE),
				caller_ip);
		else
			rwsem_acquire(&lockres->l_lockdep_map, l_subclass,
				!!(arg_flags & OCFS2_META_LOCK_NOQUEUE),
				caller_ip);
	}
#endif
	return ret;
}

static inline int ocfs2_cluster_lock(struct ocfs2_super *osb,
				     struct ocfs2_lock_res *lockres,
				     int level,
				     u32 lkm_flags,
				     int arg_flags)
{
	return __ocfs2_cluster_lock(osb, lockres, level, lkm_flags, arg_flags,
				    0, _RET_IP_);
}


static void __ocfs2_cluster_unlock(struct ocfs2_super *osb,
				   struct ocfs2_lock_res *lockres,
				   int level,
				   unsigned long caller_ip)
{
	unsigned long flags;

	spin_lock_irqsave(&lockres->l_lock, flags);
	ocfs2_dec_holders(lockres, level);
	ocfs2_downconvert_on_unlock(osb, lockres);
	spin_unlock_irqrestore(&lockres->l_lock, flags);
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (lockres->l_lockdep_map.key != NULL)
		rwsem_release(&lockres->l_lockdep_map, 1, caller_ip);
#endif
}

static int ocfs2_create_new_lock(struct ocfs2_super *osb,
				 struct ocfs2_lock_res *lockres,
				 int ex,
				 int local)
{
	int level =  ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	unsigned long flags;
	u32 lkm_flags = local ? DLM_LKF_LOCAL : 0;

	spin_lock_irqsave(&lockres->l_lock, flags);
	BUG_ON(lockres->l_flags & OCFS2_LOCK_ATTACHED);
	lockres_or_flags(lockres, OCFS2_LOCK_LOCAL);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	return ocfs2_lock_create(osb, lockres, level, lkm_flags);
}

int ocfs2_create_new_inode_locks(struct inode *inode)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	BUG_ON(!inode);
	BUG_ON(!ocfs2_inode_is_new(inode));

	mlog(0, "Inode %llu\n", (unsigned long long)OCFS2_I(inode)->ip_blkno);


	ret = ocfs2_create_new_lock(osb, &OCFS2_I(inode)->ip_rw_lockres, 1, 1);
	if (ret) {
		mlog_errno(ret);
		goto bail;
	}

	ret = ocfs2_create_new_lock(osb, &OCFS2_I(inode)->ip_inode_lockres, 1, 0);
	if (ret) {
		mlog_errno(ret);
		goto bail;
	}

	ret = ocfs2_create_new_lock(osb, &OCFS2_I(inode)->ip_open_lockres, 0, 0);
	if (ret) {
		mlog_errno(ret);
		goto bail;
	}

bail:
	return ret;
}

int ocfs2_rw_lock(struct inode *inode, int write)
{
	int status, level;
	struct ocfs2_lock_res *lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	BUG_ON(!inode);

	mlog(0, "inode %llu take %s RW lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     write ? "EXMODE" : "PRMODE");

	if (ocfs2_mount_local(osb))
		return 0;

	lockres = &OCFS2_I(inode)->ip_rw_lockres;

	level = write ? DLM_LOCK_EX : DLM_LOCK_PR;

	status = ocfs2_cluster_lock(OCFS2_SB(inode->i_sb), lockres, level, 0,
				    0);
	if (status < 0)
		mlog_errno(status);

	return status;
}

void ocfs2_rw_unlock(struct inode *inode, int write)
{
	int level = write ? DLM_LOCK_EX : DLM_LOCK_PR;
	struct ocfs2_lock_res *lockres = &OCFS2_I(inode)->ip_rw_lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog(0, "inode %llu drop %s RW lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     write ? "EXMODE" : "PRMODE");

	if (!ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(OCFS2_SB(inode->i_sb), lockres, level);
}

int ocfs2_open_lock(struct inode *inode)
{
	int status = 0;
	struct ocfs2_lock_res *lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	BUG_ON(!inode);

	mlog(0, "inode %llu take PRMODE open lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno);

	if (ocfs2_is_hard_readonly(osb) || ocfs2_mount_local(osb))
		goto out;

	lockres = &OCFS2_I(inode)->ip_open_lockres;

	status = ocfs2_cluster_lock(OCFS2_SB(inode->i_sb), lockres,
				    DLM_LOCK_PR, 0, 0);
	if (status < 0)
		mlog_errno(status);

out:
	return status;
}

int ocfs2_try_open_lock(struct inode *inode, int write)
{
	int status = 0, level;
	struct ocfs2_lock_res *lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	BUG_ON(!inode);

	mlog(0, "inode %llu try to take %s open lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     write ? "EXMODE" : "PRMODE");

	if (ocfs2_is_hard_readonly(osb)) {
		if (write)
			status = -EROFS;
		goto out;
	}

	if (ocfs2_mount_local(osb))
		goto out;

	lockres = &OCFS2_I(inode)->ip_open_lockres;

	level = write ? DLM_LOCK_EX : DLM_LOCK_PR;

	status = ocfs2_cluster_lock(OCFS2_SB(inode->i_sb), lockres,
				    level, DLM_LKF_NOQUEUE, 0);

out:
	return status;
}

void ocfs2_open_unlock(struct inode *inode)
{
	struct ocfs2_lock_res *lockres = &OCFS2_I(inode)->ip_open_lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog(0, "inode %llu drop open lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno);

	if (ocfs2_mount_local(osb))
		goto out;

	if(lockres->l_ro_holders)
		ocfs2_cluster_unlock(OCFS2_SB(inode->i_sb), lockres,
				     DLM_LOCK_PR);
	if(lockres->l_ex_holders)
		ocfs2_cluster_unlock(OCFS2_SB(inode->i_sb), lockres,
				     DLM_LOCK_EX);

out:
	return;
}

static int ocfs2_flock_handle_signal(struct ocfs2_lock_res *lockres,
				     int level)
{
	int ret;
	struct ocfs2_super *osb = ocfs2_get_lockres_osb(lockres);
	unsigned long flags;
	struct ocfs2_mask_waiter mw;

	ocfs2_init_mask_waiter(&mw);

retry_cancel:
	spin_lock_irqsave(&lockres->l_lock, flags);
	if (lockres->l_flags & OCFS2_LOCK_BUSY) {
		ret = ocfs2_prepare_cancel_convert(osb, lockres);
		if (ret) {
			spin_unlock_irqrestore(&lockres->l_lock, flags);
			ret = ocfs2_cancel_convert(osb, lockres);
			if (ret < 0) {
				mlog_errno(ret);
				goto out;
			}
			goto retry_cancel;
		}
		lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BUSY, 0);
		spin_unlock_irqrestore(&lockres->l_lock, flags);

		ocfs2_wait_for_mask(&mw);
		goto retry_cancel;
	}

	ret = -ERESTARTSYS;
	if (lockres->l_level == level)
		ret = 0;

	mlog(0, "Cancel returning %d. flags: 0x%lx, level: %d, act: %d\n", ret,
	     lockres->l_flags, lockres->l_level, lockres->l_action);

	spin_unlock_irqrestore(&lockres->l_lock, flags);

out:
	return ret;
}

int ocfs2_file_lock(struct file *file, int ex, int trylock)
{
	int ret, level = ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	unsigned int lkm_flags = trylock ? DLM_LKF_NOQUEUE : 0;
	unsigned long flags;
	struct ocfs2_file_private *fp = file->private_data;
	struct ocfs2_lock_res *lockres = &fp->fp_flock;
	struct ocfs2_super *osb = OCFS2_SB(file->f_mapping->host->i_sb);
	struct ocfs2_mask_waiter mw;

	ocfs2_init_mask_waiter(&mw);

	if ((lockres->l_flags & OCFS2_LOCK_BUSY) ||
	    (lockres->l_level > DLM_LOCK_NL)) {
		mlog(ML_ERROR,
		     "File lock \"%s\" has busy or locked state: flags: 0x%lx, "
		     "level: %u\n", lockres->l_name, lockres->l_flags,
		     lockres->l_level);
		return -EINVAL;
	}

	spin_lock_irqsave(&lockres->l_lock, flags);
	if (!(lockres->l_flags & OCFS2_LOCK_ATTACHED)) {
		lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BUSY, 0);
		spin_unlock_irqrestore(&lockres->l_lock, flags);

		ret = ocfs2_lock_create(osb, lockres, DLM_LOCK_NL, 0);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_wait_for_mask(&mw);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
		spin_lock_irqsave(&lockres->l_lock, flags);
	}

	lockres->l_action = OCFS2_AST_CONVERT;
	lkm_flags |= DLM_LKF_CONVERT;
	lockres->l_requested = level;
	lockres_or_flags(lockres, OCFS2_LOCK_BUSY);

	lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BUSY, 0);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	ret = ocfs2_dlm_lock(osb->cconn, level, &lockres->l_lksb, lkm_flags,
			     lockres->l_name, OCFS2_LOCK_ID_MAX_LEN - 1);
	if (ret) {
		if (!trylock || (ret != -EAGAIN)) {
			ocfs2_log_dlm_error("ocfs2_dlm_lock", ret, lockres);
			ret = -EINVAL;
		}

		ocfs2_recover_from_dlm_error(lockres, 1);
		lockres_remove_mask_waiter(lockres, &mw);
		goto out;
	}

	ret = ocfs2_wait_for_mask_interruptible(&mw, lockres);
	if (ret == -ERESTARTSYS) {
		/*
		 * Userspace can cause deadlock itself with
		 * flock(). Current behavior locally is to allow the
		 * deadlock, but abort the system call if a signal is
		 * received. We follow this example, otherwise a
		 * poorly written program could sit in kernel until
		 * reboot.
		 *
		 * Handling this is a bit more complicated for Ocfs2
		 * though. We can't exit this function with an
		 * outstanding lock request, so a cancel convert is
		 * required. We intentionally overwrite 'ret' - if the
		 * cancel fails and the lock was granted, it's easier
		 * to just bubble success back up to the user.
		 */
		ret = ocfs2_flock_handle_signal(lockres, level);
	} else if (!ret && (level > lockres->l_level)) {
		
		BUG_ON(!trylock);
		ret = -EAGAIN;
	}

out:

	mlog(0, "Lock: \"%s\" ex: %d, trylock: %d, returns: %d\n",
	     lockres->l_name, ex, trylock, ret);
	return ret;
}

void ocfs2_file_unlock(struct file *file)
{
	int ret;
	unsigned int gen;
	unsigned long flags;
	struct ocfs2_file_private *fp = file->private_data;
	struct ocfs2_lock_res *lockres = &fp->fp_flock;
	struct ocfs2_super *osb = OCFS2_SB(file->f_mapping->host->i_sb);
	struct ocfs2_mask_waiter mw;

	ocfs2_init_mask_waiter(&mw);

	if (!(lockres->l_flags & OCFS2_LOCK_ATTACHED))
		return;

	if (lockres->l_level == DLM_LOCK_NL)
		return;

	mlog(0, "Unlock: \"%s\" flags: 0x%lx, level: %d, act: %d\n",
	     lockres->l_name, lockres->l_flags, lockres->l_level,
	     lockres->l_action);

	spin_lock_irqsave(&lockres->l_lock, flags);
	lockres_or_flags(lockres, OCFS2_LOCK_BLOCKED);
	lockres->l_blocking = DLM_LOCK_EX;

	gen = ocfs2_prepare_downconvert(lockres, DLM_LOCK_NL);
	lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BUSY, 0);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	ret = ocfs2_downconvert_lock(osb, lockres, DLM_LOCK_NL, 0, gen);
	if (ret) {
		mlog_errno(ret);
		return;
	}

	ret = ocfs2_wait_for_mask(&mw);
	if (ret)
		mlog_errno(ret);
}

static void ocfs2_downconvert_on_unlock(struct ocfs2_super *osb,
					struct ocfs2_lock_res *lockres)
{
	int kick = 0;

	if (lockres->l_flags & OCFS2_LOCK_BLOCKED) {
		switch(lockres->l_blocking) {
		case DLM_LOCK_EX:
			if (!lockres->l_ex_holders && !lockres->l_ro_holders)
				kick = 1;
			break;
		case DLM_LOCK_PR:
			if (!lockres->l_ex_holders)
				kick = 1;
			break;
		default:
			BUG();
		}
	}

	if (kick)
		ocfs2_wake_downconvert_thread(osb);
}

#define OCFS2_SEC_BITS   34
#define OCFS2_SEC_SHIFT  (64 - 34)
#define OCFS2_NSEC_MASK  ((1ULL << OCFS2_SEC_SHIFT) - 1)

static u64 ocfs2_pack_timespec(struct timespec *spec)
{
	u64 res;
	u64 sec = spec->tv_sec;
	u32 nsec = spec->tv_nsec;

	res = (sec << OCFS2_SEC_SHIFT) | (nsec & OCFS2_NSEC_MASK);

	return res;
}

static void __ocfs2_stuff_meta_lvb(struct inode *inode)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_lock_res *lockres = &oi->ip_inode_lockres;
	struct ocfs2_meta_lvb *lvb;

	lvb = ocfs2_dlm_lvb(&lockres->l_lksb);

	if (oi->ip_flags & OCFS2_INODE_DELETED) {
		lvb->lvb_version = 0;
		goto out;
	}

	lvb->lvb_version   = OCFS2_LVB_VERSION;
	lvb->lvb_isize	   = cpu_to_be64(i_size_read(inode));
	lvb->lvb_iclusters = cpu_to_be32(oi->ip_clusters);
	lvb->lvb_iuid      = cpu_to_be32(inode->i_uid);
	lvb->lvb_igid      = cpu_to_be32(inode->i_gid);
	lvb->lvb_imode     = cpu_to_be16(inode->i_mode);
	lvb->lvb_inlink    = cpu_to_be16(inode->i_nlink);
	lvb->lvb_iatime_packed  =
		cpu_to_be64(ocfs2_pack_timespec(&inode->i_atime));
	lvb->lvb_ictime_packed =
		cpu_to_be64(ocfs2_pack_timespec(&inode->i_ctime));
	lvb->lvb_imtime_packed =
		cpu_to_be64(ocfs2_pack_timespec(&inode->i_mtime));
	lvb->lvb_iattr    = cpu_to_be32(oi->ip_attr);
	lvb->lvb_idynfeatures = cpu_to_be16(oi->ip_dyn_features);
	lvb->lvb_igeneration = cpu_to_be32(inode->i_generation);

out:
	mlog_meta_lvb(0, lockres);
}

static void ocfs2_unpack_timespec(struct timespec *spec,
				  u64 packed_time)
{
	spec->tv_sec = packed_time >> OCFS2_SEC_SHIFT;
	spec->tv_nsec = packed_time & OCFS2_NSEC_MASK;
}

static void ocfs2_refresh_inode_from_lvb(struct inode *inode)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_lock_res *lockres = &oi->ip_inode_lockres;
	struct ocfs2_meta_lvb *lvb;

	mlog_meta_lvb(0, lockres);

	lvb = ocfs2_dlm_lvb(&lockres->l_lksb);

	
	spin_lock(&oi->ip_lock);
	oi->ip_clusters = be32_to_cpu(lvb->lvb_iclusters);
	i_size_write(inode, be64_to_cpu(lvb->lvb_isize));

	oi->ip_attr = be32_to_cpu(lvb->lvb_iattr);
	oi->ip_dyn_features = be16_to_cpu(lvb->lvb_idynfeatures);
	ocfs2_set_inode_flags(inode);

	
	if (S_ISLNK(inode->i_mode) && !oi->ip_clusters)
		inode->i_blocks = 0;
	else
		inode->i_blocks = ocfs2_inode_sector_count(inode);

	inode->i_uid     = be32_to_cpu(lvb->lvb_iuid);
	inode->i_gid     = be32_to_cpu(lvb->lvb_igid);
	inode->i_mode    = be16_to_cpu(lvb->lvb_imode);
	set_nlink(inode, be16_to_cpu(lvb->lvb_inlink));
	ocfs2_unpack_timespec(&inode->i_atime,
			      be64_to_cpu(lvb->lvb_iatime_packed));
	ocfs2_unpack_timespec(&inode->i_mtime,
			      be64_to_cpu(lvb->lvb_imtime_packed));
	ocfs2_unpack_timespec(&inode->i_ctime,
			      be64_to_cpu(lvb->lvb_ictime_packed));
	spin_unlock(&oi->ip_lock);
}

static inline int ocfs2_meta_lvb_is_trustable(struct inode *inode,
					      struct ocfs2_lock_res *lockres)
{
	struct ocfs2_meta_lvb *lvb = ocfs2_dlm_lvb(&lockres->l_lksb);

	if (ocfs2_dlm_lvb_valid(&lockres->l_lksb)
	    && lvb->lvb_version == OCFS2_LVB_VERSION
	    && be32_to_cpu(lvb->lvb_igeneration) == inode->i_generation)
		return 1;
	return 0;
}

static int ocfs2_should_refresh_lock_res(struct ocfs2_lock_res *lockres)
{
	unsigned long flags;
	int status = 0;

refresh_check:
	spin_lock_irqsave(&lockres->l_lock, flags);
	if (!(lockres->l_flags & OCFS2_LOCK_NEEDS_REFRESH)) {
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		goto bail;
	}

	if (lockres->l_flags & OCFS2_LOCK_REFRESHING) {
		spin_unlock_irqrestore(&lockres->l_lock, flags);

		ocfs2_wait_on_refreshing_lock(lockres);
		goto refresh_check;
	}

	
	lockres_or_flags(lockres, OCFS2_LOCK_REFRESHING);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	status = 1;
bail:
	mlog(0, "status %d\n", status);
	return status;
}

static inline void ocfs2_complete_lock_res_refresh(struct ocfs2_lock_res *lockres,
						   int status)
{
	unsigned long flags;

	spin_lock_irqsave(&lockres->l_lock, flags);
	lockres_clear_flags(lockres, OCFS2_LOCK_REFRESHING);
	if (!status)
		lockres_clear_flags(lockres, OCFS2_LOCK_NEEDS_REFRESH);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	wake_up(&lockres->l_event);
}

static int ocfs2_inode_lock_update(struct inode *inode,
				  struct buffer_head **bh)
{
	int status = 0;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_lock_res *lockres = &oi->ip_inode_lockres;
	struct ocfs2_dinode *fe;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (ocfs2_mount_local(osb))
		goto bail;

	spin_lock(&oi->ip_lock);
	if (oi->ip_flags & OCFS2_INODE_DELETED) {
		mlog(0, "Orphaned inode %llu was deleted while we "
		     "were waiting on a lock. ip_flags = 0x%x\n",
		     (unsigned long long)oi->ip_blkno, oi->ip_flags);
		spin_unlock(&oi->ip_lock);
		status = -ENOENT;
		goto bail;
	}
	spin_unlock(&oi->ip_lock);

	if (!ocfs2_should_refresh_lock_res(lockres))
		goto bail;

	ocfs2_metadata_cache_purge(INODE_CACHE(inode));

	ocfs2_extent_map_trunc(inode, 0);

	if (ocfs2_meta_lvb_is_trustable(inode, lockres)) {
		mlog(0, "Trusting LVB on inode %llu\n",
		     (unsigned long long)oi->ip_blkno);
		ocfs2_refresh_inode_from_lvb(inode);
	} else {
		
		
		status = ocfs2_read_inode_block(inode, bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail_refresh;
		}
		fe = (struct ocfs2_dinode *) (*bh)->b_data;

		mlog_bug_on_msg(inode->i_generation !=
				le32_to_cpu(fe->i_generation),
				"Invalid dinode %llu disk generation: %u "
				"inode->i_generation: %u\n",
				(unsigned long long)oi->ip_blkno,
				le32_to_cpu(fe->i_generation),
				inode->i_generation);
		mlog_bug_on_msg(le64_to_cpu(fe->i_dtime) ||
				!(fe->i_flags & cpu_to_le32(OCFS2_VALID_FL)),
				"Stale dinode %llu dtime: %llu flags: 0x%x\n",
				(unsigned long long)oi->ip_blkno,
				(unsigned long long)le64_to_cpu(fe->i_dtime),
				le32_to_cpu(fe->i_flags));

		ocfs2_refresh_inode(inode, fe);
		ocfs2_track_lock_refresh(lockres);
	}

	status = 0;
bail_refresh:
	ocfs2_complete_lock_res_refresh(lockres, status);
bail:
	return status;
}

static int ocfs2_assign_bh(struct inode *inode,
			   struct buffer_head **ret_bh,
			   struct buffer_head *passed_bh)
{
	int status;

	if (passed_bh) {
		*ret_bh = passed_bh;
		get_bh(*ret_bh);

		return 0;
	}

	status = ocfs2_read_inode_block(inode, ret_bh);
	if (status < 0)
		mlog_errno(status);

	return status;
}

int ocfs2_inode_lock_full_nested(struct inode *inode,
				 struct buffer_head **ret_bh,
				 int ex,
				 int arg_flags,
				 int subclass)
{
	int status, level, acquired;
	u32 dlm_flags;
	struct ocfs2_lock_res *lockres = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct buffer_head *local_bh = NULL;

	BUG_ON(!inode);

	mlog(0, "inode %llu, take %s META lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     ex ? "EXMODE" : "PRMODE");

	status = 0;
	acquired = 0;
	if (ocfs2_is_hard_readonly(osb)) {
		if (ex)
			status = -EROFS;
		goto getbh;
	}

	if (ocfs2_mount_local(osb))
		goto local;

	if (!(arg_flags & OCFS2_META_LOCK_RECOVERY))
		ocfs2_wait_for_recovery(osb);

	lockres = &OCFS2_I(inode)->ip_inode_lockres;
	level = ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	dlm_flags = 0;
	if (arg_flags & OCFS2_META_LOCK_NOQUEUE)
		dlm_flags |= DLM_LKF_NOQUEUE;

	status = __ocfs2_cluster_lock(osb, lockres, level, dlm_flags,
				      arg_flags, subclass, _RET_IP_);
	if (status < 0) {
		if (status != -EAGAIN && status != -EIOCBRETRY)
			mlog_errno(status);
		goto bail;
	}

	
	acquired = 1;

	if (!(arg_flags & OCFS2_META_LOCK_RECOVERY))
		ocfs2_wait_for_recovery(osb);

local:
	if (inode->i_state & I_NEW) {
		status = 0;
		if (lockres)
			ocfs2_complete_lock_res_refresh(lockres, 0);
		goto bail;
	}

	status = ocfs2_inode_lock_update(inode, &local_bh);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		goto bail;
	}
getbh:
	if (ret_bh) {
		status = ocfs2_assign_bh(inode, ret_bh, local_bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

bail:
	if (status < 0) {
		if (ret_bh && (*ret_bh)) {
			brelse(*ret_bh);
			*ret_bh = NULL;
		}
		if (acquired)
			ocfs2_inode_unlock(inode, ex);
	}

	if (local_bh)
		brelse(local_bh);

	return status;
}

int ocfs2_inode_lock_with_page(struct inode *inode,
			      struct buffer_head **ret_bh,
			      int ex,
			      struct page *page)
{
	int ret;

	ret = ocfs2_inode_lock_full(inode, ret_bh, ex, OCFS2_LOCK_NONBLOCK);
	if (ret == -EAGAIN) {
		unlock_page(page);
		if (ocfs2_inode_lock(inode, ret_bh, ex) == 0)
			ocfs2_inode_unlock(inode, ex);
		ret = AOP_TRUNCATED_PAGE;
	}

	return ret;
}

int ocfs2_inode_lock_atime(struct inode *inode,
			  struct vfsmount *vfsmnt,
			  int *level)
{
	int ret;

	ret = ocfs2_inode_lock(inode, NULL, 0);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}

	if (ocfs2_should_update_atime(inode, vfsmnt)) {
		struct buffer_head *bh = NULL;

		ocfs2_inode_unlock(inode, 0);
		ret = ocfs2_inode_lock(inode, &bh, 1);
		if (ret < 0) {
			mlog_errno(ret);
			return ret;
		}
		*level = 1;
		if (ocfs2_should_update_atime(inode, vfsmnt))
			ocfs2_update_inode_atime(inode, bh);
		if (bh)
			brelse(bh);
	} else
		*level = 0;

	return ret;
}

void ocfs2_inode_unlock(struct inode *inode,
		       int ex)
{
	int level = ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	struct ocfs2_lock_res *lockres = &OCFS2_I(inode)->ip_inode_lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog(0, "inode %llu drop %s META lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     ex ? "EXMODE" : "PRMODE");

	if (!ocfs2_is_hard_readonly(OCFS2_SB(inode->i_sb)) &&
	    !ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(OCFS2_SB(inode->i_sb), lockres, level);
}

int ocfs2_orphan_scan_lock(struct ocfs2_super *osb, u32 *seqno)
{
	struct ocfs2_lock_res *lockres;
	struct ocfs2_orphan_scan_lvb *lvb;
	int status = 0;

	if (ocfs2_is_hard_readonly(osb))
		return -EROFS;

	if (ocfs2_mount_local(osb))
		return 0;

	lockres = &osb->osb_orphan_scan.os_lockres;
	status = ocfs2_cluster_lock(osb, lockres, DLM_LOCK_EX, 0, 0);
	if (status < 0)
		return status;

	lvb = ocfs2_dlm_lvb(&lockres->l_lksb);
	if (ocfs2_dlm_lvb_valid(&lockres->l_lksb) &&
	    lvb->lvb_version == OCFS2_ORPHAN_LVB_VERSION)
		*seqno = be32_to_cpu(lvb->lvb_os_seqno);
	else
		*seqno = osb->osb_orphan_scan.os_seqno + 1;

	return status;
}

void ocfs2_orphan_scan_unlock(struct ocfs2_super *osb, u32 seqno)
{
	struct ocfs2_lock_res *lockres;
	struct ocfs2_orphan_scan_lvb *lvb;

	if (!ocfs2_is_hard_readonly(osb) && !ocfs2_mount_local(osb)) {
		lockres = &osb->osb_orphan_scan.os_lockres;
		lvb = ocfs2_dlm_lvb(&lockres->l_lksb);
		lvb->lvb_version = OCFS2_ORPHAN_LVB_VERSION;
		lvb->lvb_os_seqno = cpu_to_be32(seqno);
		ocfs2_cluster_unlock(osb, lockres, DLM_LOCK_EX);
	}
}

int ocfs2_super_lock(struct ocfs2_super *osb,
		     int ex)
{
	int status = 0;
	int level = ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	struct ocfs2_lock_res *lockres = &osb->osb_super_lockres;

	if (ocfs2_is_hard_readonly(osb))
		return -EROFS;

	if (ocfs2_mount_local(osb))
		goto bail;

	status = ocfs2_cluster_lock(osb, lockres, level, 0, 0);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_should_refresh_lock_res(lockres);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	if (status) {
		status = ocfs2_refresh_slot_info(osb);

		ocfs2_complete_lock_res_refresh(lockres, status);

		if (status < 0)
			mlog_errno(status);
		ocfs2_track_lock_refresh(lockres);
	}
bail:
	return status;
}

void ocfs2_super_unlock(struct ocfs2_super *osb,
			int ex)
{
	int level = ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	struct ocfs2_lock_res *lockres = &osb->osb_super_lockres;

	if (!ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(osb, lockres, level);
}

int ocfs2_rename_lock(struct ocfs2_super *osb)
{
	int status;
	struct ocfs2_lock_res *lockres = &osb->osb_rename_lockres;

	if (ocfs2_is_hard_readonly(osb))
		return -EROFS;

	if (ocfs2_mount_local(osb))
		return 0;

	status = ocfs2_cluster_lock(osb, lockres, DLM_LOCK_EX, 0, 0);
	if (status < 0)
		mlog_errno(status);

	return status;
}

void ocfs2_rename_unlock(struct ocfs2_super *osb)
{
	struct ocfs2_lock_res *lockres = &osb->osb_rename_lockres;

	if (!ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(osb, lockres, DLM_LOCK_EX);
}

int ocfs2_nfs_sync_lock(struct ocfs2_super *osb, int ex)
{
	int status;
	struct ocfs2_lock_res *lockres = &osb->osb_nfs_sync_lockres;

	if (ocfs2_is_hard_readonly(osb))
		return -EROFS;

	if (ocfs2_mount_local(osb))
		return 0;

	status = ocfs2_cluster_lock(osb, lockres, ex ? LKM_EXMODE : LKM_PRMODE,
				    0, 0);
	if (status < 0)
		mlog(ML_ERROR, "lock on nfs sync lock failed %d\n", status);

	return status;
}

void ocfs2_nfs_sync_unlock(struct ocfs2_super *osb, int ex)
{
	struct ocfs2_lock_res *lockres = &osb->osb_nfs_sync_lockres;

	if (!ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(osb, lockres,
				     ex ? LKM_EXMODE : LKM_PRMODE);
}

int ocfs2_dentry_lock(struct dentry *dentry, int ex)
{
	int ret;
	int level = ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	struct ocfs2_dentry_lock *dl = dentry->d_fsdata;
	struct ocfs2_super *osb = OCFS2_SB(dentry->d_sb);

	BUG_ON(!dl);

	if (ocfs2_is_hard_readonly(osb)) {
		if (ex)
			return -EROFS;
		return 0;
	}

	if (ocfs2_mount_local(osb))
		return 0;

	ret = ocfs2_cluster_lock(osb, &dl->dl_lockres, level, 0, 0);
	if (ret < 0)
		mlog_errno(ret);

	return ret;
}

void ocfs2_dentry_unlock(struct dentry *dentry, int ex)
{
	int level = ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	struct ocfs2_dentry_lock *dl = dentry->d_fsdata;
	struct ocfs2_super *osb = OCFS2_SB(dentry->d_sb);

	if (!ocfs2_is_hard_readonly(osb) && !ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(osb, &dl->dl_lockres, level);
}

static void ocfs2_dlm_debug_free(struct kref *kref)
{
	struct ocfs2_dlm_debug *dlm_debug;

	dlm_debug = container_of(kref, struct ocfs2_dlm_debug, d_refcnt);

	kfree(dlm_debug);
}

void ocfs2_put_dlm_debug(struct ocfs2_dlm_debug *dlm_debug)
{
	if (dlm_debug)
		kref_put(&dlm_debug->d_refcnt, ocfs2_dlm_debug_free);
}

static void ocfs2_get_dlm_debug(struct ocfs2_dlm_debug *debug)
{
	kref_get(&debug->d_refcnt);
}

struct ocfs2_dlm_debug *ocfs2_new_dlm_debug(void)
{
	struct ocfs2_dlm_debug *dlm_debug;

	dlm_debug = kmalloc(sizeof(struct ocfs2_dlm_debug), GFP_KERNEL);
	if (!dlm_debug) {
		mlog_errno(-ENOMEM);
		goto out;
	}

	kref_init(&dlm_debug->d_refcnt);
	INIT_LIST_HEAD(&dlm_debug->d_lockres_tracking);
	dlm_debug->d_locking_state = NULL;
out:
	return dlm_debug;
}

struct ocfs2_dlm_seq_priv {
	struct ocfs2_dlm_debug *p_dlm_debug;
	struct ocfs2_lock_res p_iter_res;
	struct ocfs2_lock_res p_tmp_res;
};

static struct ocfs2_lock_res *ocfs2_dlm_next_res(struct ocfs2_lock_res *start,
						 struct ocfs2_dlm_seq_priv *priv)
{
	struct ocfs2_lock_res *iter, *ret = NULL;
	struct ocfs2_dlm_debug *dlm_debug = priv->p_dlm_debug;

	assert_spin_locked(&ocfs2_dlm_tracking_lock);

	list_for_each_entry(iter, &start->l_debug_list, l_debug_list) {
		
		if (&iter->l_debug_list == &dlm_debug->d_lockres_tracking) {
			mlog(0, "End of list found, %p\n", ret);
			break;
		}

		if (iter->l_ops != NULL) {
			ret = iter;
			break;
		}
	}

	return ret;
}

static void *ocfs2_dlm_seq_start(struct seq_file *m, loff_t *pos)
{
	struct ocfs2_dlm_seq_priv *priv = m->private;
	struct ocfs2_lock_res *iter;

	spin_lock(&ocfs2_dlm_tracking_lock);
	iter = ocfs2_dlm_next_res(&priv->p_iter_res, priv);
	if (iter) {
		priv->p_tmp_res = *iter;
		iter = &priv->p_tmp_res;
	}
	spin_unlock(&ocfs2_dlm_tracking_lock);

	return iter;
}

static void ocfs2_dlm_seq_stop(struct seq_file *m, void *v)
{
}

static void *ocfs2_dlm_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ocfs2_dlm_seq_priv *priv = m->private;
	struct ocfs2_lock_res *iter = v;
	struct ocfs2_lock_res *dummy = &priv->p_iter_res;

	spin_lock(&ocfs2_dlm_tracking_lock);
	iter = ocfs2_dlm_next_res(iter, priv);
	list_del_init(&dummy->l_debug_list);
	if (iter) {
		list_add(&dummy->l_debug_list, &iter->l_debug_list);
		priv->p_tmp_res = *iter;
		iter = &priv->p_tmp_res;
	}
	spin_unlock(&ocfs2_dlm_tracking_lock);

	return iter;
}

#define OCFS2_DLM_DEBUG_STR_VERSION 3
static int ocfs2_dlm_seq_show(struct seq_file *m, void *v)
{
	int i;
	char *lvb;
	struct ocfs2_lock_res *lockres = v;

	if (!lockres)
		return -EINVAL;

	seq_printf(m, "0x%x\t", OCFS2_DLM_DEBUG_STR_VERSION);

	if (lockres->l_type == OCFS2_LOCK_TYPE_DENTRY)
		seq_printf(m, "%.*s%08x\t", OCFS2_DENTRY_LOCK_INO_START - 1,
			   lockres->l_name,
			   (unsigned int)ocfs2_get_dentry_lock_ino(lockres));
	else
		seq_printf(m, "%.*s\t", OCFS2_LOCK_ID_MAX_LEN, lockres->l_name);

	seq_printf(m, "%d\t"
		   "0x%lx\t"
		   "0x%x\t"
		   "0x%x\t"
		   "%u\t"
		   "%u\t"
		   "%d\t"
		   "%d\t",
		   lockres->l_level,
		   lockres->l_flags,
		   lockres->l_action,
		   lockres->l_unlock_action,
		   lockres->l_ro_holders,
		   lockres->l_ex_holders,
		   lockres->l_requested,
		   lockres->l_blocking);

	
	lvb = ocfs2_dlm_lvb(&lockres->l_lksb);
	for(i = 0; i < DLM_LVB_LEN; i++)
		seq_printf(m, "0x%x\t", lvb[i]);

#ifdef CONFIG_OCFS2_FS_STATS
# define lock_num_prmode(_l)		((_l)->l_lock_prmode.ls_gets)
# define lock_num_exmode(_l)		((_l)->l_lock_exmode.ls_gets)
# define lock_num_prmode_failed(_l)	((_l)->l_lock_prmode.ls_fail)
# define lock_num_exmode_failed(_l)	((_l)->l_lock_exmode.ls_fail)
# define lock_total_prmode(_l)		((_l)->l_lock_prmode.ls_total)
# define lock_total_exmode(_l)		((_l)->l_lock_exmode.ls_total)
# define lock_max_prmode(_l)		((_l)->l_lock_prmode.ls_max)
# define lock_max_exmode(_l)		((_l)->l_lock_exmode.ls_max)
# define lock_refresh(_l)		((_l)->l_lock_refresh)
#else
# define lock_num_prmode(_l)		(0)
# define lock_num_exmode(_l)		(0)
# define lock_num_prmode_failed(_l)	(0)
# define lock_num_exmode_failed(_l)	(0)
# define lock_total_prmode(_l)		(0ULL)
# define lock_total_exmode(_l)		(0ULL)
# define lock_max_prmode(_l)		(0)
# define lock_max_exmode(_l)		(0)
# define lock_refresh(_l)		(0)
#endif
	
	seq_printf(m, "%u\t"
		   "%u\t"
		   "%u\t"
		   "%u\t"
		   "%llu\t"
		   "%llu\t"
		   "%u\t"
		   "%u\t"
		   "%u\t",
		   lock_num_prmode(lockres),
		   lock_num_exmode(lockres),
		   lock_num_prmode_failed(lockres),
		   lock_num_exmode_failed(lockres),
		   lock_total_prmode(lockres),
		   lock_total_exmode(lockres),
		   lock_max_prmode(lockres),
		   lock_max_exmode(lockres),
		   lock_refresh(lockres));

	
	seq_printf(m, "\n");
	return 0;
}

static const struct seq_operations ocfs2_dlm_seq_ops = {
	.start =	ocfs2_dlm_seq_start,
	.stop =		ocfs2_dlm_seq_stop,
	.next =		ocfs2_dlm_seq_next,
	.show =		ocfs2_dlm_seq_show,
};

static int ocfs2_dlm_debug_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct ocfs2_dlm_seq_priv *priv = seq->private;
	struct ocfs2_lock_res *res = &priv->p_iter_res;

	ocfs2_remove_lockres_tracking(res);
	ocfs2_put_dlm_debug(priv->p_dlm_debug);
	return seq_release_private(inode, file);
}

static int ocfs2_dlm_debug_open(struct inode *inode, struct file *file)
{
	int ret;
	struct ocfs2_dlm_seq_priv *priv;
	struct seq_file *seq;
	struct ocfs2_super *osb;

	priv = kzalloc(sizeof(struct ocfs2_dlm_seq_priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}
	osb = inode->i_private;
	ocfs2_get_dlm_debug(osb->osb_dlm_debug);
	priv->p_dlm_debug = osb->osb_dlm_debug;
	INIT_LIST_HEAD(&priv->p_iter_res.l_debug_list);

	ret = seq_open(file, &ocfs2_dlm_seq_ops);
	if (ret) {
		kfree(priv);
		mlog_errno(ret);
		goto out;
	}

	seq = file->private_data;
	seq->private = priv;

	ocfs2_add_lockres_tracking(&priv->p_iter_res,
				   priv->p_dlm_debug);

out:
	return ret;
}

static const struct file_operations ocfs2_dlm_debug_fops = {
	.open =		ocfs2_dlm_debug_open,
	.release =	ocfs2_dlm_debug_release,
	.read =		seq_read,
	.llseek =	seq_lseek,
};

static int ocfs2_dlm_init_debug(struct ocfs2_super *osb)
{
	int ret = 0;
	struct ocfs2_dlm_debug *dlm_debug = osb->osb_dlm_debug;

	dlm_debug->d_locking_state = debugfs_create_file("locking_state",
							 S_IFREG|S_IRUSR,
							 osb->osb_debug_root,
							 osb,
							 &ocfs2_dlm_debug_fops);
	if (!dlm_debug->d_locking_state) {
		ret = -EINVAL;
		mlog(ML_ERROR,
		     "Unable to create locking state debugfs file.\n");
		goto out;
	}

	ocfs2_get_dlm_debug(dlm_debug);
out:
	return ret;
}

static void ocfs2_dlm_shutdown_debug(struct ocfs2_super *osb)
{
	struct ocfs2_dlm_debug *dlm_debug = osb->osb_dlm_debug;

	if (dlm_debug) {
		debugfs_remove(dlm_debug->d_locking_state);
		ocfs2_put_dlm_debug(dlm_debug);
	}
}

int ocfs2_dlm_init(struct ocfs2_super *osb)
{
	int status = 0;
	struct ocfs2_cluster_connection *conn = NULL;

	if (ocfs2_mount_local(osb)) {
		osb->node_num = 0;
		goto local;
	}

	status = ocfs2_dlm_init_debug(osb);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	
	osb->dc_task = kthread_run(ocfs2_downconvert_thread, osb, "ocfs2dc");
	if (IS_ERR(osb->dc_task)) {
		status = PTR_ERR(osb->dc_task);
		osb->dc_task = NULL;
		mlog_errno(status);
		goto bail;
	}

	
	status = ocfs2_cluster_connect(osb->osb_cluster_stack,
				       osb->uuid_str,
				       strlen(osb->uuid_str),
				       &lproto, ocfs2_do_node_down, osb,
				       &conn);
	if (status) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_cluster_this_node(&osb->node_num);
	if (status < 0) {
		mlog_errno(status);
		mlog(ML_ERROR,
		     "could not find this host's node number\n");
		ocfs2_cluster_disconnect(conn, 0);
		goto bail;
	}

local:
	ocfs2_super_lock_res_init(&osb->osb_super_lockres, osb);
	ocfs2_rename_lock_res_init(&osb->osb_rename_lockres, osb);
	ocfs2_nfs_sync_lock_res_init(&osb->osb_nfs_sync_lockres, osb);
	ocfs2_orphan_scan_lock_res_init(&osb->osb_orphan_scan.os_lockres, osb);

	osb->cconn = conn;

	status = 0;
bail:
	if (status < 0) {
		ocfs2_dlm_shutdown_debug(osb);
		if (osb->dc_task)
			kthread_stop(osb->dc_task);
	}

	return status;
}

void ocfs2_dlm_shutdown(struct ocfs2_super *osb,
			int hangup_pending)
{
	ocfs2_drop_osb_locks(osb);


	if (osb->dc_task) {
		kthread_stop(osb->dc_task);
		osb->dc_task = NULL;
	}

	ocfs2_lock_res_free(&osb->osb_super_lockres);
	ocfs2_lock_res_free(&osb->osb_rename_lockres);
	ocfs2_lock_res_free(&osb->osb_nfs_sync_lockres);
	ocfs2_lock_res_free(&osb->osb_orphan_scan.os_lockres);

	ocfs2_cluster_disconnect(osb->cconn, hangup_pending);
	osb->cconn = NULL;

	ocfs2_dlm_shutdown_debug(osb);
}

static int ocfs2_drop_lock(struct ocfs2_super *osb,
			   struct ocfs2_lock_res *lockres)
{
	int ret;
	unsigned long flags;
	u32 lkm_flags = 0;

	
	if (!(lockres->l_flags & OCFS2_LOCK_INITIALIZED))
		goto out;

	if (lockres->l_ops->flags & LOCK_TYPE_USES_LVB)
		lkm_flags |= DLM_LKF_VALBLK;

	spin_lock_irqsave(&lockres->l_lock, flags);

	mlog_bug_on_msg(!(lockres->l_flags & OCFS2_LOCK_FREEING),
			"lockres %s, flags 0x%lx\n",
			lockres->l_name, lockres->l_flags);

	while (lockres->l_flags & OCFS2_LOCK_BUSY) {
		mlog(0, "waiting on busy lock \"%s\": flags = %lx, action = "
		     "%u, unlock_action = %u\n",
		     lockres->l_name, lockres->l_flags, lockres->l_action,
		     lockres->l_unlock_action);

		spin_unlock_irqrestore(&lockres->l_lock, flags);

		ocfs2_wait_on_busy_lock(lockres);

		spin_lock_irqsave(&lockres->l_lock, flags);
	}

	if (lockres->l_ops->flags & LOCK_TYPE_USES_LVB) {
		if (lockres->l_flags & OCFS2_LOCK_ATTACHED &&
		    lockres->l_level == DLM_LOCK_EX &&
		    !(lockres->l_flags & OCFS2_LOCK_NEEDS_REFRESH))
			lockres->l_ops->set_lvb(lockres);
	}

	if (lockres->l_flags & OCFS2_LOCK_BUSY)
		mlog(ML_ERROR, "destroying busy lock: \"%s\"\n",
		     lockres->l_name);
	if (lockres->l_flags & OCFS2_LOCK_BLOCKED)
		mlog(0, "destroying blocked lock: \"%s\"\n", lockres->l_name);

	if (!(lockres->l_flags & OCFS2_LOCK_ATTACHED)) {
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		goto out;
	}

	lockres_clear_flags(lockres, OCFS2_LOCK_ATTACHED);

	BUG_ON(lockres->l_action != OCFS2_AST_INVALID);

	
	lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
	lockres->l_unlock_action = OCFS2_UNLOCK_DROP_LOCK;
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	mlog(0, "lock %s\n", lockres->l_name);

	ret = ocfs2_dlm_unlock(osb->cconn, &lockres->l_lksb, lkm_flags);
	if (ret) {
		ocfs2_log_dlm_error("ocfs2_dlm_unlock", ret, lockres);
		mlog(ML_ERROR, "lockres flags: %lu\n", lockres->l_flags);
		ocfs2_dlm_dump_lksb(&lockres->l_lksb);
		BUG();
	}
	mlog(0, "lock %s, successful return from ocfs2_dlm_unlock\n",
	     lockres->l_name);

	ocfs2_wait_on_busy_lock(lockres);
out:
	return 0;
}

void ocfs2_mark_lockres_freeing(struct ocfs2_lock_res *lockres)
{
	int status;
	struct ocfs2_mask_waiter mw;
	unsigned long flags;

	ocfs2_init_mask_waiter(&mw);

	spin_lock_irqsave(&lockres->l_lock, flags);
	lockres->l_flags |= OCFS2_LOCK_FREEING;
	while (lockres->l_flags & OCFS2_LOCK_QUEUED) {
		lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_QUEUED, 0);
		spin_unlock_irqrestore(&lockres->l_lock, flags);

		mlog(0, "Waiting on lockres %s\n", lockres->l_name);

		status = ocfs2_wait_for_mask(&mw);
		if (status)
			mlog_errno(status);

		spin_lock_irqsave(&lockres->l_lock, flags);
	}
	spin_unlock_irqrestore(&lockres->l_lock, flags);
}

void ocfs2_simple_drop_lockres(struct ocfs2_super *osb,
			       struct ocfs2_lock_res *lockres)
{
	int ret;

	ocfs2_mark_lockres_freeing(lockres);
	ret = ocfs2_drop_lock(osb, lockres);
	if (ret)
		mlog_errno(ret);
}

static void ocfs2_drop_osb_locks(struct ocfs2_super *osb)
{
	ocfs2_simple_drop_lockres(osb, &osb->osb_super_lockres);
	ocfs2_simple_drop_lockres(osb, &osb->osb_rename_lockres);
	ocfs2_simple_drop_lockres(osb, &osb->osb_nfs_sync_lockres);
	ocfs2_simple_drop_lockres(osb, &osb->osb_orphan_scan.os_lockres);
}

int ocfs2_drop_inode_locks(struct inode *inode)
{
	int status, err;


	err = ocfs2_drop_lock(OCFS2_SB(inode->i_sb),
			      &OCFS2_I(inode)->ip_open_lockres);
	if (err < 0)
		mlog_errno(err);

	status = err;

	err = ocfs2_drop_lock(OCFS2_SB(inode->i_sb),
			      &OCFS2_I(inode)->ip_inode_lockres);
	if (err < 0)
		mlog_errno(err);
	if (err < 0 && !status)
		status = err;

	err = ocfs2_drop_lock(OCFS2_SB(inode->i_sb),
			      &OCFS2_I(inode)->ip_rw_lockres);
	if (err < 0)
		mlog_errno(err);
	if (err < 0 && !status)
		status = err;

	return status;
}

static unsigned int ocfs2_prepare_downconvert(struct ocfs2_lock_res *lockres,
					      int new_level)
{
	assert_spin_locked(&lockres->l_lock);

	BUG_ON(lockres->l_blocking <= DLM_LOCK_NL);

	if (lockres->l_level <= new_level) {
		mlog(ML_ERROR, "lockres %s, lvl %d <= %d, blcklst %d, mask %d, "
		     "type %d, flags 0x%lx, hold %d %d, act %d %d, req %d, "
		     "block %d, pgen %d\n", lockres->l_name, lockres->l_level,
		     new_level, list_empty(&lockres->l_blocked_list),
		     list_empty(&lockres->l_mask_waiters), lockres->l_type,
		     lockres->l_flags, lockres->l_ro_holders,
		     lockres->l_ex_holders, lockres->l_action,
		     lockres->l_unlock_action, lockres->l_requested,
		     lockres->l_blocking, lockres->l_pending_gen);
		BUG();
	}

	mlog(ML_BASTS, "lockres %s, level %d => %d, blocking %d\n",
	     lockres->l_name, lockres->l_level, new_level, lockres->l_blocking);

	lockres->l_action = OCFS2_AST_DOWNCONVERT;
	lockres->l_requested = new_level;
	lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
	return lockres_set_pending(lockres);
}

static int ocfs2_downconvert_lock(struct ocfs2_super *osb,
				  struct ocfs2_lock_res *lockres,
				  int new_level,
				  int lvb,
				  unsigned int generation)
{
	int ret;
	u32 dlm_flags = DLM_LKF_CONVERT;

	mlog(ML_BASTS, "lockres %s, level %d => %d\n", lockres->l_name,
	     lockres->l_level, new_level);

	if (lvb)
		dlm_flags |= DLM_LKF_VALBLK;

	ret = ocfs2_dlm_lock(osb->cconn,
			     new_level,
			     &lockres->l_lksb,
			     dlm_flags,
			     lockres->l_name,
			     OCFS2_LOCK_ID_MAX_LEN - 1);
	lockres_clear_pending(lockres, generation, osb);
	if (ret) {
		ocfs2_log_dlm_error("ocfs2_dlm_lock", ret, lockres);
		ocfs2_recover_from_dlm_error(lockres, 1);
		goto bail;
	}

	ret = 0;
bail:
	return ret;
}

static int ocfs2_prepare_cancel_convert(struct ocfs2_super *osb,
				        struct ocfs2_lock_res *lockres)
{
	assert_spin_locked(&lockres->l_lock);

	if (lockres->l_unlock_action == OCFS2_UNLOCK_CANCEL_CONVERT) {
		mlog(ML_BASTS, "lockres %s, skip convert\n", lockres->l_name);
		return 0;
	}

	
	BUG_ON(lockres->l_action != OCFS2_AST_CONVERT &&
	       lockres->l_action != OCFS2_AST_DOWNCONVERT);
	lockres->l_unlock_action = OCFS2_UNLOCK_CANCEL_CONVERT;

	mlog_bug_on_msg(!(lockres->l_flags & OCFS2_LOCK_BUSY),
			"lock %s, invalid flags: 0x%lx\n",
			lockres->l_name, lockres->l_flags);

	mlog(ML_BASTS, "lockres %s\n", lockres->l_name);

	return 1;
}

static int ocfs2_cancel_convert(struct ocfs2_super *osb,
				struct ocfs2_lock_res *lockres)
{
	int ret;

	ret = ocfs2_dlm_unlock(osb->cconn, &lockres->l_lksb,
			       DLM_LKF_CANCEL);
	if (ret) {
		ocfs2_log_dlm_error("ocfs2_dlm_unlock", ret, lockres);
		ocfs2_recover_from_dlm_error(lockres, 0);
	}

	mlog(ML_BASTS, "lockres %s\n", lockres->l_name);

	return ret;
}

static int ocfs2_unblock_lock(struct ocfs2_super *osb,
			      struct ocfs2_lock_res *lockres,
			      struct ocfs2_unblock_ctl *ctl)
{
	unsigned long flags;
	int blocking;
	int new_level;
	int level;
	int ret = 0;
	int set_lvb = 0;
	unsigned int gen;

	spin_lock_irqsave(&lockres->l_lock, flags);

recheck:
	if (!(lockres->l_flags & OCFS2_LOCK_BLOCKED)) {
		BUG_ON(lockres->l_blocking != DLM_LOCK_NL);
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		ret = 0;
		goto leave;
	}

	if (lockres->l_flags & OCFS2_LOCK_BUSY) {
		if (lockres->l_flags & OCFS2_LOCK_PENDING) {
			mlog(ML_BASTS, "lockres %s, ReQ: Pending\n",
			     lockres->l_name);
			goto leave_requeue;
		}

		ctl->requeue = 1;
		ret = ocfs2_prepare_cancel_convert(osb, lockres);
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		if (ret) {
			ret = ocfs2_cancel_convert(osb, lockres);
			if (ret < 0)
				mlog_errno(ret);
		}
		goto leave;
	}

	if (lockres->l_flags & OCFS2_LOCK_UPCONVERT_FINISHING)
		goto leave_requeue;

	if (lockres->l_level == DLM_LOCK_NL) {
		BUG_ON(lockres->l_ex_holders || lockres->l_ro_holders);
		mlog(ML_BASTS, "lockres %s, Aborting dc\n", lockres->l_name);
		lockres->l_blocking = DLM_LOCK_NL;
		lockres_clear_flags(lockres, OCFS2_LOCK_BLOCKED);
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		goto leave;
	}

	if ((lockres->l_blocking == DLM_LOCK_EX)
	    && (lockres->l_ex_holders || lockres->l_ro_holders)) {
		mlog(ML_BASTS, "lockres %s, ReQ: EX/PR Holders %u,%u\n",
		     lockres->l_name, lockres->l_ex_holders,
		     lockres->l_ro_holders);
		goto leave_requeue;
	}

	if (lockres->l_blocking == DLM_LOCK_PR &&
	    lockres->l_ex_holders) {
		mlog(ML_BASTS, "lockres %s, ReQ: EX Holders %u\n",
		     lockres->l_name, lockres->l_ex_holders);
		goto leave_requeue;
	}

	if ((lockres->l_ops->flags & LOCK_TYPE_REQUIRES_REFRESH)
	    && (lockres->l_flags & OCFS2_LOCK_REFRESHING)) {
		mlog(ML_BASTS, "lockres %s, ReQ: Lock Refreshing\n",
		     lockres->l_name);
		goto leave_requeue;
	}

	new_level = ocfs2_highest_compat_lock_level(lockres->l_blocking);

	if (lockres->l_ops->check_downconvert
	    && !lockres->l_ops->check_downconvert(lockres, new_level)) {
		mlog(ML_BASTS, "lockres %s, ReQ: Checkpointing\n",
		     lockres->l_name);
		goto leave_requeue;
	}

	if (!lockres->l_ops->downconvert_worker)
		goto downconvert;

	blocking = lockres->l_blocking;
	level = lockres->l_level;
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	ctl->unblock_action = lockres->l_ops->downconvert_worker(lockres, blocking);

	if (ctl->unblock_action == UNBLOCK_STOP_POST) {
		mlog(ML_BASTS, "lockres %s, UNBLOCK_STOP_POST\n",
		     lockres->l_name);
		goto leave;
	}

	spin_lock_irqsave(&lockres->l_lock, flags);
	if ((blocking != lockres->l_blocking) || (level != lockres->l_level)) {
		mlog(ML_BASTS, "lockres %s, block=%d:%d, level=%d:%d, "
		     "Recheck\n", lockres->l_name, blocking,
		     lockres->l_blocking, level, lockres->l_level);
		goto recheck;
	}

downconvert:
	ctl->requeue = 0;

	if (lockres->l_ops->flags & LOCK_TYPE_USES_LVB) {
		if (lockres->l_level == DLM_LOCK_EX)
			set_lvb = 1;

		if (set_lvb && !(lockres->l_flags & OCFS2_LOCK_NEEDS_REFRESH))
			lockres->l_ops->set_lvb(lockres);
	}

	gen = ocfs2_prepare_downconvert(lockres, new_level);
	spin_unlock_irqrestore(&lockres->l_lock, flags);
	ret = ocfs2_downconvert_lock(osb, lockres, new_level, set_lvb,
				     gen);

leave:
	if (ret)
		mlog_errno(ret);
	return ret;

leave_requeue:
	spin_unlock_irqrestore(&lockres->l_lock, flags);
	ctl->requeue = 1;

	return 0;
}

static int ocfs2_data_convert_worker(struct ocfs2_lock_res *lockres,
				     int blocking)
{
	struct inode *inode;
	struct address_space *mapping;
	struct ocfs2_inode_info *oi;

       	inode = ocfs2_lock_res_inode(lockres);
	mapping = inode->i_mapping;

	if (S_ISDIR(inode->i_mode)) {
		oi = OCFS2_I(inode);
		oi->ip_dir_lock_gen++;
		mlog(0, "generation: %u\n", oi->ip_dir_lock_gen);
		goto out;
	}

	if (!S_ISREG(inode->i_mode))
		goto out;

	unmap_mapping_range(mapping, 0, 0, 0);

	if (filemap_fdatawrite(mapping)) {
		mlog(ML_ERROR, "Could not sync inode %llu for downconvert!",
		     (unsigned long long)OCFS2_I(inode)->ip_blkno);
	}
	sync_mapping_buffers(mapping);
	if (blocking == DLM_LOCK_EX) {
		truncate_inode_pages(mapping, 0);
	} else {
		filemap_fdatawait(mapping);
	}

out:
	return UNBLOCK_CONTINUE;
}

static int ocfs2_ci_checkpointed(struct ocfs2_caching_info *ci,
				 struct ocfs2_lock_res *lockres,
				 int new_level)
{
	int checkpointed = ocfs2_ci_fully_checkpointed(ci);

	BUG_ON(new_level != DLM_LOCK_NL && new_level != DLM_LOCK_PR);
	BUG_ON(lockres->l_level != DLM_LOCK_EX && !checkpointed);

	if (checkpointed)
		return 1;

	ocfs2_start_checkpoint(OCFS2_SB(ocfs2_metadata_cache_get_super(ci)));
	return 0;
}

static int ocfs2_check_meta_downconvert(struct ocfs2_lock_res *lockres,
					int new_level)
{
	struct inode *inode = ocfs2_lock_res_inode(lockres);

	return ocfs2_ci_checkpointed(INODE_CACHE(inode), lockres, new_level);
}

static void ocfs2_set_meta_lvb(struct ocfs2_lock_res *lockres)
{
	struct inode *inode = ocfs2_lock_res_inode(lockres);

	__ocfs2_stuff_meta_lvb(inode);
}

static void ocfs2_dentry_post_unlock(struct ocfs2_super *osb,
				     struct ocfs2_lock_res *lockres)
{
	struct ocfs2_dentry_lock *dl = ocfs2_lock_res_dl(lockres);
	ocfs2_dentry_lock_put(osb, dl);
}

static int ocfs2_dentry_convert_worker(struct ocfs2_lock_res *lockres,
				       int blocking)
{
	struct ocfs2_dentry_lock *dl = ocfs2_lock_res_dl(lockres);
	struct ocfs2_inode_info *oi = OCFS2_I(dl->dl_inode);
	struct dentry *dentry;
	unsigned long flags;
	int extra_ref = 0;

	if (blocking == DLM_LOCK_PR)
		return UNBLOCK_CONTINUE;

	spin_lock(&oi->ip_lock);
	oi->ip_flags |= OCFS2_INODE_MAYBE_ORPHANED;
	spin_unlock(&oi->ip_lock);

	spin_lock_irqsave(&lockres->l_lock, flags);
	spin_lock(&dentry_attach_lock);
	if (!(lockres->l_flags & OCFS2_LOCK_FREEING)
	    && dl->dl_count) {
		dl->dl_count++;
		extra_ref = 1;
	}
	spin_unlock(&dentry_attach_lock);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	mlog(0, "extra_ref = %d\n", extra_ref);

	if (!extra_ref)
		return UNBLOCK_CONTINUE;

	spin_lock(&dentry_attach_lock);
	while (1) {
		dentry = ocfs2_find_local_alias(dl->dl_inode,
						dl->dl_parent_blkno, 1);
		if (!dentry)
			break;
		spin_unlock(&dentry_attach_lock);

		mlog(0, "d_delete(%.*s);\n", dentry->d_name.len,
		     dentry->d_name.name);

		d_delete(dentry);
		dput(dentry);

		spin_lock(&dentry_attach_lock);
	}
	spin_unlock(&dentry_attach_lock);

	if (dl->dl_count == 1)
		return UNBLOCK_STOP_POST;

	return UNBLOCK_CONTINUE_POST;
}

static int ocfs2_check_refcount_downconvert(struct ocfs2_lock_res *lockres,
					    int new_level)
{
	struct ocfs2_refcount_tree *tree =
				ocfs2_lock_res_refcount_tree(lockres);

	return ocfs2_ci_checkpointed(&tree->rf_ci, lockres, new_level);
}

static int ocfs2_refcount_convert_worker(struct ocfs2_lock_res *lockres,
					 int blocking)
{
	struct ocfs2_refcount_tree *tree =
				ocfs2_lock_res_refcount_tree(lockres);

	ocfs2_metadata_cache_purge(&tree->rf_ci);

	return UNBLOCK_CONTINUE;
}

static void ocfs2_set_qinfo_lvb(struct ocfs2_lock_res *lockres)
{
	struct ocfs2_qinfo_lvb *lvb;
	struct ocfs2_mem_dqinfo *oinfo = ocfs2_lock_res_qinfo(lockres);
	struct mem_dqinfo *info = sb_dqinfo(oinfo->dqi_gi.dqi_sb,
					    oinfo->dqi_gi.dqi_type);

	lvb = ocfs2_dlm_lvb(&lockres->l_lksb);
	lvb->lvb_version = OCFS2_QINFO_LVB_VERSION;
	lvb->lvb_bgrace = cpu_to_be32(info->dqi_bgrace);
	lvb->lvb_igrace = cpu_to_be32(info->dqi_igrace);
	lvb->lvb_syncms = cpu_to_be32(oinfo->dqi_syncms);
	lvb->lvb_blocks = cpu_to_be32(oinfo->dqi_gi.dqi_blocks);
	lvb->lvb_free_blk = cpu_to_be32(oinfo->dqi_gi.dqi_free_blk);
	lvb->lvb_free_entry = cpu_to_be32(oinfo->dqi_gi.dqi_free_entry);
}

void ocfs2_qinfo_unlock(struct ocfs2_mem_dqinfo *oinfo, int ex)
{
	struct ocfs2_lock_res *lockres = &oinfo->dqi_gqlock;
	struct ocfs2_super *osb = OCFS2_SB(oinfo->dqi_gi.dqi_sb);
	int level = ex ? DLM_LOCK_EX : DLM_LOCK_PR;

	if (!ocfs2_is_hard_readonly(osb) && !ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(osb, lockres, level);
}

static int ocfs2_refresh_qinfo(struct ocfs2_mem_dqinfo *oinfo)
{
	struct mem_dqinfo *info = sb_dqinfo(oinfo->dqi_gi.dqi_sb,
					    oinfo->dqi_gi.dqi_type);
	struct ocfs2_lock_res *lockres = &oinfo->dqi_gqlock;
	struct ocfs2_qinfo_lvb *lvb = ocfs2_dlm_lvb(&lockres->l_lksb);
	struct buffer_head *bh = NULL;
	struct ocfs2_global_disk_dqinfo *gdinfo;
	int status = 0;

	if (ocfs2_dlm_lvb_valid(&lockres->l_lksb) &&
	    lvb->lvb_version == OCFS2_QINFO_LVB_VERSION) {
		info->dqi_bgrace = be32_to_cpu(lvb->lvb_bgrace);
		info->dqi_igrace = be32_to_cpu(lvb->lvb_igrace);
		oinfo->dqi_syncms = be32_to_cpu(lvb->lvb_syncms);
		oinfo->dqi_gi.dqi_blocks = be32_to_cpu(lvb->lvb_blocks);
		oinfo->dqi_gi.dqi_free_blk = be32_to_cpu(lvb->lvb_free_blk);
		oinfo->dqi_gi.dqi_free_entry =
					be32_to_cpu(lvb->lvb_free_entry);
	} else {
		status = ocfs2_read_quota_phys_block(oinfo->dqi_gqinode,
						     oinfo->dqi_giblk, &bh);
		if (status) {
			mlog_errno(status);
			goto bail;
		}
		gdinfo = (struct ocfs2_global_disk_dqinfo *)
					(bh->b_data + OCFS2_GLOBAL_INFO_OFF);
		info->dqi_bgrace = le32_to_cpu(gdinfo->dqi_bgrace);
		info->dqi_igrace = le32_to_cpu(gdinfo->dqi_igrace);
		oinfo->dqi_syncms = le32_to_cpu(gdinfo->dqi_syncms);
		oinfo->dqi_gi.dqi_blocks = le32_to_cpu(gdinfo->dqi_blocks);
		oinfo->dqi_gi.dqi_free_blk = le32_to_cpu(gdinfo->dqi_free_blk);
		oinfo->dqi_gi.dqi_free_entry =
					le32_to_cpu(gdinfo->dqi_free_entry);
		brelse(bh);
		ocfs2_track_lock_refresh(lockres);
	}

bail:
	return status;
}

int ocfs2_qinfo_lock(struct ocfs2_mem_dqinfo *oinfo, int ex)
{
	struct ocfs2_lock_res *lockres = &oinfo->dqi_gqlock;
	struct ocfs2_super *osb = OCFS2_SB(oinfo->dqi_gi.dqi_sb);
	int level = ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	int status = 0;

	
	if (ocfs2_is_hard_readonly(osb)) {
		if (ex)
			status = -EROFS;
		goto bail;
	}
	if (ocfs2_mount_local(osb))
		goto bail;

	status = ocfs2_cluster_lock(osb, lockres, level, 0, 0);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	if (!ocfs2_should_refresh_lock_res(lockres))
		goto bail;
	
	status = ocfs2_refresh_qinfo(oinfo);
	if (status)
		ocfs2_qinfo_unlock(oinfo, ex);
	ocfs2_complete_lock_res_refresh(lockres, status);
bail:
	return status;
}

int ocfs2_refcount_lock(struct ocfs2_refcount_tree *ref_tree, int ex)
{
	int status;
	int level = ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	struct ocfs2_lock_res *lockres = &ref_tree->rf_lockres;
	struct ocfs2_super *osb = lockres->l_priv;


	if (ocfs2_is_hard_readonly(osb))
		return -EROFS;

	if (ocfs2_mount_local(osb))
		return 0;

	status = ocfs2_cluster_lock(osb, lockres, level, 0, 0);
	if (status < 0)
		mlog_errno(status);

	return status;
}

void ocfs2_refcount_unlock(struct ocfs2_refcount_tree *ref_tree, int ex)
{
	int level = ex ? DLM_LOCK_EX : DLM_LOCK_PR;
	struct ocfs2_lock_res *lockres = &ref_tree->rf_lockres;
	struct ocfs2_super *osb = lockres->l_priv;

	if (!ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(osb, lockres, level);
}

static void ocfs2_process_blocked_lock(struct ocfs2_super *osb,
				       struct ocfs2_lock_res *lockres)
{
	int status;
	struct ocfs2_unblock_ctl ctl = {0, 0,};
	unsigned long flags;


	BUG_ON(!lockres);
	BUG_ON(!lockres->l_ops);

	mlog(ML_BASTS, "lockres %s blocked\n", lockres->l_name);

	spin_lock_irqsave(&lockres->l_lock, flags);
	if (lockres->l_flags & OCFS2_LOCK_FREEING)
		goto unqueue;
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	status = ocfs2_unblock_lock(osb, lockres, &ctl);
	if (status < 0)
		mlog_errno(status);

	spin_lock_irqsave(&lockres->l_lock, flags);
unqueue:
	if (lockres->l_flags & OCFS2_LOCK_FREEING || !ctl.requeue) {
		lockres_clear_flags(lockres, OCFS2_LOCK_QUEUED);
	} else
		ocfs2_schedule_blocked_lock(osb, lockres);

	mlog(ML_BASTS, "lockres %s, requeue = %s.\n", lockres->l_name,
	     ctl.requeue ? "yes" : "no");
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	if (ctl.unblock_action != UNBLOCK_CONTINUE
	    && lockres->l_ops->post_unlock)
		lockres->l_ops->post_unlock(osb, lockres);
}

static void ocfs2_schedule_blocked_lock(struct ocfs2_super *osb,
					struct ocfs2_lock_res *lockres)
{
	assert_spin_locked(&lockres->l_lock);

	if (lockres->l_flags & OCFS2_LOCK_FREEING) {
		mlog(ML_BASTS, "lockres %s won't be scheduled: flags 0x%lx\n",
		     lockres->l_name, lockres->l_flags);
		return;
	}

	lockres_or_flags(lockres, OCFS2_LOCK_QUEUED);

	spin_lock(&osb->dc_task_lock);
	if (list_empty(&lockres->l_blocked_list)) {
		list_add_tail(&lockres->l_blocked_list,
			      &osb->blocked_lock_list);
		osb->blocked_lock_count++;
	}
	spin_unlock(&osb->dc_task_lock);
}

static void ocfs2_downconvert_thread_do_work(struct ocfs2_super *osb)
{
	unsigned long processed;
	struct ocfs2_lock_res *lockres;

	spin_lock(&osb->dc_task_lock);
	osb->dc_work_sequence = osb->dc_wake_sequence;

	processed = osb->blocked_lock_count;
	while (processed) {
		BUG_ON(list_empty(&osb->blocked_lock_list));

		lockres = list_entry(osb->blocked_lock_list.next,
				     struct ocfs2_lock_res, l_blocked_list);
		list_del_init(&lockres->l_blocked_list);
		osb->blocked_lock_count--;
		spin_unlock(&osb->dc_task_lock);

		BUG_ON(!processed);
		processed--;

		ocfs2_process_blocked_lock(osb, lockres);

		spin_lock(&osb->dc_task_lock);
	}
	spin_unlock(&osb->dc_task_lock);
}

static int ocfs2_downconvert_thread_lists_empty(struct ocfs2_super *osb)
{
	int empty = 0;

	spin_lock(&osb->dc_task_lock);
	if (list_empty(&osb->blocked_lock_list))
		empty = 1;

	spin_unlock(&osb->dc_task_lock);
	return empty;
}

static int ocfs2_downconvert_thread_should_wake(struct ocfs2_super *osb)
{
	int should_wake = 0;

	spin_lock(&osb->dc_task_lock);
	if (osb->dc_work_sequence != osb->dc_wake_sequence)
		should_wake = 1;
	spin_unlock(&osb->dc_task_lock);

	return should_wake;
}

static int ocfs2_downconvert_thread(void *arg)
{
	int status = 0;
	struct ocfs2_super *osb = arg;

	while (!(kthread_should_stop() &&
		ocfs2_downconvert_thread_lists_empty(osb))) {

		wait_event_interruptible(osb->dc_event,
					 ocfs2_downconvert_thread_should_wake(osb) ||
					 kthread_should_stop());

		mlog(0, "downconvert_thread: awoken\n");

		ocfs2_downconvert_thread_do_work(osb);
	}

	osb->dc_task = NULL;
	return status;
}

void ocfs2_wake_downconvert_thread(struct ocfs2_super *osb)
{
	spin_lock(&osb->dc_task_lock);
	osb->dc_wake_sequence++;
	spin_unlock(&osb->dc_task_lock);
	wake_up(&osb->dc_event);
}
