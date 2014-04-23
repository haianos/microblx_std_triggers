/* generated type file source library for std_triggers */

#include <stdint.h>
#include <ubx.h>

#include "ptrig_config.h"
#include "ptrig_config.h.hexarr"
#include "ptrig_period.h"
#include "ptrig_period.h.hexarr"
#include "ptrig_tstat.h"
#include "ptrig_tstat.h.hexarr"

/* declare types */
ubx_type_t types[] = {
        def_struct_type(struct ptrig_config, &ptrig_config_h),
        def_struct_type(struct ptrig_period, &ptrig_period_h),
        def_struct_type(struct ptrig_tstat, &ptrig_tstat_h),
        { NULL },
};

static int decltypes_init(ubx_node_info_t* ni)
{
        DBG(" ");
        ubx_type_t *tptr;
        for(tptr=types; tptr->name!=NULL; tptr++) {
                /* TODO check for errors */
                ubx_type_register(ni, tptr);
        }

        return 0;
}

static void decltypes_cleanup(ubx_node_info_t *ni)
{
        DBG(" ");
        const ubx_type_t *tptr;

        for(tptr=types; tptr->name!=NULL; tptr++)
                ubx_type_unregister(ni, tptr->name);
}

UBX_MODULE_INIT(decltypes_init)
UBX_MODULE_CLEANUP(decltypes_cleanup)
UBX_MODULE_LICENSE_SPDX(BSD-3-Clause)
