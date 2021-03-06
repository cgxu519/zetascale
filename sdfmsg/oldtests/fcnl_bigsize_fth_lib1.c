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
 * File:   fcnl_bigsize_fth_lib1.c
 * Author: Norman Xu
 *
 * Created on February 21, 2008, 11:45 AM
 *
 * (c) Copyright 2008, Schooner Information Technology, Inc.
 * http://www.schoonerinfotech.com/
 *
 * $Id: fcnl_bigsize_fth_lib1.c
 */

#ifdef MPI_BUILD
#include <mpi.h>
#endif

#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>

#include "platform/logging.h"
#include "sdfmsg/sdf_fth_mbx.h"
#include "sdfmsg/sdf_msg_types.h"
#include "fcnl_test.h"
#include "Utilfuncs.h"
#include "log.h"

extern uint32_t myid;
extern int sdf_msg_free_buff(sdf_msg_t *msg);
extern int outtahere;

#define DBGP 0
#define SENDTWO 0
#define DIVNUM 100
#define TSZE 1024
#define BIGSIZE 1000000
#define FIXSIZE 0
#define SHORTTEST 10
#define UNEXPT_TEST 0
#define FASTPATH_TEST 0
#define SHOWBUFF 0

//PLAT_LOG_CAT_LOCAL(LOG_CAT, "sdf/sdfmsg/tests/mpilog");
#define LOG_CAT PLAT_LOG_CAT_SDF_SDFMSG


/*
 * These fthreads will be used to simulate the SDF protocol sending and
 * receiving messages to the sdf messaging engine and across nodes
 */

struct sdf_queue_pair *q_pair_CONSISTENCY;
struct sdf_queue_pair *q_pair_RESPONSES;

static fthMbox_t ackmbox, respmbox, ackmbx1, ackmbx2;
static int cluster_node[MAX_NUM_SCH_NODES];

static int mysync = 0;
static struct startsync crp;
static startsync_t *ssync = &crp;

extern struct test_info * info;

/*
 * Here we locally create the queue pairs, for now both source and destination protocols
 * will be the same.
 */

/*struct sdf_queue_pair *
 local_create_myqpairs(service_t protocol, uint32_t myid) {
 struct sdf_queue_pair *q_pair;

 return(q_pair = (sdf_create_queue_pair(myid, myid == 0 ? 1 : 0,
 protocol, protocol, SDF_WAIT_FTH)));
 }*/

/*
 * This fth thread simulates the Action Node, it sends a CONSISTENCY msg and
 * sleeps on a mailbox waiting for a response. The msg types are arbitrary for now
 */

