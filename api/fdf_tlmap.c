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
 * File:   fdf_tlmap.c
 * Author: Brian O'Krafka
 *
 * Created on September 11, 2008
 *
 * SanDisk Proprietary Material, © Copyright 2012 SanDisk, all rights reserved.
 * http://www.sandisk.com
 *
 * IMPORTANT NOTES:
 *    - Unlike tlmap in the zs directory, zs_tlmap does NOT
 *      automatically malloc and free the key and contest of
 *      a hashtable entry!
 *
 * $Id: tlmap.c 308 2008-02-20 22:34:58Z tomr $
 */

#define _ZS_TLMAP_C

#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <execinfo.h>
#include "fdf_tlmap.h"
#include "fdf_tlmap_internal.h"
#include "fdf_hash.h"
#include <assert.h>

//  Define this to turn on detailed list checking
// #define LISTCHECK

static void _zstlmap_assert(int x) {
    if (x) {
        fprintf(stderr, "Assertion failure in zs_tlmap!\n");
	assert(0);
    }
}
#define zstlmap_assert(x) _zstlmap_assert((x) == 0)

#define do_lock(x)  {if (pm->use_locks) { pthread_mutex_lock(x); }}
#define do_unlock(x) {if (pm->use_locks) { pthread_mutex_unlock(x); }}

//  Predeclarations
#ifdef LISTCHECK
static void check_list(ZSTLMapBucket_t *pb, ZSTLMapEntry_t *pme);
#endif
static void insert_lru(struct ZSTLMap *pm, ZSTLMapEntry_t *pme);
static void remove_lru(struct ZSTLMap *pm, ZSTLMapEntry_t *pme);
static void update_lru(struct ZSTLMap *pm, ZSTLMapEntry_t *pme);
static void replace_lru(struct ZSTLMap *pm, ZSTLMapEntry_t *pme);
static ZSTLMapEntry_t *find_pme(struct ZSTLMap *pm, char *pkey, uint32_t keylen, ZSTLMapBucket_t **pb);
static ZSTLMapEntry_t *create_pme(ZSTLMap_t *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen);
// static ZSTLMapEntry_t *copy_pme(ZSTLMap_t *pm, ZSTLMapEntry_t *pme);
// static ZSTLMapEntry_t *copy_pme_list(ZSTLMap_t *pm, ZSTLMapEntry_t *pme);
static void free_pme(ZSTLMap_t *pm, ZSTLMapEntry_t *pme);
static void free_pme_list(ZSTLMap_t *pm, ZSTLMapEntry_t *pme);


static ZSTLMapEntry_t *get_entry(ZSTLMap_t *pm)
{
    int                i;
    ZSTLMapEntry_t  *e;

    if (pm->FreeEntries == NULL) {
        e = (ZSTLMapEntry_t *) malloc(N_ENTRIES_TO_MALLOC*sizeof(ZSTLMapEntry_t));
	zstlmap_assert(e);

	for (i=0; i<N_ENTRIES_TO_MALLOC; i++) {
	    e[i].next = pm->FreeEntries;
	    pm->FreeEntries = &(e[i]);
	}
        pm->NEntries += N_ENTRIES_TO_MALLOC;
    }
    e = pm->FreeEntries;
    pm->FreeEntries = e->next;

    pm->NUsedEntries++;
    return(e);
}

static void free_entry(ZSTLMap_t *pm, ZSTLMapEntry_t *e)
{
    e->next     = pm->FreeEntries;
    pm->FreeEntries = e;
    pm->NUsedEntries--;
}

static struct ZSTLIterator *get_iterator(ZSTLMap_t *pm)
{
    int                   i;
    struct ZSTLIterator *it;

