/*
 * File:   name_service.c
 * Author: DO
 *
 * Created on January 15, 2008, 10:04 AM
 *
 * Copyright Schooner Information Technology, Inc.
 * http://www.schoonerinfotech.com/
 *
 * $Id: name_service.c 12335 2010-03-18 20:56:46Z briano $
 *
 */
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include "platform/string.h"
#include "platform/stdlib.h"
#include "platform/logging.h"
#include "cmc.h"
#include "container.h"
#include "container_props.h"
#include "name_service.h"
#include "shard_compute.h"
#include "init_sdf.h"


extern void UTStartDebugger(char *s); /* XXXZZZ */

extern SDF_cmc_t *theCMC; // Global CMC object

#define LOG_ID PLAT_LOG_ID_INITIAL
#define LOG_CAT PLAT_LOG_CAT_SDF_NAMING
#define LOG_DBG PLAT_LOG_LEVEL_DEBUG
#define LOG_ERR PLAT_LOG_LEVEL_ERROR
#define LOG_WARN PLAT_LOG_LEVEL_WARN
#define LOG_FATAL PLAT_LOG_LEVEL_FATAL


// =============================================================
SDF_status_t
name_service_create_meta(SDF_internal_ctxt_t *pai, const char *cname, SDF_cguid_t cguid,
			 SDF_container_meta_t *meta) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;

    plat_log_msg(21579, LOG_CAT, LOG_DBG, "%s - %lu", cname, cguid);

    if (ISEMPTY(cname)) {
	status = SDF_INVALID_PARAMETER;
    } else if ((status = cmc_create_meta(pai, theCMC, cname, cguid, meta)) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    /*
    ** We need to stuff away metadata in the shard so that the CMC can be recovered.
    ** Need to revisit this after Beta. It would be better to delete containers from Admin.
    */
    if (status == SDF_SUCCESS && 
	meta->properties.container_type.persistence == SDF_TRUE &&
	init_container_meta_blob_put != NULL) {

	SDF_container_meta_blob_t blob;

	// Stuff the metadata
	blob.version = SDF_BLOB_CONTAINER_META_VERSION;
	memcpy(&(blob.meta), meta, sizeof(*meta));

	status = init_container_meta_blob_put(meta->shard, (char *) &blob,
					 sizeof(SDF_container_meta_blob_t));
    }

    plat_log_msg(21610, LOG_CAT, log_level, "%s - %llu - %s", 
		 cname, (unsigned long long)cguid, SDF_Status_Strings[status]);


    return (status);
}

