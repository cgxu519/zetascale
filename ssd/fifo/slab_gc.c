//----------------------------------------------------------------------------
// ZetaScale
// Copyright (c) 2016, SanDisk Corp. and/or all its affiliates.
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License version 2.1 as published by the Free
// Software Foundation;
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License v2.1 for more details.
//
// A copy of the GNU Lesser General Public License v2.1 is provided with this package and
// can also be found at: http://opensource.org/licenses/LGPL-2.1
// You should have received a copy of the GNU Lesser General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 59 Temple
// Place, Suite 330, Boston, MA 02111-1307 USA.
//----------------------------------------------------------------------------

/*
 * File:   slab_gc.c
 * Author: Evgeny Firsov
 *
 * Created on February 1, 2013
 *
 * SanDisk Proprietary Material, © Copyright 2012 SanDisk, all rights reserved.
 * http://www.sandisk.com
 *
 */
#include "slab_gc.h"
#include "utils/properties.h" /* getProperty_int */
#include "sdftcp/locks.h" /* atomic_inc */
#include "ssd/ssd_aio.h" /* SSD_AIO_CTXT_MCD_REC_LGWR */
#include "api/zs.h" /* Statistics */
#include "protocol/action/recovery.h"
#include "hash.h"

extern inline uint32_t mcd_osd_lba_to_blk( uint32_t blocks );
extern int              Mcd_osd_max_nclasses;


void *
context_alloc( int category ); /* mcd_rec.c */

struct slab_gc_shard_struct {
	fthMbox_t mbox;
	fthMbox_t killed_mbox;
	int threshold;
	char *buf, *_ubuf;
	uint64_t *bitmap;
	/*EF: Not used */
	uint64_t stat[STAT(SEGMENTS_CANCELLED) - STAT(SEGMENTS_COMPACTED) + 1];
};

uint64_t gc_stat[STAT(SEGMENTS_CANCELLED) - STAT(SEGMENTS_COMPACTED) + 1];

