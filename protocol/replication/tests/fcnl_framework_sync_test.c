/*
 * File:   fcnl_framework_sync_test.c
 * Author: Zhenwei Lu
 *
 * Note:test case for replication test_framework
 * Created on Nov 17, 2008, 12:04 AM
 *
 * Copyright Schooner Information Technology, Inc.
 * http://www.schoonerinfotech.com/
 *
 * $Id: fcnl_framework_sync_test.c 9804 2009-06-02 08:50:48Z lzwei $
 */


#include "fth/fthOpt.h"
#include "platform/stdio.h"
#include "test_framework.h"

#undef MILLION
#define MILLION 1000000

#define RT_USE_COMMON 1
#include "test_common.h"

#define CRASH_INCLUDED
// #define RESTART_NODE

/*
 * We use a sub-category under test because test implies a huge number
 * of log messages out of simulated networking, flash, etc.
 */
PLAT_LOG_SUBCAT_LOCAL(LOG_CAT, PLAT_LOG_CAT_SDF_PROT_REPLICATION,
                      "test/case");

#define PLAT_OPTS_NAME(name) name ## _sync_test
#include "platform/opts.h"
#include "misc/misc.h"

#define PLAT_OPTS_ITEMS_sync_test() \
    PLAT_OPTS_COMMON_TEST(common_config)

#define SLEEP_US 1000000

struct plat_opts_config_sync_test {
    struct rt_common_test_config common_config;
};

/**
 * @brief synchronized create_shard/write/read/delete/delete_shard operations
 */
void
user_operations_sync(uint64_t args) {
    struct replication_test_framework *test_framework =
            (struct replication_test_framework *)args;

    SDF_boolean_t op_ret = SDF_FALSE;
    struct SDF_shard_meta *shard_meta = NULL;
    SDF_replication_props_t *replication_props = NULL;
    int failed;
    SDF_shardid_t shard_id;
    vnode_t first_node = 0;
    vnode_t node_id = 0;
    struct timeval now;
    struct timeval when;
    /* timeval incre */
    struct timeval incre;

    void *data_read;
    size_t data_read_len;

    int t_read, t_write, t_delete, t_c_shard, t_d_shard;
    int read, write, delete, c_shard, d_shard;
    t_read = t_write = t_delete = t_c_shard = t_d_shard = 0;
    read = write = delete = c_shard = d_shard = 0;

    shard_id = __sync_add_and_fetch(&test_framework->max_shard_id, 1);
    char *key;
    char *data;

    failed = !plat_calloc_struct(&meta);
    plat_assert(!failed);
    replication_test_meta_init(meta);
    uint32_t r_iter, w_iter, d_iter;

    /* Assure test_framework is started?! */
    plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG, "start test_framework");
    rtfw_start(test_framework);
    plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG, "test_framework started\n");

    /* set now */
#ifdef RELATIVE_TIME
    test_framework->now.tv_sec = 0;
    test_framework->now.tv_usec = 0;
#else
    /* ABSOLUTE time */
    fthGetTimeOfDay(&test_framework->now);
#endif
    failed = !plat_calloc_struct(&replication_props);
    plat_assert(!failed);
    rtfw_set_default_replication_props(&test_framework->config,
                                       replication_props);
    shard_meta = rtfw_init_shard_meta(&test_framework->config,
                                      first_node /* first_node */,
                                      shard_id
                                      /* shard_id, in real system generated by generate_shard_ids() */,
                                      replication_props);

    plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG,
                 "\n**************************************************\n"
                 "                  create shard sync                 "
                 "\n**************************************************");
    op_ret = rtfw_create_shard_sync(test_framework, node_id, shard_meta);
    plat_assert(op_ret == SDF_SUCCESS);
    if (op_ret == SDF_SUCCESS) {
        (void) __sync_fetch_and_add(&c_shard, 1);
    }
    (void) __sync_fetch_and_add(&t_c_shard, 1);


    for (w_iter = 0; w_iter < test_framework->config.iterations; w_iter += 0) {
        plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG,
                     "\n**************************************************\n"
                     "                 write object sync                  "
                     "\n**************************************************");
        plat_asprintf(&key, "google:%d", w_iter);
        plat_asprintf(&data, "Sebstian:%d", w_iter);
#if 0
        key_len = GOOGLE + (int)log10(w_iter+0.5) + 1;
        data_len = SEBSTIAN + (int)log10(w_iter+0.5) + 1;
        key = plat_alloc(key_len+1);
        data = plat_alloc(data_len+1);
        sprintf(key, "google:%d", w_iter);
        sprintf(data, "Sebstian:%d", w_iter);
#endif

        plat_log_msg(LOG_ID, LOG_CAT, LOG_TRACE,
                     "write key:%s, key_len:%u, data:%s, data_len:%u",
                     key, (int)(strlen(key)), data, (int)(strlen(data)));
        op_ret = rtfw_write_sync(test_framework,
                                 shard_id /* shard */, node_id /* node */,
                                 meta /* test_meta */,
                                 key, strlen(key)+1, data, strlen(data)+1);
        plat_free(key);
        plat_free(data);
        if (op_ret == SDF_SUCCESS) {
            (void) __sync_fetch_and_add(&write, 1);
        }
        (void) __sync_fetch_and_add(&t_write, 1);
        (void) __sync_fetch_and_add(&w_iter, 1);
    }

    for (r_iter = 0; r_iter < test_framework->config.iterations; r_iter += 0) {
        plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG,
                     "\n**************************************************\n"
                     "                  read object sync                  "
                     "\n**************************************************");
        replication_test_framework_read_data_free_cb_t free_cb =
            replication_test_framework_read_data_free_cb_create(PLAT_CLOSURE_SCHEDULER_ANY_OR_SYNCHRONOUS,
                                                                &rtfw_read_free,
                                                                test_framework);
        plat_asprintf(&key, "google:%d", r_iter);
        plat_log_msg(LOG_ID, LOG_CAT, LOG_TRACE,
                     "KEY:%s, key_len:%d", key, (int)strlen(key));

        op_ret = rtfw_read_sync(test_framework, shard_id /* shard */, node_id /* node */, key, strlen(key) + 1,
                                &data_read, &data_read_len, &free_cb);
        plat_free(key);
        if (op_ret == SDF_SUCCESS) {
            (void) __sync_fetch_and_add(&read, 1);
            plat_log_msg(LOG_ID, LOG_CAT, LOG_TRACE,
                         "read data:%s, data_len:%d", (char *)data_read, (int)data_read_len);
            plat_free(data_read);
        }
        (void) __sync_fetch_and_add(&t_read, 1);
        (void) __sync_fetch_and_add(&r_iter, 1);
    }
