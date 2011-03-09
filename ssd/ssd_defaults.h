/************************************************************************
 *
 * File:   ssd_defaults.h
 * Author: Brian O'Krafka
 *
 * Created on January 8, 2009
 *
 * (c) Copyright 2009, Schooner Information Technology, Inc.
 * http://www.schoonerinfotech.com/
 *
 * $Id: ssd_defaults.h 308 2008-02-20 22:34:58Z briano $
 ************************************************************************/

#ifndef _SSD_DEFAULTS_H
#define _SSD_DEFAULTS_H

#ifdef __cplusplus
extern "C" {
#endif

extern struct flashDev *default_flashOpen(char *name, int flags);
extern void default_flashClose(struct flashDev *dev);
extern struct shard *default_shardCreate(struct flashDev *dev, uint64_t shardID, 
	  int flags, uint64_t quota, unsigned maxObjs);
extern struct shard *default_shardOpen(struct flashDev *dev, 
                                         uint64_t shardID);
extern void default_shardFree(struct shard *);
extern void default_shardAttributes(struct shard *shard, int *p_flags, 
	  uint64_t *p_quota, unsigned *p_maxObjs);
extern int default_shardDelete(struct shard *shard);
extern int default_shardStart(struct shard *shard);
extern int default_shardStop(struct shard *shard);
extern void default_shardFlushAll(struct shard *shard, flashTime_t expTime);
extern struct shard *default_getNextShard(struct flashDev *dev, struct shard *prevShard);
extern struct shard *default_shardFind(struct flashDev *dev, uint64_t shardID);
extern int default_flashGet(struct ssdaio_ctxt *pctxt, struct shard *shard, struct objMetaData *metaData, 
	  char *key, char **dataPtr, int flags);
extern int default_flashPut(struct ssdaio_ctxt *pctxt, struct shard *shard, struct objMetaData *metaData, 
	  char *key, char *data, int flags);
extern struct objDesc *default_flashEnumerate(struct shard *shard, 
	  struct objDesc *prevObj, int *hashIndex, char **key);
extern void default_setLRUCallback(struct shard *shard, 
	  uint64_t (*lruCallback)(syndrome_t syndrome, uint64_t newSeqNo));
extern struct objMetaData *default_getMetaData(struct objDesc *obj);
extern uint64_t default_getSequence(shard_t *shard);
extern void default_flashSetSyncedSequence(shard_t *shard, uint64_t seqno);
extern uint64_t default_flashStats(struct shard *shard, int key);
extern void default_debugPrintfInit(void);
extern void default_debugPrintf(char *format, ...);
extern int default_flashSequenceScan(struct shard *shard, uint64_t *id1, uint64_t *id2,
	  struct objMetaData *metaData, char **key, 
	  char **dataPtr, int flags);
extern int default_flashFreeBuf(void *p);
extern void default_shardSync(struct shard *shard);
extern uint64_t default_flashGetRetainedTombstoneGuarantee(struct shard *shard);
extern void default_flashRegisterSetRetainedTombstoneGuaranteeCallback(void (*callback)(uint64_t shardID, uint64_t seqno));

#ifdef	__cplusplus
}
#endif

#endif /* _SSD_DEFAULTS_H */

