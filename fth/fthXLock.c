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
 * File:   fthXLock.c
 * Author: Jim
 *
 * Created on February 29, 2008
 *
 * (c) Copyright 2008, Schooner Information Technology, Inc.
 * http:                                     //www.schoonerinfotech.com/
 *
 * $Id: fthXLock.c 396 2008-02-29 22:55:43Z jim $
 */

/**
 * @brief X-thread locking.  This is the FTH-callable version
 */

#include <pthread.h>

#include "fthWaitEl.h"
#include "fthXLock.h"
#include "fth.h"

/**
 * @brief Initialize cross lock data structure
 */
void fthXLockInit(XLock_t *cross) {
    cross->fthLock = 0;
    pthread_rwlock_init(&cross->qLock, NULL);
    pthread_rwlock_init(&cross->lock, NULL);
}

/**
 * @brief Wait for cross lock (callable from fth thread ONLY).
 *
 * @param cross <IN> cross lock structure pointer
 * @param write <IN> Nonzero for write lock, zero for read lock
 */
void fthXLock(XLock_t *cross, int write) {

    // Wait for the fth lock to be free
    while (__sync_val_compare_and_swap(&cross->fthLock, 0, 1) != 0) {
        fthYield(0);
    }

    // Now aquire the queing lock (should be free except for race)
    while (pthread_rwlock_trywrlock(&cross->qLock) != 0) {
        // Another fthread might be waiting for the lock - avoid race
        fthYield(0);                         // Avoid race between 2 fthreads
    }

     // Release the FTH lock now that we have the Q lock
    (void) __sync_fetch_and_sub(&cross->fthLock, 1);

    // Now we have the pthread queueing lock so everyone will wait behind us
    if (write) {
        while (pthread_rwlock_trywrlock(&cross->lock) != 0) { // Try to get it
            fthYield(0);                     // Let everyone else run
        }
    } else {
        while (pthread_rwlock_tryrdlock(&cross->lock) != 0) { // Try to get it
            fthYield(0);                     // Let everyone else run
        }
   }

    // Release the Q lock now that we have the full lock
    pthread_rwlock_unlock(&cross->qLock);

}

/**
 * @brief Try for cross lock (callable from fth thread ONLY).
 *
 * @param cross <IN> cross lock structure pointer
 * @param write <IN> Nonzero for write lock, zero for read lock
 */
int fthXTryLock(XLock_t *cross, int write) {

    // Wait for the fth lock to be free
    if (__sync_val_compare_and_swap(&cross->fthLock, 0, 1) != 0) {
        return (1);
    }

    // Now aquire the queing lock (should be free except for race)
    while (pthread_rwlock_trywrlock(&cross->qLock) != 0) {
        // Another fthread might be waiting for the lock - avoid race
        fthYield(0);                         // Avoid race between 2 fthreads
    }

     // Release the FTH lock now that we have the Q lock
    (void) __sync_fetch_and_sub(&cross->fthLock, 1);

    // Now we have the pthread queueing lock so everyone will wait behind us
    if (write) {
        if (pthread_rwlock_trywrlock(&cross->lock) != 0) { // Try to get it
            pthread_rwlock_unlock(&cross->qLock); // We failed - clean up
            return (2);                      // Try failed
        }
    } else {
        if (pthread_rwlock_tryrdlock(&cross->lock) != 0) { // Try to get it
            pthread_rwlock_unlock(&cross->qLock); // We failed - clean up
            return (2);                      // Try failed
        }
    }

    // Release the Q lock now that we have the full lock
    pthread_rwlock_unlock(&cross->qLock);

    return (0);

}

/**
 * @brief Release cross lock (callable from fth thread ONLY).
 *
 * @param cross <IN> cross lock structure pointer
 */
void fthXUnlock(XLock_t *cross) {
    plat_assert_rc(pthread_rwlock_unlock(&cross->lock));
}