#ifdef CRASH_INCLUDED
    plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG,
                 "\n**************************************************\n"
                 "                  crash node sync                  "
                 "\n**************************************************");
    rtfw_crash_node_sync(test_framework, first_node);
    plat_log_msg(LOG_ID, LOG_CAT, LOG_TRACE,
                 "crash node:%"PRIu32" complete", first_node);
#endif

    /* Block for a while */
    now = test_framework->now;
    incre.tv_sec = 10;
    incre.tv_usec = 0;
    timeradd(&now, &incre, &when);
    rtfw_block_until(test_framework, (const struct timeval)when);
    rtfw_sleep_usec(test_framework, SLEEP_US);
    /* delete object synchronous */
    for (d_iter = 0; d_iter < test_framework->config.iterations; d_iter += 0)  {
        plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG,
                     "\n***************************************************\n"
                     "                  delete object sync                 "
                     "\n***************************************************");
        plat_asprintf(&key, "google:%d", d_iter);
        plat_log_msg(LOG_ID, LOG_CAT, LOG_TRACE,
                     "KEY:%s, key_len:%d", key, (int)(strlen(key)));

        op_ret = rtfw_delete_sync(test_framework, shard_id /* shard */, node_id /* node */, key, strlen(key)+1);
        plat_free(key);
        if (op_ret == SDF_SUCCESS) {
            (void) __sync_fetch_and_add(&delete, 1);
        }
        (void) __sync_fetch_and_add(&t_delete, 1);
        (void) __sync_fetch_and_add(&d_iter, 1);
    }
#ifdef RESTART_NODE
    plat_log_msg(LOG_ID, LOG_CAT, LOG_INFO,
                 "\n**************************************************\n"
                 "              start crash node sync                  "
                 "\n**************************************************");
    rtfw_start_node(test_framework, first_node);
    plat_log_msg(LOG_ID, LOG_CAT, LOG_INFO,
                 "\n**************************************************\n"
                 "              start crash node finished              "
                 "\n**************************************************");

#endif
    plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG,
                 "\n**************************************************\n"
                 "                   delete shard sync                "
                 "\n**************************************************");
    op_ret = rtfw_delete_shard_sync(test_framework, node_id, shard_id);
    if (op_ret == SDF_SUCCESS) {
        (void) __sync_fetch_and_add(&d_shard, 1);
    }
    (void) __sync_fetch_and_add(&t_d_shard, 1);
    plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG,
                 "\n************************************************************\n"
                 "                  Test framework shutdown                       "
                 "\n************************************************************");
    rtfw_shutdown_sync(test_framework);

    plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG,
                 "\n************************************************************\n"
                 "                  Test framework sync summary                 "
                 "\n************************************************************");
    plat_log_msg(LOG_ID, LOG_CAT, LOG_INFO,
                 "\ncreat_shard :%d, Success:%d\n"
                 "write_sync  :%d, Success:%d\n"
                 "read_sync   :%d, Success:%d\n"
                 "delete_sync :%d, Success:%d\n"
                 "delete_shard:%d, Success:%d\n",
                 t_c_shard, c_shard, t_write, write, t_read, read,
                 t_delete, delete, t_d_shard, d_shard);
    plat_free(meta);
    plat_free(replication_props);
    plat_free(shard_meta);

    /* Terminate scheduler if idle_thread exit */
    while (test_framework->timer_dispatcher) {
        fthYield(-1);
    }
    plat_free(test_framework);

    fthKill(1);
}

int main(int argc, char **argv) {
    SDF_status_t status;
    struct replication_test_framework *test_framework = NULL;

    struct plat_opts_config_sync_test opts_config;
    memset(&opts_config, 0, sizeof (opts_config));

    rt_common_test_config_init(&opts_config.common_config);
    opts_config.common_config.test_config.nnode = 2;
    opts_config.common_config.test_config.replicator_config.lease_usecs =
        100 * MILLION;

    int opts_status = plat_opts_parse_sync_test(&opts_config, argc, argv);
    if (opts_status) {
        return (1);
    }

    status = rt_sm_init(&opts_config.common_config.shmem_config);
    if (status) {
        return (1);
    }

    /* start fthread library */
    fthInit();

    test_framework =
        replication_test_framework_alloc(&opts_config.common_config.test_config);
    if (test_framework) {
        plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG, "test_framework %p allocated\n",
                     test_framework);
    }
    XResume(fthSpawn(&user_operations_sync, 40960), (uint64_t)test_framework);
    fthSchedulerPthread(0);
    plat_log_msg(LOG_ID, LOG_CAT, LOG_DBG, "JOIN");

    rt_sm_detach();

    rt_common_test_config_destroy(&opts_config.common_config);

    return (0);
}

#include "platform/opts_c.h"
