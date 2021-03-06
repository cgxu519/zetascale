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

#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <getopt.h>
#include <zs.h>

static int btree_opt = 0;    
static int fixbtree_opt = 0;    
static char *logfile = NULL;

static struct ZS_state* zs_state;
static __thread struct ZS_thread_state *_zs_thd_state;

static struct option long_options[] = { 
    {"btree",      no_argument,       0, 'b'}, 
    {"fixbtree",   no_argument,       0, 'f'}, 
    {"help",       no_argument,       0, 'h'}, 
    {"logfile",    required_argument, 0, 'l'}, 
    {0, 0, 0, 0} 
};

void print_help(char *pname) 
{
    fprintf(stderr, "\nExecute validation of ZetaScale persistent metadata, btree structures and recovery logs.\n\n");
    fprintf(stderr, "%s --btree --fixbtree --logfile=file --help\n", pname);
    fprintf(stderr, "     --btree           run btree validation without fixup\n");
    fprintf(stderr, "     --fixbtree        run btree validation with fixup\n"); 
    fprintf(stderr, "     --logfile         logfile path\n"); 
    fprintf(stderr, "     --help            show help\n");

}

int get_options(int argc, char *argv[])
{
    int option_index = 0;
    int c;

    while (1) { 
        c = getopt_long (argc, argv, "bfl:h", long_options, &option_index); 

        if (c == -1) 
            break;
     
        switch (c) { 
     
        case 'b': 
            btree_opt = 1; 
            break;
     
        case 'f': 
            fixbtree_opt = 1; 
            break;
     
        case 'l': 
            logfile = optarg; 
            break;
     
        case 'h': 
            print_help(argv[0]); 
            return -1;

        default:
            print_help(argv[0]); 
            return -1;
        }
    }

    return 0;
}

int init_zs() {
    ZS_status_t status = ZS_FAILURE;

    if ( ZS_SUCCESS != ( status = ZSInit( &zs_state ) ) ) {
        fprintf(stderr, "Failed to initialize ZS API!\n");
        return status;
    }

    if ( ZS_SUCCESS != ( status = ZSInitPerThreadState(zs_state, &_zs_thd_state ) ) ) {
        fprintf(stderr, "Failed to initialize ZS API!\n");
        return status;
    }

    return status;
}

int close_zs()
{
    ZS_status_t status = ZS_FAILURE;

    ZSCheckClose();

    if ( ZS_SUCCESS != ( status = ZSReleasePerThreadState(&_zs_thd_state ) ) ) {
        fprintf(stderr, "Failed to release ZS thread state!\n");
        return status;
    }

    if ( ZS_SUCCESS != (status = ZSShutdown( zs_state ) ) ) {
        fprintf(stderr, "Failed to shutdown ZS API!\n");
        return status;
    }

    return status;
}

ZS_status_t check_meta()
{
    return ZSCheckMeta();
}

ZS_status_t check_btree()
{
    int flags = 0;

    if ( fixbtree_opt )
        flags = ZS_CHECK_FIX_BOJ_CNT;

    return ZSCheck( _zs_thd_state , flags );
}

void set_props()
{
    ZSLoadProperties(getenv("ZS_PROPERTY_FILE"));
    if (btree_opt || fixbtree_opt)
        ZSSetProperty("ZS_CHECK_MODE", "2");
    else
        ZSSetProperty("ZS_CHECK_MODE", "1");
    ZSSetProperty("ZS_REFORMAT", "0");
    unsetenv("ZS_PROPERTY_FILE");
}

int main(int argc, char *argv[])
{
    ZS_status_t status = ZS_FAILURE;

    if ( get_options( argc, argv) ) {
        return -1;
    }

    set_props();

    ZSCheckInit(logfile);

    // always check superblock and shard metadata
    if (ZS_SUCCESS != check_meta()) {
        fprintf(stderr, "meta check failed, unable to continue\n");
        return -1;
    } else {
        fprintf(stderr, "meta check succeeded\n");
    }

    if (btree_opt || fixbtree_opt) {
        if (init_zs() < 0) {
            fprintf(stderr, "ZS init failed\n");
            return -1;
        }

        if (ZS_SUCCESS != (status = check_btree())) {
            fprintf(stderr, "btree check failed: %s\n", ZSStrError(status));
        } else {
            fprintf(stderr, "btree check succeeded\n");
        }

        if (close_zs() < 0) {
            fprintf(stderr, "ZS close failed\n");
            return -1;
        }
    }

    return 0;
}