    if (pm->FreeIterators == NULL) {
        it = (struct ZSTLIterator *) malloc(N_ITERATORS_TO_MALLOC*sizeof(struct ZSTLIterator));
	zstlmap_assert(it);

	for (i=0; i<N_ITERATORS_TO_MALLOC; i++) {
	    it[i].next = pm->FreeIterators;
	    pm->FreeIterators = &(it[i]);
	}
        pm->NIterators += N_ITERATORS_TO_MALLOC;
    }
    it = pm->FreeIterators;
    pm->FreeIterators = it->next;

    pm->NUsedIterators++;
    return(it);
}

static void free_iterator(ZSTLMap_t *pm, struct ZSTLIterator *it)
{
    it->next     = pm->FreeIterators;
    pm->FreeIterators = it;
    pm->NUsedIterators--;
}

struct ZSTLMap *ZSTLMapInit(uint64_t nbuckets, uint64_t max_entries, char use_locks, void (*replacement_callback)(void *callback_data, char *key, uint32_t keylen, char *pdata, uint64_t datalen), void *replacement_callback_data)
{
    uint64_t          i;
    ZSTLMap_t       *pm;

    pm = (ZSTLMap_t *) malloc(sizeof(ZSTLMap_t));
    zstlmap_assert(pm);
    pm->lru_head       = NULL;
    pm->lru_tail       = NULL;
    pm->replacement_callback       = replacement_callback;
    pm->replacement_callback_data  = replacement_callback_data;
    pm->use_locks      = use_locks;
    pm->max_entries    = max_entries;
    pm->n_entries      = 0;
    pm->nbuckets       = nbuckets;
    pm->NEntries       = 0;
    pm->NUsedEntries   = 0;
    pm->FreeEntries    = NULL;
    pm->NIterators     = 0;
    pm->NUsedIterators = 0;
    pm->FreeIterators  = NULL;

    pm->buckets       = (ZSTLMapBucket_t *) malloc(nbuckets*(sizeof(ZSTLMapBucket_t)));
    zstlmap_assert(pm->buckets);

    for (i=0; i<nbuckets; i++) {
	pm->buckets[i].entry = NULL;
    }

    pthread_mutex_init(&(pm->mutex), NULL);

    // xxxzzz only one enumeration can be done at a time per ZSTLMap!
    pthread_mutex_init(&(pm->enum_mutex), NULL);

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapInit: pm=%p\n", pm);
    #endif

    return(pm);
}

void ZSTLMapDestroy(struct ZSTLMap *pm)
{
    uint64_t          i;

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapDestroy: pm=%p\n", pm);
    #endif

    for (i=0; i<pm->nbuckets; i++) {
        free_pme_list(pm, pm->buckets[i].entry);
    }

    free(pm->buckets);
    pthread_mutex_destroy(&(pm->mutex));
    free(pm);
}

void ZSTLMapClear(struct ZSTLMap *pm)
{
    ZSTLMapEntry_t   *pme, *pme_next;

    do_lock(&(pm->mutex));

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapClear: pm=%p\n", pm);
    #endif

    //  Go through LRU list and free entries
    //  This is much faster than examining all of the buckets!

    for (pme=pm->lru_head; pme != NULL; pme = pme_next) {
        pme_next = pme->next_lru;
	pme->bucket->entry = NULL;
        free_pme(pm, pme);
    }

    pm->n_entries     = 0;
    pm->lru_head      = NULL;
    pm->lru_tail      = NULL;
    do_unlock(&(pm->mutex));
}

void ZSTLMapCheckRefcnts(struct ZSTLMap *pm)
{
    uint64_t           i;
    ZSTLMapEntry_t   *pme;

    do_lock(&(pm->mutex));

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapCheckRefcnts: pm=%p\n", pm);
    #endif

    for (i=0; i<pm->nbuckets; i++) {
	for (pme = pm->buckets[i].entry; pme != NULL; pme = pme->next) {
	    if (pme->refcnt != 0) {
		fprintf(stderr, "*****************   ZSTLMapCheckRefcnts: [pm=%p]: key 0x%lx refcnt=%d!\n", pm, *((uint64_t *) pme->key), pme->refcnt);
	    }
	}
    }
    do_unlock(&(pm->mutex));
}

