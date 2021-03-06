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
 * File:   ssd/fifo/mcd_osd_internal.h
 * Author: Xiaonan Ma/Brian O'Krafka
 *
 * Created on August 31, 2010
 *
 * (c) Copyright 2010, Schooner Information Technology, Inc.
 * http://www.schoonerinfotech.com/
 *
 * $Id: mcd_osd_internal.h 13657 2010-05-16 03:01:47Z briano $
 */

#ifndef __MCD_OSD_INTERNAL_H__
#define __MCD_OSD_INTERNAL_H__
     
#include "fth/fthSem.h"

#define MCD_LOGBUFS_PER_CNTR    2       // FIXME
#define MCD_CNTR_NAME_MAXLEN    64      // max container name length
#define MCD_CNTR_MAX_NUM_IPS    17      // max number of IPs per container

typedef struct mcd_container {

    int                         state;
    int                         prev_state;
    int                         need_reinstate;
    int                         defunct;
    uint32_t                    generation;
    uint32_t                    ref_count;

    /*
     * needed by the memcached protocol layer
     */
    time_t                      flush_time;
    int                         cas_num_nodes;
    int                         cas_node_id;
    uint64_t                    cas_id;

    /*
     * container configuration
     */
    int                         tcp_port;
    int                         udp_port;
    int                         eviction;
    int                         persistent;
    int                         container_id;
    char                        cname[MCD_CNTR_NAME_MAXLEN];
    char                        cluster_name[64];
    int                         sync_backup;
    int                         sync_updates;
    uint32_t                    sync_msec;
    uint64_t                    size_quota;
    uint32_t                    obj_quota;

    /*
     * stats
     */
    int                         hot_key_stats;
    int                         max_hot_keys;
    void                      * hot_key_reporter;

    /*
     * pointers to internal data structures
     */
    SDF_cguid_t                 cguid;
    SDFContainer                sdf_container;
    void                      * shard;

    /*
     * for ip_addr-per-container support
     */
    int                         num_ips;
    struct in_addr              ip_addrs[MCD_CNTR_MAX_NUM_IPS];

    /*
     * for SASL support
     */
    bool                        sasl;

    /*
     * for prefix-based deletion support
     */
    bool                        prefix_delete;
    void                      * pfx_deleter;

    /*
     * for fast binary tracing
     */
    bool                        binary_tracing;

} mcd_container_t;

extern mcd_container_t *Mcd_containers;

#endif  /* __MCD_OSD_INTERNAL_H__ */