static void fthThreadSender(uint64_t arg) {
    int i, l;
    uint64_t aresp;
    struct sdf_msg *send_msg = NULL;
    vnode_t node;
    serviceid_t protocol = SDF_CONSISTENCY;
    serviceid_t my_protocol = SDF_CONSISTENCY;
    msg_type_t type = REQ_FLUSH;
    sdf_fth_mbx_t fthmbx;
    fthMbox_t *fthmbxtst;
    int debug = 0;
    int maxcnt;
    int msgsize;

#if SENDTWO
    maxcnt = 5;
#elif SHORTTEST
    maxcnt = SHORTTEST;
#else
    maxcnt = 990000;
#endif

    fthmbxtst = &respmbox;

    fthMboxInit(&ackmbox);
    fthMboxInit(&respmbox);

    fthmbx.actlvl = SACK_BOTH_FTH;
    fthmbx.abox = &ackmbox;
    fthmbx.rbox = &respmbox;

    printf("FTH Thread starting %s Number of msgs to send = %d arg in %li\n",
            __func__, maxcnt, arg);
    fflush(stdout);

    if (DBGP) {
        plat_log_msg(
                PLAT_LOG_ID_INITIAL,
                LOG_CAT,
                PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: fth mb info fthmbxtst %p rbox %p abox %p lvl %d maxcnt %d\n",
                myid, fthmbxtst, fthmbx.rbox, fthmbx.abox, SACK_BOTH_FTH,
                maxcnt);
    }

    /* node is the destination node */
    int localpn, actmask;
    uint32_t numprocs;
    int localrank = sdf_msg_nodestatus(&numprocs, &localpn, cluster_node, &actmask);
    if (numprocs == 1) {
        node = 0;
    }
    else {
        /* just get the next node in line, not really a multi-node test */
        node = local_get_pnode(localrank, localpn, numprocs);
        printf("Node %d: %s my pnode is  %d\n", localrank, __func__, node);
        fflush(stdout);
        for (i = 0; i < numprocs; i++) {
            printf("Node %d: %s cluster_node[%d] = %d\n", localrank, __func__, i, cluster_node[i]);
            fflush(stdout);
        }
    }

    /* you only init this once but share the q_pairs among the other threads here */

    q_pair_CONSISTENCY = local_create_myqpairs(SDF_CONSISTENCY, myid, node);
    info->queue_pair_info->queue_add[0] = q_pair_CONSISTENCY->q_in;
    info->queue_pair_info->queue_add[1] = q_pair_CONSISTENCY->q_out;
    info->queue_pair_info->queue_pair_type = SDF_CONSISTENCY; 
    if (q_pair_CONSISTENCY == NULL) {
        fprintf(stderr, "%s: sdf_create_queue_pair failed\n", __func__);
        return;
    }
    /* right now we are not using shmem for these buffers */

    plat_log_msg(
            PLAT_LOG_ID_INITIAL,
            LOG_CAT,
            PLAT_LOG_LEVEL_TRACE,
            "\nNode %d: created queue pair %p sn %d dn %d ss %d ds %d maxcnt %d\n",
            myid, q_pair_CONSISTENCY, myid, node, SDF_CONSISTENCY,
            SDF_CONSISTENCY, maxcnt);

    /* main loop will send SDF_CONSISTENCY protocol messages till maxcnt is reached
     * this sleeps on both mailboxes ack and resp based on the lvl dictated
     */
    if (myid == 1) {
        debug = 0;
        if (debug) {
            plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                    "\nNode %d: DEBUG --- NOT SENDING MESSAGES FROM HERE", myid);
            while (debug)
                fthYield(100); /* Don't send mesages from node one for now */
        }
    }
    FTH_SPIN_LOCK(&ssync->spin);
    mysync = 1;
    FTH_SPIN_UNLOCK(&ssync->spin);

    sdf_msg_startmsg(myid, 0, NULL);

    msgsize = TSZE;
    for (l = 0; l < maxcnt; ++l) {
        sdf_msg_t *msg;
        unsigned char *m;
        int ret;

        /* create the buffer on every loop to check the buffer release func */
        if (FIXSIZE) {
            msgsize = BIGSIZE;
        } else {
            msgsize *= 2;
        }
        plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                "Node %d: msg %d with size %d\n", myid, l, msgsize);

        /* create the buffer on every loop to check the buffer release func */
        send_msg = (struct sdf_msg *) sdf_msg_alloc(msgsize);

        if (send_msg == NULL) {
            /* FIXME should default to an error  */
            plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_FATAL,
                    "sdf_msg_alloc(TSZE) failed\n");
            /* return ((void *)1); */
        }

        for (i = 0; i < msgsize; ++i)
            send_msg->msg_payload[i] = (unsigned char) i;

        type = REQ_FLUSH;

        ret = sdf_msg_send((struct sdf_msg *) send_msg, msgsize, node,
                protocol, myid, my_protocol, type, &fthmbx, NULL);

        if (ret != 0)
            process_ret(ret, protocol, type, myid);

        plat_log_msg(
                PLAT_LOG_ID_INITIAL,
                LOG_CAT,
                PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: Msg %d Posted ret %d proto %d type %d Now Sleep on Ack Mbox\n",
                myid, l, ret, protocol, type);

        aresp = fthMboxWait(&ackmbox);
        plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: Send Buff loop num %d Ack Freed aresp %ld\n", myid,
                l, aresp);

        if (!fthmbx.actlvl) {
            plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                    "\nNode %d: actvl %d\n", myid, fthmbx.actlvl);
            plat_assert(fthmbx.actlvl >= 1);
        } else {
            plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                    "\nNode %d: Sleeping on RESP %d  fth mailbox %p loop %d\n",
                    myid, l, &respmbox, l);
            /*
             * Sleep on the mailbox waiting to get a properly directed response message
             */
            msg = (sdf_msg_t *) fthMboxWait(&respmbox);
            
            m = (unsigned char *) msg;

            plat_log_msg(
                    PLAT_LOG_ID_INITIAL,
                    LOG_CAT,
                    PLAT_LOG_LEVEL_TRACE,
                    "\nNode %d: RESP %d msg %p seq %lu sn %d dn %d proto %d type %d len %d loop %d\n",
                    myid, l, msg, msg->msg_conversation, msg->msg_src_vnode,
                    msg->msg_dest_vnode, msg->msg_dest_service, msg->msg_type, msg->msg_len, l);
            /*
             * Print out the buffer contents that we just got
             */