//  Return non-NULL if success, NULL if object exists
struct ZSTLMapEntry *ZSTLMapCreate(struct ZSTLMap *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen)
{
    ZSTLMapEntry_t   *pme;
    ZSTLMapBucket_t  *pb;

    do_lock(&(pm->mutex));

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapCreate: pm=%p, key=0x%lx, keylen=%d, pdata=%p, datalen=%ld, ", pm, *((uint64_t *) pkey), keylen, pdata, datalen);
    #endif

    pme = find_pme(pm, pkey, keylen, &pb);

    if (pme != NULL) {
	#ifdef SANDISK_PRINTSTUFF
	    fprintf(stderr, "pme=%p\n", pme);
	#endif

	do_unlock(&(pm->mutex));
        return(NULL);
    }

    /* Create a new entry. */

    pme = create_pme(pm, pkey, keylen, pdata, datalen);

    /* put myself on the bucket list */
    pme->bucket = pb;
    pme->next = pb->entry;
    pb->entry = pme;
    #ifdef LISTCHECK
        check_list(pb, pme);
    #endif

    pm->n_entries++;

    /* put myself on the lru list */
    {
        insert_lru(pm, pme);

	if ((pm->max_entries > 0) && (pm->n_entries > pm->max_entries)) {
	    // do an LRU replacement
	    replace_lru(pm, pme);
	}
    }

    do_unlock(&(pm->mutex));
    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "pme=%p\n", pme);
    #endif

    return(pme);
}

//  Return non-NULL if success, NULL if object does not exist
struct ZSTLMapEntry *ZSTLMapUpdate(struct ZSTLMap *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen)
{
    ZSTLMapEntry_t   *pme;

    do_lock(&(pm->mutex));

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapUpdate: pm=%p, key=0x%lx, keylen=%d, pdata=%p, datalen=%ld\n", pm, *((uint64_t *) pkey), keylen, pdata, datalen);
    #endif

    pme = find_pme(pm, pkey, keylen, NULL);

    if (pme == NULL) {
	do_unlock(&(pm->mutex));
        return(NULL);
    }

    /* Update an existing entry. */

    // update the LRU list if necessary
    if (pm->max_entries != 0) {
	update_lru(pm, pme);
    }

    if (pm->replacement_callback) {
        //  return NULL for key so that it is not freed!
	(pm->replacement_callback)(pm->replacement_callback_data, pme->key, pme->keylen, pme->contents, pme->datalen);
    }

    pme->contents = pdata;
    pme->datalen  = datalen;

    do_unlock(&(pm->mutex));
    return(pme);
}

//  Return non-NULL if success, NULL if object exists
struct ZSTLMapEntry *ZSTLMapSet(struct ZSTLMap *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen, char **old_pdata, uint64_t *old_datalen)
{
    ZSTLMapEntry_t   *pme;
    ZSTLMapBucket_t  *pb;

    do_lock(&(pm->mutex));

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapSet: pm=%p, key=0x%lx, keylen=%d, pdata=%p, datalen=%ld\n", pm, *((uint64_t *) pkey), keylen, pdata, datalen);
    #endif

    pme = find_pme(pm, pkey, keylen, &pb);

    if (pme != NULL) {

	/* Update an existing entry. */

	if (pm->replacement_callback) {
	    //  return NULL for key so that it is not freed!
	    (pm->replacement_callback)(pm->replacement_callback_data, NULL, 0, pme->contents, pme->datalen);
	}

	*old_pdata   = pme->contents;
	*old_datalen = pme->datalen;

	pme->contents = pdata;
	pme->datalen  = datalen;

	/* update the lru list if necessary */
	if (pm->max_entries != 0) {
	    update_lru(pm, pme);
	}

    } else {

	/* Create a new entry. */

	pme = create_pme(pm, pkey, keylen, pdata, datalen);

	*old_pdata   = NULL;
	*old_datalen = 0;

	/* put myself on the bucket list */
	pme->bucket = pb;
	pme->next = pb->entry;
	pb->entry = pme;
	#ifdef LISTCHECK
	    check_list(pb, pme);
	#endif

	pm->n_entries++;

	/* put myself on the lru list */
	{
	    insert_lru(pm, pme);

	    if ((pm->max_entries != 0) && (pm->n_entries > pm->max_entries)) {
		// do an LRU replacement
		replace_lru(pm, pme);
	    }
	}
    }

