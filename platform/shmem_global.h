/*
 * File:   shmem_global.h
 * Author: Jim
 *
 * Created on February 29, 2008
 *
 * (c) Copyright 2008, Schooner Information Technology, Inc.
 * http:                                     //www.schoonerinfotech.com/
 *
 * $Id: shmem_global.h 396 2008-02-29 22:55:43Z jim $
 */

#ifndef SHMEM_GLOBAL_H
#define SHMEM_GLOBAL_H

#include "platform/defs.h"

/**
 * @brief Global namespace for shmem global variables
 *
 *  shmem globals are arbitrary 64-bit values and can be used for any purpose.  If they
 *  are shared (and since they are 'global" they probably are) all users must agree on
 *  what type they actually are and cast appropriately.
 */

enum plat_shmem_global_id {
    SHMEM_GLOBAL_PTOF_MBOX_PTRS,
    SHMEM_GLOBAL_IDLE_CONTROL,
    SHMEM_GLOBAL_BACKTRACE_SET,
    SHMEM_GLOBAL_FFDC,

    SHMEM_GLOBAL_COUNT                       // *** MUST BE LAST ***
};

/**
 * @brief get the value of a global item using the global ID for the item
 *
 * @param globalId <IN> globalId
 * @return the value, or 0 for uninitialized (like BSS)
 */
uint64_t shmem_global_get(enum plat_shmem_global_id globalId);

/**
 * @brief set the value of a global item using the global ID for the item.
 *
 * The value will only be set if it is currently uninitialized.  Since val is
 * often a dynamically allocated shared memory object it is an error to ignore
 * the return.
 *
 * @param globalId <IN> globalId
 *
 * @param val <IN> value
 *
 * @return old value which means only a 0 return implies val was accepted.
 * Use #shmem_global_reset when it is desirable to replace an existing value.
 */
uint64_t shmem_global_set(enum plat_shmem_global_id globalId,
                          uint64_t val) __wur;

/**
 * @brief reset the value of a global item
 *
 * Since val is often a dynamically allocated shared memory object it is an
 * error to ignore the return.
 *
 * @param globalId <IN> globalId
 * @param val <IN> value
 * @return old value
 */
uint64_t shmem_global_reset(enum plat_shmem_global_id globalId,
                            uint64_t val) __wur;

#endif