#if SHOWBUFF
            for (i = 0; i < 256; i++) {
                printf(" %02x", *m);
                m++;
                if ((i % 16) == 15) {
                    putchar('\n');
                    fflush(stdout);
                }
            }
#endif
            /* release the receive buffer back to the sdf messaging thread */
            ret = sdf_msg_free_buff(msg);

            plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                    "\nNode %d: Returned Buff %d ret %d\n", myid, l, ret);

            fthYield(1); /* we yield to give others a chance to do some work */
        }
    }
    while (mysync != 3)
        fthYield(100);
    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nNode %d: EXITING completed sending %d messages - mysync %d\n",
            myid, l, mysync);
    fthKill(5); // Kill off FTH
}

/* This FTH thread will act as a protocol worker thread.
 * It will wait for CONSISTENCY Messages
 * on the queue, processes them and returns appropriate RESPONSE messages
 */

static void fthThreadReceiver1(uint64_t arg) {
    int i, ret, ct = 0;
    uint64_t aresp;
    struct sdf_msg *send_msg = NULL, *recv_msg = NULL;
    vnode_t node;
    serviceid_t protocol = SDF_RESPONSES;
    serviceid_t my_protocol = SDF_RESPONSES;
    msg_type_t type = RESP_ONE;
    sdf_fth_mbx_t fthmbx;

    fthmbx.actlvl = SACK_ONLY_FTH;
    fthmbx.abox = &ackmbx1;
    fthmbx.rbox = NULL;
    fthMboxInit(&ackmbx1);

    printf("FTH Thread starting %s\n", __func__);

    /* node is the destination node */
    int localpn, actmask;
    uint32_t numprocs;
    int localrank = sdf_msg_nodestatus(&numprocs, &localpn, cluster_node, &actmask);
    if (FASTPATH_TEST) {
        node = myid;
        plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: FASTPATH_TEST node %d myid %d\n", myid, node, myid);
    } else {
        /* just get the next node in line, not really a multi-node test */
        node = local_get_pnode(localrank, localpn, numprocs);
        printf("Node %d: %s my pnode is  %d\n", localrank, __func__, node);
        fflush(stdout);
        for (i = 0; i < numprocs; i++) {
            printf("Node %d: %s cluster_node[%d] = %d\n", localrank, __func__, i, cluster_node[i]);
            fflush(stdout);
        }
    }

    q_pair_RESPONSES = local_create_myqpairs(SDF_RESPONSES, myid, node);

    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nNode %d: created queue pair %p sn %d dn %d ss %d ds %d\n", myid,
            q_pair_RESPONSES, myid, node, SDF_RESPONSES,
            SDF_RESPONSES);
    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nNode %d: Now yielding waiting for mysync\n", myid);

    /* Need to yield till all queues have been created */
    while (!mysync)
        fthYield(1);

    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nNode %d: Ready To Accept 1st MESSAGE sack lvl %d\n\n\n", myid,
            fthmbx.actlvl);

    for (;;) {

        plat_log_msg(
                PLAT_LOG_ID_INITIAL,
                LOG_CAT,
                PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: Waiting for messages q_pair_CONSISTENCY %p loop%d\n",
                myid, q_pair_CONSISTENCY, ct);
        recv_msg = sdf_msg_receive(q_pair_CONSISTENCY->q_out, 0, B_TRUE);
        
        plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: Got One *msg %p sn %d dn %d proto %d type %d size %d"
                    " akrpmbx %p\n", myid, recv_msg, recv_msg->msg_src_vnode,
                recv_msg->msg_dest_vnode, recv_msg->msg_dest_service,
		     recv_msg->msg_type, recv_msg->msg_len, recv_msg->akrpmbx);

