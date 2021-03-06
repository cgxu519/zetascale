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

#ifndef REPLICATION_REPLICATOR_ADAPTER_H
#define REPLICATION_REPLICATOR_ADAPTER_H 1

/*
 * File:   sdf/protocol/replication/replicator.h
 *
 * Author: drew
 *
 * Created on April 18, 2008
 *
 * (c) Copyright 2008, Schooner Information Technology, Inc.
 * http://www.schoonerinfotech.com/
 *
 * $Id: replicator_adapter.h 1480 2008-06-05 09:23:13Z drew $
 */

/**
 * Interface an sdf_replicator to sdf/sdf_msg and internal sdf/shared
 * code so subsequent implementations (Paxos-based) share the same complex
 * glue and unit testing is readily accomodated with all of the used
 * interfaces being specifically enumerated.
 *
 * As of 2008-06-09, the mess comes from
 *
 * 1.  The internal client code lacks an asynchronous interface which
 *     would make it easy to multiple operations together like fan-out.
 *
 * 2.  The messaging layer as of requires queue pairs to be
 *     defined for each popssible connection.
 *
 */

#include "platform/closure.h"
#include "platform/defs.h"

#include "replicator.h"

PLAT_CLOSURE(sdf_replicator_adapter_shutdown);

__BEGIN_DECLS

/**
 * @brief Allocate a replicator adapter
 *
 * To allow construction of messaging layer structures, etc. SDF components
 * have separate construction and start phases.
 *
 * #sdf_replicator_adapter_start shall be called after construction is
 * complete.
 *
 * @param replicator_alloc <IN> Allocate the replicator
 * @param config <IN> configuration owned by the caller
 * @return new replicator adapter, call #sdf_replicator_adapter_shutdown
 * to stop.
 */
struct sdf_replicator_adapter *
sdf_replicator_adapter_alloc(const struct sdf_replicator_config *config,
                             struct sdf_replicator *(*replicator_alloc)
                             (const struct sdf_replicator_config *config,
                              struct sdf_replicator_api *api,
                              void *extra),
                             void *replicator_alloc_extra);

/**
 * @brief Get the replicator for client use
 *
 * XXX: It may be more elegant to have the replicator adapter return
 * a pointer to its sdf_replicator_callbacks structure and let the user
 * construct the replicator.
 */
struct sdf_replicator *
sdf_replicator_adapter_get_replicator(struct sdf_replicator_adapter *adapter);

/**
 * @brief Start replicator
 *
 * @return 0 on success, non-zero on failure
 */
int sdf_replicator_adapter_start(struct sdf_replicator_adapter *adapter);

/**
 * @brief Shutdown replicator adapter
 *
 * @param adapter <IN> the adapter
 * @param shutdown_closure <IN> when not sdf_replicator_adapter_shutdown_null,
 * the closure is applied when the asynchronous shutdown process completes.
 */
void
sdf_replicator_adapter_shutdown(struct sdf_replicator_adapter *adapter,
                                sdf_replicator_shutdown_cb_t shutdown_closure);


__END_DECLS

#endif /* ndef REPLICATION_REPLICATOR_ADAPTER_H */
