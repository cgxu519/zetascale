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

#ifndef PLATFORM_AIO_API_INTERNAL_H
#define PLATFORM_AIO_API_INTERNAL_H 1

/*
 * File:   $URL: svn://svn.schoonerinfotech.net/schooner-trunk/trunk/sdf/platform/aio_api_internal.h $
 * Author: drew
 *
 * Created on March 8, 2010
 *
 * (c) Copyright 2010, Schooner Information Technology, Inc.
 * http://www.schoonerinfotech.com/
 *
 * $Id: aio_api_internal.h 12398 2010-03-20 00:58:14Z drew $
 */

/**
 * Implementation side of paio_api for asynchronous IO suppliers that look
 * like Linux libaio.
 */
#include <libaio.h>

struct paio_context;

/**
 * @brief Entry points for apis
 *
 * Instances may put a #paio_api structure at the start of their state
 * specific structure and type-pun.
 */
struct paio_api {
    /** @brief Destroy API.  Implementations must be pthread safe */
    void (*api_destroy)(struct paio_api *api);

    /*
     * All other APIs must be fth safe
     *
     * All APIs match their libaio behaviors, with errors indicated by
     * a -1 return with errno set to status.
     */
    int (*io_setup)(struct paio_api *api, int maxevents,
                    struct paio_context **ctxp);
    int (*io_destroy)(struct paio_context *ctx);
    int (*io_submit)(struct paio_context *ctx, long nr, struct iocb *ios[]);
    int (*io_cancel)(struct paio_context *ctx, struct iocb *iocb,
                     struct io_event *evt);
    long (*io_getevents)(struct paio_context *ctx_id, long min_nr, long nr,
                         struct io_event *events, struct timespec *timeout);
};

/**
 * @brief Context for a set of IOs, allocated with #paio_setup
 *
 * Instances may put a #paio_context structure at the start of their state
 * specific structure and type-pun.
 */
struct paio_context {
    /** @param Pointer to API structure */
    struct paio_api *api;

    /** @brief Number of in-flight writes */
    int writes_inflight_count;

    /** @brief Total size of in-flight writes */
    long writes_inflight_bytes;

    /** @brief Number of writes completed */
    int64_t writes_completed_count;

    /** @brief Total size of completed writes */
    int64_t writes_completed_bytes;
};

#endif /* ndef PLATFORM_AIO_API_INTERNAL_H */