#if SHOWBUFF
        unsigned char *m = (unsigned char *)recv_msg;
        for (i = 0; i < 256; i++) {
            printf(" %02x", *m);
            m++;
            if ((i % 16) == 15) {
                printf("  myid %d", myid);
                putchar('\n');
                fflush(stdout);
            }
        }
#endif

        send_msg = (struct sdf_msg *) sdf_msg_alloc(recv_msg->msg_len);
        if (send_msg == NULL) {
            plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_FATAL,
                    "sdf_msg_alloc(recv_msg->msg_len) failed\n");
            /* return ((void *)1); */
        }

                for (i = 0; i < TSZE; ++i)
                    send_msg->msg_payload[i] = (unsigned char) 0x69;

        //memcpy(send_msg, recv_msg, recv_msg->msg_len);
        struct sdf_resp_mbx rhkey;
        struct sdf_resp_mbx *ptrkey = &rhkey;

        strncpy(rhkey.mkey, MSG_DFLT_KEY, (MSG_KEYSZE - 1));
        rhkey.mkey[MSG_KEYSZE - 1] = '\0';
        rhkey.akrpmbx_from_req = NULL;
        rhkey.rbox = NULL;

        plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: Sending MSG dn %d ds %d sn %d ss %d type %d"
                    " akrpmbx %p send_msg %p\n", myid, node, protocol, myid,
                my_protocol, type, recv_msg->akrpmbx, send_msg);

        ret = sdf_msg_send((struct sdf_msg *) send_msg, recv_msg->msg_len, node, protocol,
                myid, my_protocol, type, &fthmbx, sdf_msg_get_response(
                        recv_msg, ptrkey));
        if (ret != 0) {
            process_ret(ret, protocol, type, myid);
        }

        /* release the receive buffer back to the sdf messaging thread */
        ret = sdf_msg_free_buff(recv_msg);

        aresp = fthMboxWait(&ackmbx1);
        plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: Send Buff Freed aresp %ld loop %d\n", myid, aresp,
                ct);

        ct++;
        if (SHORTTEST) {
	    if (ct == (SHORTTEST / 2))
                break;
	}
        /* Simple exit mechanism, worker threads will just quit when predefined msgcnt
         * has been reached in the sender thread
         */
    }
    mysync++;
    fthYield(1);
    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nNode %d: WORKER FTH exiting - loop %d mysync %d\n", myid, i,
            mysync);
}

/* This 2nd FTH thread will act as a protocol worker thread. It will wait for CONSISTENCY Messages
 * on the queue, processes them and returns appropriate RESPONSE messages
 */

static void fthThreadReceiver2(uint64_t arg) {
    int i, ret, ct = 0;
    uint64_t aresp;
    struct sdf_msg *send_msg = NULL, *recv_msg = NULL;
    vnode_t node;
    serviceid_t protocol = SDF_RESPONSES;
    serviceid_t my_protocol = SDF_RESPONSES;
    msg_type_t type = RESP_TWO;
    sdf_fth_mbx_t fthmbx;

    fthmbx.actlvl = SACK_ONLY_FTH;
    fthmbx.abox = &ackmbx2;
    fthMboxInit(&ackmbx2);

    printf("FTH Thread starting %s\n", __func__);

    /* node is the destination node */
    int localpn, actmask;
    uint32_t numprocs;
    int localrank = sdf_msg_nodestatus(&numprocs, &localpn, cluster_node, &actmask);
    if (FASTPATH_TEST) {
        node = myid;
        plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: FASTPATH_TEST node %d myid %d\n", myid, node, myid);
    } else {
        /* just get the next node in line, not really a multi-node test */
        node = local_get_pnode(localrank, localpn, numprocs);
        printf("Node %d: %s my pnode is  %d\n", localrank, __func__, node);
        fflush(stdout);
        for (i = 0; i < numprocs; i++) {
            printf("Node %d: %s cluster_node[%d] = %d\n", localrank, __func__, i, cluster_node[i]);
            fflush(stdout);
        }
    }

    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nNode %d: Found queue pair %p sn %d dn %d ss %d ds %d loop %d\n",
            myid, q_pair_RESPONSES, myid, node, SDF_RESPONSES,
            SDF_RESPONSES, ct);
    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nNode %d: Now yielding waiting for mysync\n", myid);

    /* Need to yield till all queues have been created */
    while (!mysync)
        fthYield(1);
    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nNode %d: Ready To Accept 1st MESSAGE sack lvl %d loop %d\n\n\n",
            myid, fthmbx.actlvl, ct);

    for (;;) {

        plat_log_msg(
                PLAT_LOG_ID_INITIAL,
                LOG_CAT,
                PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: Waiting for messages q_pair_CONSISTENCY %p loop %d\n",
                myid, q_pair_CONSISTENCY, ct);

        recv_msg = sdf_msg_receive(q_pair_CONSISTENCY->q_out, 0, B_TRUE);

        plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: Got One *msg %p sn %d dn %d proto %d type %d len %d"
                    " akrpmbx %p\n", myid, recv_msg, recv_msg->msg_src_vnode,
                recv_msg->msg_dest_vnode, recv_msg->msg_dest_service,
                recv_msg->msg_type, recv_msg->msg_len, recv_msg->akrpmbx);