SDF_status_t
name_service_get_meta(SDF_internal_ctxt_t *pai, SDF_cguid_t cguid, SDF_container_meta_t *meta) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_DBG;

    plat_log_msg(21507, LOG_CAT, LOG_DBG, "%llu", (unsigned long long)cguid);

    if (meta == NULL) {
	status = SDF_INVALID_PARAMETER;
	log_level = LOG_ERR;
    } else {
	status = cmc_get_meta(pai, theCMC, cguid, meta);
    }

    plat_log_msg(21506, LOG_CAT, log_level, "%llu - %s", 
		 (unsigned long long)cguid, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_get_meta_from_cname(SDF_internal_ctxt_t *pai, const char *cname,
				 SDF_container_meta_t *meta) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_DBG;

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", cname);

    status = cmc_get_meta_from_cname(pai, theCMC, cname, meta);

    plat_log_msg(21511, LOG_CAT, log_level, "%s - %s",
		 cname, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_put_meta(SDF_internal_ctxt_t *pai, SDF_cguid_t cguid, SDF_container_meta_t *meta) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;

    plat_log_msg(21507, LOG_CAT, LOG_DBG, "%llu", (unsigned long long)cguid);

    if (meta == NULL) {
	status = SDF_INVALID_PARAMETER;
    } else if ((status = cmc_put_meta(pai, theCMC, cguid, meta))== SDF_SUCCESS) {
	log_level = LOG_DBG;
    }


    /*
    ** We need to stuff away metadata in the shard so that the CMC can be recovered.
    ** Need to revisit this after Beta. It would be better to delete containers from Admin.
    */
    if (status == SDF_SUCCESS && 
	meta->properties.container_type.persistence == SDF_TRUE &&
	init_container_meta_blob_put != NULL) {

	SDF_container_meta_blob_t blob;

	// Stuff the metadata
	blob.version = SDF_BLOB_CONTAINER_META_VERSION;
	memcpy(&(blob.meta), meta, sizeof(*meta));

	status = init_container_meta_blob_put(meta->shard, (char *) &blob,
					 sizeof(SDF_container_meta_blob_t));
    }

    plat_log_msg(21506, LOG_CAT, log_level, "%llu - %s",
		 (unsigned long long)cguid, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_lock_meta(SDF_internal_ctxt_t *pai, const char *cname) {

    SDF_status_t status = SDF_FAILURE;
    SDF_cguid_t cguid = SDF_NULL_CGUID;
    int log_level = LOG_ERR;

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", cname);

    if ((status = name_service_get_cguid(pai, cname, &cguid)) == SDF_SUCCESS &&
	(status = cmc_lock_meta(pai, theCMC, cguid)) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21506, LOG_CAT, log_level, "%llu - %s",
		 (unsigned long long)cguid, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_unlock_meta(SDF_internal_ctxt_t *pai, const char *cname) {

    SDF_status_t status = SDF_FAILURE;
    SDF_cguid_t cguid = SDF_NULL_CGUID;
    int log_level = LOG_ERR;

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", cname);

    if ((status = name_service_get_cguid(pai, cname, &cguid)) == SDF_SUCCESS &&
	(status = cmc_unlock_meta(pai, theCMC, cguid)) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21506, LOG_CAT, log_level, "%llu - %s",
		 (unsigned long long)cguid, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_lock_meta_by_cguid(SDF_internal_ctxt_t *pai, SDF_cguid_t cguid) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;

    if ((status = cmc_lock_meta(pai, theCMC, cguid)) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21506, LOG_CAT, log_level, "%llu - %s",
		 (unsigned long long)cguid, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_unlock_meta_by_cguid(SDF_internal_ctxt_t *pai, SDF_cguid_t cguid) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;

    if ((status = cmc_unlock_meta(pai, theCMC, cguid)) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21506, LOG_CAT, log_level, "%llu - %s",
		 (unsigned long long)cguid, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_meta_exists(SDF_internal_ctxt_t *pai, const char *cname) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_DBG;

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", cname);

    status = cmc_meta_exists(pai, theCMC, cname);

    plat_log_msg(21511, LOG_CAT, log_level, "%s - %s",
		 cname, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_remove_meta(SDF_internal_ctxt_t *pai, const char *cname) {

    SDF_status_t status = SDF_FAILURE;
    SDF_cguid_t cguid = SDF_NULL_CGUID;
    int log_level = LOG_ERR;

    plat_log_msg(21507, LOG_CAT, LOG_DBG, "%llu",
		 (unsigned long long)cguid);

    if ((status = name_service_get_cguid(pai, cname, &cguid)) == SDF_SUCCESS &&
	(status = cmc_remove_meta(pai, theCMC, cguid)) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21506, LOG_CAT, log_level, "%llu - %s",
		 (unsigned long long)cguid, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_get_props(SDF_internal_ctxt_t *pai, const char *cname, SDF_container_props_t *props) {

    SDF_status_t status = SDF_FAILURE;
    SDF_cguid_t cguid = SDF_NULL_CGUID;
    int log_level = LOG_DBG;

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", cname);

    if (ISEMPTY(cname) || props == NULL) {
	status = SDF_INVALID_PARAMETER;
	log_level = LOG_ERR;
    } else if ((status = name_service_get_cguid(pai, cname, &cguid)) == SDF_SUCCESS) {
	status = cmc_get_props(pai, theCMC, cguid, props);
    }

    plat_log_msg(21511, LOG_CAT, log_level, "%s - %s",
		 cname, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_put_props(SDF_internal_ctxt_t *pai, const char *cname, SDF_container_props_t props) {

    SDF_status_t status = SDF_FAILURE;
    SDF_cguid_t cguid = SDF_NULL_CGUID;
    int log_level = LOG_ERR;

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", cname);

    if (ISEMPTY(cname)) {
	status = SDF_INVALID_PARAMETER;
    } else if ((status = name_service_get_cguid(pai, cname, &cguid)) == SDF_SUCCESS &&
	       (status = cmc_put_props(pai, theCMC, cguid, props) == SDF_SUCCESS)) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21511, LOG_CAT, log_level, "%s - %s", 
		 cname, SDF_Status_Strings[status]);

    return (status);
}

/* Need to change this to use cguid + objkey to find shard ID */
SDF_shardid_t
name_service_get_shard(SDF_cguid_t cguid, const char *objkey, 
                       SDF_shardid_t first_shard, uint32_t shard_count) {

    SDF_shardid_t shard = SDF_SHARDID_INVALID;
    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_DBG;

    plat_log_msg(21506, LOG_CAT, LOG_DBG, "%llu - %s",
		 (unsigned long long)cguid, objkey);

    if (cguid == CMC_CGUID) {
	// Handle CMC
	shard = theCMC->meta.shard;
	status = SDF_SUCCESS;
    } else if ((shard = shard_compute_get_shard(cguid, objkey, first_shard, shard_count)) 
               >= SDF_SHARDID_LIMIT) {
	log_level = LOG_ERR;
    } else {
	status = SDF_SUCCESS;
    }

    plat_log_msg(21611, LOG_CAT, log_level, "cguid: %lu (0x%lx) - shard: %lu (0x%lx)" 
                 "\n objkey: %s  status: %s",
		 cguid, cguid, shard, shard, objkey, SDF_Status_Strings[status]);

    return (shard);
}

SDF_status_t
name_service_put_shard(SDF_internal_ctxt_t *pai, SDF_cguid_t cguid, SDF_shardid_t shard) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;

    plat_log_msg(21612, LOG_CAT, LOG_DBG, "%lu - %lu", 
		 cguid, shard);

    if ((status = cmc_put_shard(pai, theCMC, cguid, shard) == SDF_SUCCESS)) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21613, LOG_CAT, log_level, "%lu - %lu - %s",
		 cguid, shard, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_delete_shards(SDF_internal_ctxt_t *pai, const char *cname) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", cname);

    if ((status = cmc_delete_shards(pai, theCMC, cname) == SDF_SUCCESS)) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21511, LOG_CAT, log_level, "%s - %s",
		 cname, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_create_cguid_map(SDF_internal_ctxt_t *pai, const char *cname, SDF_cguid_t cguid) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;

    plat_log_msg(21614, LOG_CAT, LOG_DBG, "%s - %llu",
		 cname, (unsigned long long)cguid);

    if ((status = cmc_create_cguid_map(pai, theCMC, cguid, cname)) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21610, LOG_CAT, LOG_DBG, "%s - %llu - %s", 
		 cname, (unsigned long long)cguid, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_get_cguid(SDF_internal_ctxt_t *pai, const char *cname, SDF_cguid_t *cguid) {

    SDF_status_t status = SDF_FAILURE;
    SDF_cguid_map_t map;
    int log_level = LOG_DBG;

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", cname);

    *cguid = SDF_NULL_CGUID;

    if (ISEMPTY(cname)) {
	status = SDF_INVALID_PARAMETER;
	log_level = LOG_ERR;
    } else if (strcmp(cname, CMC_PATH) == 0) {
	*cguid = CMC_CGUID;
	status = SDF_SUCCESS;
    } else if ((status = cmc_get_cguid_map(pai, theCMC, cname, &map)) == SDF_SUCCESS) {
	*cguid = map.cguid;
    }

    plat_log_msg(21610, LOG_CAT, log_level, "%s - %llu - %s",
		 cname, (unsigned long long)*cguid, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_put_cguid_map(SDF_internal_ctxt_t *pai, const char *cname, SDF_cguid_t cguid) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;

    plat_log_msg(21614, LOG_CAT, LOG_DBG, "%s - %llu",
		 cname, (unsigned long long)cguid);

    if ((status = cmc_put_cguid_map(pai, theCMC, cguid, cname)) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21610, LOG_CAT, LOG_DBG, "%s - %llu - %s", 
		 cname, (unsigned long long)cguid, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_remove_cguid_map(SDF_internal_ctxt_t *pai, const char *cname) {

    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", cname);

    if ((status = cmc_remove_cguid_map(pai, theCMC, cname)) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21511, LOG_CAT, log_level, "%s - %s", 
		 cname, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_get_home_node(SDF_internal_ctxt_t *pai, SDF_cguid_t cguid, SDF_simple_key_t *pkey, uint32_t *node) {

    SDF_status_t status = SDF_FAILURE;
    SDF_container_meta_t meta;
    int log_level = LOG_DBG;

    plat_log_msg(21507, LOG_CAT, LOG_DBG, "%llu", (unsigned long long)cguid);

    if (node == NULL) {
	status = SDF_INVALID_PARAMETER;
	log_level = LOG_ERR;
    /* XXX: This needs to change to go with a replicated CMC */
    } else if (cguid == CMC_CGUID) {
	// Handle CMC 
	*node = CMC_HOME;
	status = SDF_SUCCESS;
    } else if ((status = name_service_get_meta(pai, cguid, &meta)) == SDF_SUCCESS) {
	*node = meta.node;
    }

    if (node != NULL) {
	plat_log_msg(21615, LOG_CAT, log_level, "%llu - %u - %s", 
		     (unsigned long long)cguid, *node, SDF_Status_Strings[status]);
    } else {
	plat_log_msg(21616, LOG_CAT, log_level, "%llu - NULL - %s", 
		     (unsigned long long)cguid, SDF_Status_Strings[status]);
    }

    return (status);
}

#define CGUID_COUNTER_TEMPLATE "cguid-counter-%u"

SDF_status_t
name_service_create_cguid_state(SDF_internal_ctxt_t *pai, SDF_vnode_t vnode, SDF_cguid_t cguid_counter) {
    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;
    char name[MAX_OBJECT_ID_SIZE];
    SDF_cguid_state_t state;
    SDF_container_meta_blob_t blob;

    snprintf(name, sizeof(name), CGUID_COUNTER_TEMPLATE, vnode);

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", name);

    state.type = SDF_META_TYPE_CGUID_STATE;
    state.cguid_counter = cguid_counter;

    if ((status = cmc_create_put_buffered_object(pai, 
						 theCMC->c, 
						 name, 
						 sizeof(SDF_cguid_state_t),
						 &state)) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    blob.version = SDF_BLOB_CONTAINER_META_VERSION;
    memcpy(&(blob.meta), &cguid_counter, sizeof(cguid_counter));

    if (status == SDF_SUCCESS && init_container_meta_blob_put != NULL &&
	(status = init_container_meta_blob_put(0, (char *) &blob,
					 sizeof(SDF_container_meta_blob_t))) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21511, LOG_CAT, log_level, "%s - %s", name, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_put_cguid_state(SDF_internal_ctxt_t *pai, SDF_vnode_t vnode, SDF_cguid_t cguid_counter) {
    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;
    char name[MAX_OBJECT_ID_SIZE];
    SDF_cguid_state_t state;
    SDF_container_meta_blob_t blob;

    snprintf(name, sizeof(name), CGUID_COUNTER_TEMPLATE, vnode);

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", name);

    state.type = SDF_META_TYPE_CGUID_STATE;
    state.cguid_counter = cguid_counter;

    status = cmc_put_buffered_object(pai, 
				     theCMC->c, 
				     name, 
				     sizeof(SDF_cguid_state_t), 
				     &state);

    blob.version = SDF_BLOB_CONTAINER_META_VERSION;
    memcpy(&(blob.meta), &cguid_counter, sizeof(cguid_counter));

    if (status == SDF_SUCCESS && init_container_meta_blob_put != NULL &&
	(status = init_container_meta_blob_put(0, (char *) &blob,
					 sizeof(SDF_container_meta_blob_t))) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21511, LOG_CAT, log_level, "%s - %s", name, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_get_cguid_state(SDF_internal_ctxt_t *pai, SDF_vnode_t vnode, SDF_cguid_t *cguid_counter) {
    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;
    char name[MAX_OBJECT_ID_SIZE];
    SDF_size_t len = 0;
    SDF_cguid_state_t state;

    snprintf(name, sizeof(name), CGUID_COUNTER_TEMPLATE, vnode);

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", name);

    if ((status = cmc_get_for_read_buffered_object(pai, 
						   theCMC->c, 
						   name, 
						   &state, 
						   sizeof(SDF_cguid_state_t),
						   &len) == SDF_SUCCESS)) {
	*cguid_counter = state.cguid_counter;
	log_level = LOG_DBG;
    } else {
	*cguid_counter = 0;
    }

    plat_log_msg(21511, LOG_CAT, log_level, "%s - %s", name, SDF_Status_Strings[status]);

    return (status);

}

SDF_status_t
name_service_remove_cguid_state(SDF_internal_ctxt_t *pai, SDF_vnode_t vnode, SDF_cguid_t cguid_counter) {
    SDF_status_t status = SDF_FAILURE;
    int log_level = LOG_ERR;
    char name[MAX_OBJECT_ID_SIZE];

    snprintf(name, sizeof(name), CGUID_COUNTER_TEMPLATE, vnode);

    plat_log_msg(20819, LOG_CAT, LOG_DBG, "%s", name);

    if ((status = cmc_remove_object(pai, theCMC->c, name)) == SDF_SUCCESS) {
	log_level = LOG_DBG;
    }

    plat_log_msg(21511, LOG_CAT, log_level, "%s - %s", name, SDF_Status_Strings[status]);

    return (status);
}

SDF_status_t
name_service_flush_inval_object_container(SDF_internal_ctxt_t *pai, const char *cname) {
    return (cmc_flush_inval_object_container(pai, cname));
}

SDF_status_t
name_service_inval_object_container(SDF_internal_ctxt_t *pai, const char *cname) {
    return (cmc_inval_object_container(pai, cname));
}

