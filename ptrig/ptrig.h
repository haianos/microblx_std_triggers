/*
 * ptrig microblx function block (A pthread based trigger block)
 */

#ifdef CONFIG_PTHREAD_SETNAME
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <limits.h>     /* PTHREAD_STACK_MIN */

#include <ubx.h>

/* includes types and type metadata */
#include "../types/ptrig_config.h"
#include "../types/ptrig_config.h.hexarr"
#include "../types/ptrig_period.h"
#include "../types/ptrig_period.h.hexarr"
#include "../types/ptrig_tstat.h"
#include "../types/ptrig_tstat.h.hexarr"


/* block meta information */
char ptrig_meta[] =
        "{ doc='pthread based trigger',"
        "  real-time=false,"
        "}";

/* declaration of block configuration */
ubx_config_t ptrig_config[] = {
        { .name="period", .type_name = "struct ptrig_period" },
        { .name="stacksize", .type_name = "size_t" },
        { .name="sched_priority", .type_name = "int" },
        { .name="sched_policy", .type_name = "char", .value = { .len=12 } },
        { .name="thread_name", .type_name = "char", .value = { .len=12 } },
        { .name="trig_blocks", .type_name = "struct ptrig_config" },
        { .name="time_stats_enabled", .type_name = "int" },
        { NULL },
};

/* declaration port block ports */
ubx_port_t ptrig_ports[] = {
        { .name="tstats", .out_type_name="struct ptrig_tstat", .out_data_len=1, .doc=""  },
        { NULL },
};

// /* declare a struct port_cache */
// struct ptrig_port_cache {
//         ubx_port_t* tstats;
// };

/* instance state */
struct ptrig_info {
        pthread_t tid;
        pthread_attr_t attr;
        uint32_t state;
        pthread_mutex_t mutex;
        pthread_cond_t active_cond;
        struct ptrig_config *trig_list;
        unsigned int trig_list_len;

        struct ptrig_period period;
        struct ptrig_tstat tstats;

        ubx_port_t *p_tstats;
};

def_write_fun(write_tstats, struct ptrig_tstat)

/* block operation forward declarations */
int ptrig_init(ubx_block_t *b);
int ptrig_start(ubx_block_t *b);
void ptrig_stop(ubx_block_t *b);
void ptrig_cleanup(ubx_block_t *b);


/* put everything together */
ubx_block_t ptrig_block = {
        .name = "std_triggers/ptrig",
        .type = BLOCK_TYPE_COMPUTATION,
        .meta_data = ptrig_meta,
        .configs = ptrig_config,
        .ports = ptrig_ports,

        .init = ptrig_init,
        .start = ptrig_start,
        .stop = ptrig_stop,
        .cleanup = ptrig_cleanup
};


/* ptrig module init and cleanup functions */
int ptrig_mod_init(ubx_node_info_t* ni)
{
        DBG(" ");
        int ret = -1;

        if(ubx_block_register(ni, &ptrig_block) != 0)
                goto out;

        ret=0;
out:
        return ret;
}

void ptrig_mod_cleanup(ubx_node_info_t *ni)
{
        DBG(" ");
        ubx_block_unregister(ni, "std_triggers/ptrig");
}