struct slab_gc_class_struct {
	int pending;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

#define stat_inc(s, a) atomic_inc(gc_stat[STAT(a) - STAT(SEGMENTS_COMPACTED)])

void slab_gc_get_stats(mcd_osd_shard_t* shard, ZS_stats_t* stats, FILE* log)
{ 
	int i, cnt = STAT(SEGMENTS_CANCELLED) - STAT(SEGMENTS_COMPACTED) + 1;

	for(i = 0; i < cnt; i++)
		stats->flash_stats[STAT(SEGMENTS_COMPACTED) + i] = gc_stat[i];
}

bool slab_gc_update_threshold(mcd_osd_shard_t *shard, int threshold)
{
	int gt = threshold;

	if(!shard->gc)
		return false;

	if(gt > 100 || gt < 0)
	{
		if(gt > 100) gt = 100;
		if(gt < 0) gt = 0;

		mcd_log_msg(180005, PLAT_LOG_LEVEL_WARN,
			"shard=%p GC threshold adjusted from %d %% to %d %%",
			shard, threshold, gt);
	}

	mcd_log_msg(180211, PLAT_LOG_LEVEL_INFO, "Slab GC threshold set to %d%%", threshold);

	shard->gc->threshold = gt;

	return true;
}

/* With sync set to true check for free space only, e.g. gisregard for threshold */
static
bool slab_gc_able(mcd_osd_shard_t *shard, mcd_osd_slab_class_t* class, bool sync)
{
	uint32_t free_slabs = class->total_slabs - class->used_slabs;
	uint32_t used_slabs = class->used_slabs - class->dealloc_pending;

	if(!class->total_slabs) return false;

	/* Its expensive to calcualte amount of slabs in least used segment here.
		but average should be good enough upper bound for minimum. */
	uint32_t avg_slabs = class->used_slabs * class->slabs_per_segment / class->total_slabs;

	/* Sync GC possible if there are enough free space to relocated average segment of the class */
	bool gc_sync = class->total_slabs / class->slabs_per_segment > 1 && free_slabs > avg_slabs * 3; /* 3 = from + to + reserve */
	/* Async GC possible if sync possible and class usage beyond GC threshold */
	bool gc_async = used_slabs < shard->gc->threshold * class->total_slabs / 100 && gc_sync;

	return used_slabs && ((sync && gc_sync) || gc_async);
}

static
int slab_gc_relocate_slab(
	void *context,
	mcd_osd_shard_t* shard,
	mcd_osd_slab_class_t* class,
	uint64_t src_blk_offset,
	char* buf)
{
    int               i;
    int               rc = 0;
    uint32_t          hash_index;
    hash_entry_t	* hash_entry = NULL;
    uint32_t        * bucket_head;
    uint32_t          bucket_idx;
    hash_handle_t   * hdl = shard->hash_handle;
    bucket_entry_t  * bucket;
    uint64_t          dst_blk_offset, target_seqno = 0, seqno;
    uint64_t          dst_offset, syndrome = 0;
    fthWaitEl_t     * wait;

    mcd_logrec_object_t log_rec;

	plat_assert(hdl->addr_table);
    seqno = atomic_inc_get(shard->sequence);

    hash_index = hdl->addr_table[src_blk_offset];

    bucket_head = hdl->hash_buckets + (hash_index / OSD_HASH_BUCKET_SIZE);
    wait = fthLock( hdl->bucket_locks + 
                    (hash_index / hdl->lock_bktsize), 1, NULL );
    bucket_idx = *bucket_head;
    plat_assert( bucket_idx < 
                (hdl->hash_size/OSD_HASH_ENTRY_PER_BUCKET_ENTRY) + 
                    (hdl->hash_size / OSD_HASH_BUCKET_SIZE));
    for(;bucket_idx !=0;bucket_idx = bucket->next){
        bucket = hdl->hash_table + (bucket_idx - 1);
        for(i=0;i<OSD_HASH_ENTRY_PER_BUCKET_ENTRY;i++){
            hash_entry = &bucket->hash_entry[i];

            if (hash_entry->used == 0)
                continue;

            if (hash_entry->blkaddress == src_blk_offset)
                goto found;
        }
    }

found:

    /* SLAB is pending deallocation, thats why in alloc_map so far,
       but hash_entry points to different place already */
    if(hash_entry == NULL) goto out;

	if(!hash_entry->used || hash_entry->blkaddress != src_blk_offset)
		goto out;

	if(!(rc = mcd_fth_osd_slab_alloc(context, shard, class->slab_blksize, 1, &dst_blk_offset)))
		goto out;

	plat_assert(src_blk_offset != dst_blk_offset);

	dst_offset = mcd_osd_rand_address(shard, dst_blk_offset);

	mcd_osd_meta_t *meta = (mcd_osd_meta_t *)buf;
	syndrome = hashck((unsigned char*)buf + sizeof(mcd_osd_meta_t),
			meta->key_len, 0, meta->cguid );
	target_seqno = meta->seqno;
	meta->seqno = seqno;

	plat_assert(0); //Dont use shard->durability_level. Set sync alwa=ys.
	if((rc = mcd_fth_aio_blk_write_low(context,
				buf,
				dst_offset,
				class->slab_blksize * Mcd_osd_blk_size,
				shard->durability_level > SDF_RELAXED_DURABILITY &&
				shard->data_sync)))
		goto out;

	stat_inc(shard, SLABS_RELOCATED);

	log_rec.syndrome   = hash_entry->hesyndrome;
	log_rec.deleted    = hash_entry->deleted;
	log_rec.reserved   = 0;
	log_rec.blocks     = hash_entry->blocks;
	//log_rec.rbucket     = (syndrome % hdl->hash_size) / Mcd_osd_bucket_size;
	log_rec.rbucket     = (syndrome % hdl->hash_size);
	log_rec.mlo_blk_offset = dst_blk_offset;
	log_rec.cntr_id    = hash_entry->cntr_id;
	log_rec.seqno      = seqno;
	log_rec.mlo_old_offset   = ~(hash_entry->blkaddress) & 0x0000ffffffffffffull;
	log_rec.target_seqno = target_seqno;
	log_rec.raw        = FALSE;

	mcd_fth_osd_remove_entry(shard, hash_entry, true, false);

	log_write(shard, &log_rec);

	atomic_add(shard->blk_consumed, mcd_osd_lba_to_blk(hash_entry->blocks));

	plat_assert(hash_entry->blkaddress == src_blk_offset);
	hash_entry->blkaddress = dst_blk_offset;
	hdl->addr_table[dst_blk_offset] = syndrome % hdl->hash_size;

	plat_assert(shard->slab_classes + shard->class_table[mcd_osd_lba_to_blk(hash_entry->blocks)] == class);

	plat_assert(hash_entry->blkaddress == dst_blk_offset);
out:
	if(rc) {
		abort_on_io_error(rc);
		stat_inc(shard, RELOCATE_ERRORS);
	}

	fthUnlock(wait);

	return rc;
}

static
void slab_gc_unlock_waiters(mcd_osd_slab_class_t *class, bool sync)
{
	pthread_mutex_lock(&class->gc->mutex);

	/* Allow GC requests submission for this class */
	if(!sync)
		class->gc->pending = 0;

	/* Wake everybody who wait for this class GC to finish */
	pthread_cond_broadcast(&class->gc->cond);

	pthread_mutex_unlock(&class->gc->mutex);
}

static
mcd_osd_segment_t*
slab_gc_least_used_segment(mcd_osd_slab_class_t* class)
{
	mcd_osd_segment_t *segment, *min_segment = NULL;
	uint32_t min = INT_MAX;
	int i;

	for(i = 0; i < class->num_segments; i++)
	{
		segment = class->segments[i];

		if(segment && min > segment->used_slabs)
		{
			min = segment->used_slabs;
			min_segment = segment;
		}
	}

	return min_segment;
}

static
mcd_osd_slab_class_t*
slab_gc_least_used_class(mcd_osd_shard_t* shard)
{
	mcd_osd_slab_class_t *class = NULL;
	uint32_t min = INT_MAX;
	int i;

	for(i = 0; i < Mcd_osd_max_nclasses; i++ )
	{
		uint32_t used = shard->slab_classes[i].used_slabs - shard->slab_classes[i].dealloc_pending;
		if(used && slab_gc_able(shard, &shard->slab_classes[i], true) && min > used)
		{
			min = used;
			class = &shard->slab_classes[i];
		}
	}

	return class;
}

static
void slab_gc_compact_class(
	void *context,
	mcd_osd_shard_t* shard,
	mcd_osd_slab_class_t* class)
{
	uint32_t i;
	mcd_osd_segment_t *segment;
	uint64_t blk_offset, map_offset, map_value;
	fthWaitEl_t  *wait;

	mcd_log_msg(180014, PLAT_LOG_LEVEL_TRACE, "shard->id=%ld. GC of class->blksize=%d requested", shard->id, class->slab_blksize);

	segment = slab_gc_least_used_segment(class);

	wait = fthLock(&class->lock, 1, NULL);

	/* Lock prevents segment from deallocation in mcd_fth_osd_shrink_class */
	segment = mcd_osd_segment_lock(class, segment);

	/* Prevent SLAB allocations from this segment */
	if(segment)
		mcd_osd_slab_segments_free_slot(class, segment, (void*)-1);

	fthUnlock(wait);

	/* Failed to lock segment, e.g. already freed and reused */
	if(!segment)
		return;

	mcd_osd_segment_unlock(segment);

	map_offset = 0;
	blk_offset = segment->blk_offset;

	/* Allocate and align buffer for segment */
	if(unlikely(!shard->gc->_ubuf))
	{
		if(!(shard->gc->_ubuf = plat_alloc(MCD_OSD_SEGMENT_SIZE + Mcd_osd_blk_size)))
			goto out;

		shard->gc->buf = (char *)( ( (uint64_t)shard->gc->_ubuf + Mcd_osd_blk_size - 1 )
                    & Mcd_osd_blk_mask );
	}

	if(!shard->gc->bitmap && !(shard->gc->bitmap = (uint64_t*)plat_alloc(Mcd_osd_segment_blks / 8)))
		goto out;

	/* Make bitmap snapshot */
	memcpy(shard->gc->bitmap, segment->mos_bitmap, Mcd_osd_segment_blks / 8);

	/* Read whole segment at once */
	if((mcd_fth_aio_blk_read(context,
			shard->gc->buf,
			mcd_osd_rand_address(shard, blk_offset),
			Mcd_osd_segment_size)))
		goto out;

	/* Run through all used slabs of the segment */
	while (map_offset < (Mcd_osd_segment_blks / class->slab_blksize + 63) / 64)
	{
		map_value = shard->gc->bitmap[map_offset];

		i = 64;
		while(map_value)
		{
			if((map_value & 0x1) && slab_gc_relocate_slab(context, shard, class,
					blk_offset, shard->gc->buf + (blk_offset - segment->blk_offset) * Mcd_osd_blk_size))
				goto out;

			blk_offset += class->slab_blksize;
			map_value >>= 1;
			i--;
		}

		blk_offset += i * class->slab_blksize;
		map_offset++;
	}

	/* This is done in hope that all pending deallocation
	   slabs deallocated at log sync */

	log_sync(shard);

	stat_inc(shard, SEGMENTS_COMPACTED);

	if(!segment->used_slabs)
	{
		if(mcd_fth_osd_shrink_class(shard, segment, true))
			goto out;

		mcd_log_msg(180015, PLAT_LOG_LEVEL_TRACE, "shard->id=%ld. GC freed segment from class->blksize=%d. Shard free segments %ld", shard->id, class->slab_blksize, shard->free_segments_count);

		stat_inc(shard, SEGMENTS_FREED);

		return;
	}

out:
	/* Put segment back to segment list if we were unable to free it */
	wait = fthLock( &class->lock, 1, NULL );

	class->segments[segment->idx] = segment;

	stat_inc(shard, SEGMENTS_CANCELLED);

	fthUnlock(wait);
}

static
void slab_gc_worker_thread(uint64_t arg)
{
	osd_state_t *context = context_alloc( SSD_AIO_CTXT_MCD_REC_LGWR );
	mcd_osd_shard_t* shard = (mcd_osd_shard_t *)arg;
	mcd_osd_slab_class_t *class;

	while((class = (mcd_osd_slab_class_t *)fthMboxWait(&shard->gc->mbox)))
	{
		/* Try to reclaim one segment if this was sync request */
		if(slab_gc_able(shard, class, true))
			slab_gc_compact_class(context, shard, class);

		/* Let sync waiters to proceed, we've just freed one segment */
		slab_gc_unlock_waiters(class, true);

		/* Reclaim the rest taking threshold into accout */
		while(slab_gc_able(shard, class, false))
			slab_gc_compact_class(context, shard, class);

		/* Allow GC request submission for this class */
		slab_gc_unlock_waiters(class, false);
	}

	fthMboxPost(&shard->gc->killed_mbox, 0);
}

bool slab_gc_init(mcd_osd_shard_t* shard, int threshold /* % of used slabs */)
{
	int i, j;

	mcd_log_msg(180018, PLAT_LOG_LEVEL_TRACE, "ENTERING, shard->id=%ld, gc threshold=%d", shard->id, threshold);

	if(getProperty_Int("ZS_TRX", 0)) {
		plat_log_msg(180209, PLAT_LOG_CAT_PRINT_ARGS, PLAT_LOG_LEVEL_WARN,
				"ZS_SLAB_GC is incompatible with ZS_TRX. Disabling ZS_SLAB_GC.");
		return false;
	}

	plat_rwlock_wrlock(&shard->slab_gc_mgmt_lock);

	if(shard->gc) {
		mcd_log_msg(180212, PLAT_LOG_LEVEL_INFO, "Slab GC already initialized. shardID=%ld. Threshold=%d%%", shard->id, threshold);
		plat_rwlock_unlock(&shard->slab_gc_mgmt_lock);
		return true;
	}

	if(!(shard->gc = plat_alloc(sizeof(slab_gc_shard_t)))) {
		plat_rwlock_unlock(&shard->slab_gc_mgmt_lock);
		return false;
	}

	fthMboxInit(&shard->gc->mbox);
	fthMboxInit(&shard->gc->killed_mbox);

	shard->gc->_ubuf = NULL;
	shard->gc->bitmap = NULL;

	slab_gc_update_threshold(shard, threshold);

	for(i = 0; i < Mcd_osd_max_nclasses; i++ )
	{
		mcd_osd_slab_class_t *class = &shard->slab_classes[i];

		if(!(class->gc = plat_alloc(sizeof(slab_gc_class_t))))
		{
			for(j = 0; j < i; j++)
				plat_free(shard->slab_classes[j].gc);

			plat_free(shard->gc);
			shard->gc = NULL;

			plat_rwlock_unlock(&shard->slab_gc_mgmt_lock);

			return false;
		}

		pthread_mutex_init(&class->gc->mutex, NULL);
		pthread_cond_init(&class->gc->cond, NULL);

		class->gc->pending = 0;
	}

	fthResume(fthSpawn(slab_gc_worker_thread, 81920), (uint64_t)shard);

	mcd_log_msg(180213, PLAT_LOG_LEVEL_INFO, "Slab GC initialized. shardID=%ld. Threshold=%d%%", shard->id, threshold);

	plat_rwlock_unlock(&shard->slab_gc_mgmt_lock);

	return true;
}

void slab_gc_signal(
	mcd_osd_shard_t* shard,
	mcd_osd_slab_class_t* class)
{
	bool sync = !class;

	plat_rwlock_rdlock(&shard->slab_gc_mgmt_lock);

	if(sync)
		class = slab_gc_least_used_class(shard);

	if(!class || !shard->gc) {
		plat_rwlock_unlock(&shard->slab_gc_mgmt_lock);
		return;
	}

	if(!class->gc->pending && slab_gc_able(shard, class, sync) &&
			atomic_cmp_swap_bool(class->gc->pending, 0, 1))
	{
		if(sync)
			stat_inc(shard, SIGNALLED_SYNC);
		else
			stat_inc(shard, SIGNALLED);

		mcd_log_msg(180016, PLAT_LOG_LEVEL_TRACE, "shard->id=%ld. signalling gc thread to gc class->slab_blksize=%d class->total_slabs=%ld class->used_slabs=%ld", shard->id, class->slab_blksize, class->total_slabs, class->used_slabs);

		fthMboxPost(&shard->gc->mbox, (uint64_t)(class));
	}

	if(sync)
	{
		stat_inc(shard, WAIT_SYNC);

		/* Try to free already collected/deleted objects */
		log_sync(shard);

		pthread_mutex_lock(&class->gc->mutex);

		if(class->gc->pending && !shard->free_segments_count)
			pthread_cond_wait(&class->gc->cond, &class->gc->mutex);

		pthread_mutex_unlock(&class->gc->mutex);
	}

	plat_rwlock_unlock(&shard->slab_gc_mgmt_lock);
}

void slab_gc_end(mcd_osd_shard_t* shard)
{
	int i;

	plat_rwlock_wrlock(&shard->slab_gc_mgmt_lock);

	if(!shard->gc) {
		plat_rwlock_unlock(&shard->slab_gc_mgmt_lock);
		return;
	}

	mcd_log_msg(180006, PLAT_LOG_LEVEL_TRACE, "ENTERING, shardID=%p", shard);

	fthMboxPost(&shard->gc->mbox, 0);

	fthMboxWait(&shard->gc->killed_mbox);

	fthMboxTerm(&shard->gc->mbox);
	fthMboxTerm(&shard->gc->killed_mbox);

	for(i = 0; i < Mcd_osd_max_nclasses; i++ )
	{
		mcd_osd_slab_class_t *class = &shard->slab_classes[i];

		pthread_mutex_destroy(&class->gc->mutex);
		pthread_cond_destroy(&class->gc->cond);

		plat_free(class->gc);
	}

	if(shard->gc->_ubuf)
	{
		plat_free(shard->gc->_ubuf);
		plat_free(shard->gc->bitmap);
	}

	plat_free(shard->gc);

	shard->gc = NULL;

	mcd_log_msg(180214, PLAT_LOG_LEVEL_INFO, "Slab GC stopped. shardID=%ld", shard->id);

	plat_rwlock_unlock(&shard->slab_gc_mgmt_lock);
}