    do_unlock(&(pm->mutex));
    return(pme);
}

//  Returns non-NULL if successful, NULL otherwise
struct ZSTLMapEntry *ZSTLMapGet(struct ZSTLMap *pm, char *key, uint32_t keylen, char **pdata, uint64_t *pdatalen)
{
    uint64_t           datalen;
    ZSTLMapEntry_t   *pme;
    char              *data;

    do_lock(&(pm->mutex));

    pme = find_pme(pm, key, keylen, NULL);

    if (pme != NULL) {
        data    = pme->contents;
	datalen = pme->datalen;

	// update the LRU list if necessary
	if (pm->max_entries != 0) {
	    update_lru(pm, pme);
	}

    } else {
        data    = NULL;
	datalen = 0;
    }

    #ifdef SANDISK_PRINTSTUFF
        if (pme != NULL) {
	    fprintf(stderr, "ZSTLMapGet: pm=%p, key=0x%lx, keylen=%d, pdata=%p, datalen=%ld, refcnt=%d, pme=%p\n", pm, *((uint64_t *) key), keylen, data, datalen, pme->refcnt, pme);
	} else {
	    fprintf(stderr, "ZSTLMapGet: pm=%p, key=0x%lx, keylen=%d, pdata=%p, datalen=%ld, pme=%p\n", pm, *((uint64_t *) key), keylen, data, datalen, pme);
	}

	#ifdef notdef
	    void* tracePtrs[100];
	    int count = backtrace( tracePtrs, 100 );

	    char** funcNames = backtrace_symbols( tracePtrs, count );

	    // Print the stack trace
	    printf("---------------------------------------------------------------------------\n");
	    for( int ii = 0; ii < count; ii++ ) {
		printf( "%s\n", funcNames[ii] );
	    }
	    printf("---------------------------------------------------------------------------\n");

	    // Free the string pointers
	    free( funcNames );
	#endif
    #endif

    do_unlock(&(pm->mutex));
    *pdatalen = datalen;
    *pdata    = data;
    return(pme);
}

//  Increment the reference count for this entry
//  rc=1 if entry is found, rc=0 otherwise
int ZSTLMapIncrRefcnt(struct ZSTLMap *pm, char *key, uint32_t keylen)
{
    ZSTLMapEntry_t   *pme;
    int                rc = 0;

    do_lock(&(pm->mutex));

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapIncrRefcnt: pm=%p, key=0x%lx, keylen=%d", pm, *((uint64_t *) key), keylen);
    #endif

    pme = find_pme(pm, key, keylen, NULL);

    if (pme != NULL) {
// fprintf(stderr, ", after incr refcnt [0x%lx] =%d\n", *((uint64_t *) key), pme->refcnt + 1);
	#ifdef SANDISK_PRINTSTUFF
	    fprintf(stderr, ", after incr refcnt=%d\n", pme->refcnt + 1);
	#endif
        (pme->refcnt)++;
	rc = 1;
    } else {
	#ifdef SANDISK_PRINTSTUFF
	    fprintf(stderr, " (KEY NOT FOUND!)\n");
	#endif
    }

    do_unlock(&(pm->mutex));
    return(rc);
}

