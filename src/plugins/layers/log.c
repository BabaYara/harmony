/*
 * Copyright 2003-2016 Jeffrey K. Hollingsworth
 *
 * This file is part of Active Harmony.
 *
 * Active Harmony is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Active Harmony is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Active Harmony.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \page logger Point Logger (log.so)
 *
 * This processing layer writes a log of point/performance pairs to disk as
 * they flow through the auto-tuning [feedback loop](\ref intro_feedback).
 */

#include "session-core.h"
#include "hspace.h"
#include "hpoint.h"
#include "hperf.h"
#include "hcfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>

/*
 * Name used to identify this plugin layer.
 * All Harmony plugin layers must define this variable.
 */
const char harmony_layer_name[] = "logger";

/*
 * Configuration variables used in this plugin.
 * These will automatically be registered by session-core upon load.
 */
hcfg_info_t plugin_keyinfo[] = {
    { CFGKEY_LOG_FILE, NULL,
      "Name of point/performance log file." },
    { CFGKEY_LOG_MODE, "a",
      "Mode to use with 'fopen()'.  Valid values are a for append, "
      "and w for overwrite." },
    { NULL }
};

/*
 * Structure to hold all data needed by an individual search instance.
 *
 * To support multiple parallel search sessions, no global variables
 * should be defined or used in this plug-in layer.
 */
typedef struct data {
    FILE* fd;
} data_t;

static data_t* data;

/*
 * Allocate memory for a new search instance.
 */
void* logger_alloc(void)
{
    data_t* retval = calloc(1, sizeof(*retval));
    if (!retval)
        return NULL;

    return retval;
}

/*
 * Initialize (or re-initialize) data for this search instance.
 */
int logger_init(hspace_t* space)
{
    const char* filename = hcfg_get(session_cfg, CFGKEY_LOG_FILE);
    const char* mode     = hcfg_get(session_cfg, CFGKEY_LOG_MODE);
    time_t tm;

    if (!filename) {
        session_error(CFGKEY_LOG_FILE " config key empty.");
        return -1;
    }

    data->fd = fopen(filename, mode);
    if (!data->fd) {
        session_error( strerror(errno) );
        return -1;
    }

    tm = time(NULL);
    fprintf(data->fd, "* Begin tuning session log.\n");
    fprintf(data->fd, "* Timestamp: %s", asctime( localtime(&tm) ));

    return 0;
}

int logger_join(const char* id)
{
    fprintf(data->fd, "Client \"%s\" joined the tuning session.\n", id);
    return 0;
}

int logger_analyze(hflow_t* flow, htrial_t* trial)
{
    fprintf(data->fd, "Point #%d: (", trial->point.id);
    for (int i = 0; i < trial->point.len; ++i) {
        if (i > 0) fprintf(data->fd, ",");

        const hval_t* v = &trial->point.term[i];
        switch (v->type) {
        case HVAL_INT:  fprintf(data->fd, "%ld", v->value.i); break;
        case HVAL_REAL: fprintf(data->fd, "%lf[%la]",
                                v->value.r, v->value.r); break;
        case HVAL_STR:  fprintf(data->fd, "\"%s\"", v->value.s); break;
        default:
            session_error("Invalid point value type");
            return -1;
        }
    }
    fprintf(data->fd, ") ");

    if (trial->perf.len > 1) {
        fprintf(data->fd, "=> (");
        for (int i = 0; i < trial->perf.len; ++i) {
            if (i > 0) fprintf(data->fd, ",");
            fprintf(data->fd, "%lf[%la]",
                    trial->perf.obj[i], trial->perf.obj[i]);
        }
        fprintf(data->fd, ") ");
    }
    fprintf(data->fd, "=> %lf\n", hperf_unify(&trial->perf));
    fflush(data->fd);

    flow->status = HFLOW_ACCEPT;
    return 0;
}

int logger_fini(void)
{
    fprintf(data->fd, "*\n");
    fprintf(data->fd, "* End tuning session.\n");
    fprintf(data->fd, "*\n");

    if (fclose(data->fd) != 0) {
        session_error( strerror(errno) );
        return -1;
    }

    free(data);
    data = NULL;
    return 0;
}