#if SHOWBUFF
        unsigned char *m = (unsigned char *)recv_msg;
        for (i = 0; i < 256; i++) {
            printf(" %02x", *m);
            m++;
            if ((i % 16) == 15) {
                printf("  myid %d", myid);
                putchar('\n');
                fflush(stdout);
            }
        }
#endif
        fthYield(1); /* let's give it up here */

        send_msg = (struct sdf_msg *) sdf_msg_alloc(recv_msg->msg_len);
        if (send_msg == NULL) {
            plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                    "sdf_msg_alloc(recv_msg->msg_len) failed\n");
            /* return ((void *)1); */
        }
        
        for(i = 0; i < recv_msg->msg_len; i++)
            send_msg->msg_payload[i] = (unsigned char)0x55;
        //memcpy(send_msg, recv_msg, recv_msg->msg_len);

        plat_log_msg(
                PLAT_LOG_ID_INITIAL,
                LOG_CAT,
                PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: Posting reply MSG dn %d ds %d sn %d ss %d type %d loop %d\n",
                myid, node, protocol, myid, my_protocol, type, ct);
        
        struct sdf_resp_mbx rhkey;
        struct sdf_resp_mbx *ptrkey = &rhkey;

        strncpy(rhkey.mkey, MSG_DFLT_KEY, (MSG_KEYSZE - 1));
        rhkey.mkey[MSG_KEYSZE - 1] = '\0';
        rhkey.akrpmbx_from_req = NULL;
        rhkey.rbox = NULL;

        ret = sdf_msg_send((struct sdf_msg *) send_msg, recv_msg->msg_len, node, protocol,
                myid, my_protocol, type, &fthmbx, sdf_msg_get_response(
                        recv_msg, ptrkey));
        if (ret != 0)
            process_ret(ret, protocol, type, myid);

        /* release the receive buffer back to the sdf messaging thread */
        ret = sdf_msg_free_buff(recv_msg);

        aresp = fthMboxWait(&ackmbx2);
        plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
                "\nNode %d: Send Buff Freed aresp %ld loop %d\n", myid, aresp,
                ct);
        ct++;
        if (SHORTTEST) {
	    if (ct == (SHORTTEST / 2))
                break;
	}
        /* Simple exit mechanism, worker threads will just quit when predefined msgcnt
         * has been reached in the sender thread
         */
    }
    mysync++;
    fthYield(1);
    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nNode %d: WORKER FTH exiting - loop %d\n mysync %d", myid, i,
            mysync);
}

void * BigSizeFthPthreadRoutine(void *arg) {

    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nNode %d FTH threads firing up\n", myid);

    fthInit(); // Init a scheduler

    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nNode %d FTH scheduler has initialized\n", myid);

    // Start a thread
    fthResume(fthSpawn(&fthThreadReceiver1, 40960), 1);
    fthResume(fthSpawn(&fthThreadReceiver2, 40960), 2); // Start a thread
    usleep(500);
    fthResume(fthSpawn(&fthThreadSender, 40960), 0);

    fthSchedulerPthread(0);

    plat_log_msg(PLAT_LOG_ID_INITIAL, LOG_CAT, PLAT_LOG_LEVEL_TRACE,
            "\nFTH scheduler halted\n");

    return (0);

}