//  Decrement the reference count for this entry
//  rc=1 if entry is found, rc=0 otherwise
int ZSTLMapRelease(struct ZSTLMap *pm, char *key, uint32_t keylen)
{
    ZSTLMapEntry_t   *pme;
    int                rc = 0;

    do_lock(&(pm->mutex));

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapRelease: pm=%p, key=0x%lx, keylen=%d", pm, *((uint64_t *) key), keylen);
    #endif

    pme = find_pme(pm, key, keylen, NULL);

    if (pme != NULL) {
// fprintf(stderr, ", after release [0x%lx] =%d\n", *((uint64_t *) key), pme->refcnt - 1);
	#ifdef SANDISK_PRINTSTUFF
	    fprintf(stderr, ", after release refcnt=%d\n", pme->refcnt - 1);
	#endif
        (pme->refcnt)--;
	rc = 1;
    } else {
	#ifdef SANDISK_PRINTSTUFF
	    fprintf(stderr, " (KEY NOT FOUND!)\n");
	#endif
    }

    do_unlock(&(pm->mutex));
    return(rc);
}

//  Decrement the reference count for this entry
int ZSTLMapReleaseEntry(struct ZSTLMap *pm, struct ZSTLMapEntry *pme)
{
    int rc = 0;

    do_lock(&(pm->mutex));

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapReleaseEntry: pm=%p, pme=%p\n", pm, pme);
    #endif

    if (pme != NULL) {
        (pme->refcnt)--;
	rc = 1;
    }

    do_unlock(&(pm->mutex));
    return(rc);
}

struct ZSTLIterator *ZSTLMapEnum(struct ZSTLMap *pm)
{
    ZSTLIterator_t    *iterator;

    pthread_mutex_lock(&(pm->enum_mutex));

    do_lock(&(pm->mutex));

    iterator = get_iterator(pm);
    zstlmap_assert(iterator);

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapEnum: pm=%p, iterator=%p, ", pm, iterator);
    #endif

    iterator->enum_entry = pm->lru_head;

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "pme=%p, enum_entry=%p\n", pm, iterator->enum_entry);
    #endif

    do_unlock(&(pm->mutex));

    return(iterator);
}

void ZSTLFinishEnum(struct ZSTLMap *pm, struct ZSTLIterator *iterator)
{
    do_lock(&(pm->mutex));

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLFinishEnum: pm=%p, iterator=%p\n", pm, iterator);
    #endif

    free_iterator(pm, iterator);
    pthread_mutex_unlock(&(pm->enum_mutex));
    do_unlock(&(pm->mutex));
}

//  Returns 1 if successful, 0 otherwise
//  Caller is responsible for freeing key and data
int ZSTLMapNextEnum(struct ZSTLMap *pm, struct ZSTLIterator *iterator, char **key, uint32_t *keylen, char **data, uint64_t *datalen) 
{
    ZSTLMapEntry_t    *pme_return;

    do_lock(&(pm->mutex));

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapNextEnum: pm=%p, iterator=%p, ", pm, iterator);
    #endif

    if (iterator->enum_entry == NULL) {
	do_unlock(&(pm->mutex));
	ZSTLFinishEnum(pm, iterator);

	#ifdef SANDISK_PRINTSTUFF
	    fprintf(stderr, "pdata=NULL, datalen=0\n");
	#endif

        *key     = NULL;
	*keylen  = 0;
        *data    = NULL;
	*datalen = 0;
        return(0);
    }
    pme_return = iterator->enum_entry;

    iterator->enum_entry = pme_return->next_lru;

    if (pme_return != NULL) {
	*key     = pme_return->key;
	*keylen  = pme_return->keylen;

	*data    = (char *) pme_return->contents;
	*datalen = pme_return->datalen;

	#ifdef SANDISK_PRINTSTUFF
	    fprintf(stderr, "key=%p, keylen=%d, pdata=%p, datalen=%ld, pme_return=%p\n", *key, *keylen, *data, *datalen, pme_return);
	#endif

	do_unlock(&(pm->mutex));
	return(1);
    } else {
	ZSTLFinishEnum(pm, iterator);

	#ifdef SANDISK_PRINTSTUFF
	    fprintf(stderr, "pdata=NULL, datalen=0\n");
	#endif

        *key     = NULL;
	*keylen  = 0;
        *data    = NULL;
	*datalen = 0;
	do_unlock(&(pm->mutex));
	return(0);
    }
}

/*   Return 0 if succeeds, 1 if object doesn't exist.
 */
int ZSTLMapDelete(struct ZSTLMap *pm, char *key, uint32_t keylen)
{
    uint64_t            h;
    ZSTLMapEntry_t   **ppme;
    ZSTLMapEntry_t    *pme;
    ZSTLMapBucket_t   *pb;
    char               *key2;

    key2 = (char *) *((uint64_t *) key);

    do_lock(&(pm->mutex));

    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "ZSTLMapDelete: pm=%p, key=0x%lx, keylen=%d\n", pm, *((uint64_t *) key), keylen);
    #endif

    h = zs_hash((const unsigned char *) key, sizeof(uint64_t), 0) % pm->nbuckets;
    pb = &(pm->buckets[h]);

    zstlmap_assert(keylen == 8); // remove this! xxxzzz

    for (ppme = &(pb->entry); (*ppme) != NULL; ppme = &((*ppme)->next)) {
	pme = *ppme;
	if (pme->key == key2) {

	    //  Remove from the LRU list if necessary
	    remove_lru(pm, pme);
	    pm->n_entries--;

            *ppme = pme->next;
            free_pme(pm, pme);
	    do_unlock(&(pm->mutex));
            return (0);
        }
    }
    do_unlock(&(pm->mutex));
    return (1);
}

static ZSTLMapEntry_t *create_pme(ZSTLMap_t *pm, char *pkey, uint32_t keylen, char *pdata, uint64_t datalen)
{
    ZSTLMapEntry_t   *pme;

    pme = get_entry(pm);

    if (keylen == 8) {
	pme->key = (char *) *((uint64_t *) pkey);
    } else {
        zstlmap_assert(0);
    }
    pme->keylen   = keylen;
    pme->refcnt   = 0;

    pme->contents  = pdata;
    pme->datalen   = datalen;

    pme->next      = NULL;

    return(pme);
}

#ifdef notdef
static ZSTLMapEntry_t *copy_pme(ZSTLMap_t *pm, ZSTLMapEntry_t *pme)
{
    ZSTLMapEntry_t  *pme2;

    pme2 = get_entry(pm);

    pme2->key    = pme->key;
    pme2->keylen = pme->keylen;

    pme2->contents = pme->contents;
    pme2->datalen  = pme->datalen;

    #ifdef SANDISK_PRINTSTUFF
        fprintf(stderr, "copy_pme [from pme %p to %p] key=%p, keylen=%d, data=%p, datalen=%ld\n", pme, pme2, pme->key, pme->keylen, pme->contents, pme->datalen);
    #endif

    pme2->next   = NULL;
    pme2->bucket = pme->bucket;

    return(pme2);
}

static ZSTLMapEntry_t *copy_pme_list(ZSTLMap_t *pm, ZSTLMapEntry_t *pme_in)
{
    ZSTLMapEntry_t  *pme, *pme2, *pme_out;

    pme_out = NULL;
    for (pme = pme_in; pme != NULL; pme = pme->next) {
        pme2 = copy_pme(pm, pme);
	pme2->next = pme_out;
	pme_out = pme2;
    }
    return(pme_out);
}
#endif

static void free_pme(ZSTLMap_t *pm, ZSTLMapEntry_t *pme)
{
    free_entry(pm, pme);
}

static void free_pme_list(ZSTLMap_t *pm, ZSTLMapEntry_t *pme_in)
{
    ZSTLMapEntry_t  *pme, *pme_next;

    for (pme = pme_in; pme != NULL; pme = pme_next) {
        pme_next = pme->next;
        free_pme(pm, pme);
    }
}

static void insert_lru(struct ZSTLMap *pm, ZSTLMapEntry_t *pme)
{
    pme->next_lru = pm->lru_head;
    if (pm->lru_head != NULL) {
        pm->lru_head->prev_lru = pme;
    }
    pme->prev_lru = NULL;
    pm->lru_head = pme;
    if (pm->lru_tail == NULL) {
	pm->lru_tail = pme;
    }
}

static void remove_lru(struct ZSTLMap *pm, ZSTLMapEntry_t *pme)
{
    if (pme->next_lru == NULL) {
	zstlmap_assert(pm->lru_tail == pme); // xxxzzz remove this!
	pm->lru_tail = pme->prev_lru;
    } else {
	pme->next_lru->prev_lru = pme->prev_lru;
    }
    if (pme->prev_lru == NULL) {
	zstlmap_assert(pm->lru_head == pme); // xxxzzz remove this!
	pm->lru_head = pme->next_lru;
    } else {
	pme->prev_lru->next_lru = pme->next_lru;
    }
}

static void update_lru(struct ZSTLMap *pm, ZSTLMapEntry_t *pme)
{
    //  remove pme from list
    remove_lru(pm, pme);
    //  insert_lru(pm, pme)
    insert_lru(pm, pme);
}

static void replace_lru(struct ZSTLMap *pm, ZSTLMapEntry_t *pme_in)
{
    ZSTLMapEntry_t **ppme;
    ZSTLMapEntry_t  *pme;
    int               found_it;

    for (pme=pm->lru_tail; pme != NULL; pme = pme->prev_lru) {
        zstlmap_assert(pme->refcnt >= 0); // xxxzzz remove this!
        if ((pme->refcnt == 0) && (pme != pme_in)) {
	    break;
	}
    }
    if (pme == NULL) {
	fprintf(stderr, "replace_lru could not find a victim!!!!\n");
	zstlmap_assert(0);
    }
    #ifdef SANDISK_PRINTSTUFF
	fprintf(stderr, "replace_lru found a victim: %p\n", pme);
    #endif

    //  Remove from bucket list
    found_it = 0;
    for (ppme = &(pme->bucket->entry); (*ppme) != NULL; ppme = &((*ppme)->next)) {
	if (pme == (*ppme)) {
	    *ppme = pme->next;
	    found_it = 1;
	    break;
	}
    }
    zstlmap_assert(found_it);

    remove_lru(pm, pme);
    pm->n_entries--;
    (pm->replacement_callback)(pm->replacement_callback_data, pme->key, pme->keylen, pme->contents, pme->datalen);
    free_pme(pm, pme);

}

static ZSTLMapEntry_t *find_pme(struct ZSTLMap *pm, char *pkey, uint32_t keylen, ZSTLMapBucket_t **pb_out)
{
    uint64_t           h;
    ZSTLMapBucket_t  *pb;
    ZSTLMapEntry_t   *pme = NULL;
    char              *key2;
    key2 = (char *) *((uint64_t *) pkey);

    h = zs_hash((const unsigned char *) pkey, keylen, 0) % pm->nbuckets;
    pb = &(pm->buckets[h]);

    if (keylen == 8) {
	for (pme = pb->entry; pme != NULL; pme = pme->next) {
	    if (pme->key == key2) {
		break;
	    }
	}
    } else {
        zstlmap_assert(0);
    }
    if (pb_out) {
        *pb_out = pb;
    }
    return(pme);
}

#ifdef LISTCHECK
static void check_list(ZSTLMapBucket_t *pb, ZSTLMapEntry_t *pme_in)
{
    ZSTLMapEntry_t *pme, *pme2;
    //  check for a circular list

    for (pme = pb->entry; pme != NULL; pme = pme->next) {
	for (pme2 = pme->next; pme2 != NULL; pme2 = pme2->next) {
	    if (pme2 == pme) {
                fprintf(stderr, "Circular pme list!\n");
		zstlmap_assert(0);
	    }
	}
    }
}
#endif


