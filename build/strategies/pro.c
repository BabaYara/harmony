/*
 * Copyright 2003-2013 Jeffrey K. Hollingsworth
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

#include "strategy.h"
#include "session-core.h"
#include "hsession.h"
#include "hutil.h"
#include "hcfg.h"
#include "defaults.h"
#include "libvertex.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <math.h>

hpoint_t strategy_best;
double strategy_best_perf;

hpoint_t curr;

typedef enum simplex_init {
    SIMPLEX_INIT_UNKNOWN = 0,
    SIMPLEX_INIT_RANDOM,
    SIMPLEX_INIT_POINT,
    SIMPLEX_INIT_POINT_FAST,

    SIMPLEX_INIT_MAX
} simplex_init_t;

typedef enum simplex_state {
    SIMPLEX_STATE_UNKNONW = 0,
    SIMPLEX_STATE_INIT,
    SIMPLEX_STATE_REFLECT,
    SIMPLEX_STATE_EXPAND_ONE,
    SIMPLEX_STATE_EXPAND_ALL,
    SIMPLEX_STATE_SHRINK,
    SIMPLEX_STATE_CONVERGED,

    SIMPLEX_STATE_MAX
} simplex_state_t;

/* Forward function definitions. */
int  strategy_cfg(hmesg_t *mesg);
int  init_by_random(void);
int  init_by_point(int fast);
int  pro_algorithm(void);
int  pro_next_state(const simplex_t *input, int best_in);
int  pro_next_simplex(simplex_t *output);
void check_convergence(void);

/* Variables to control search properties. */
simplex_init_t init_method  = SIMPLEX_INIT_POINT;
vertex_t *     init_point;
double         init_percent = 0.35;

double reflect_coefficient  = 1.0;
double expand_coefficient   = 2.0;
double contract_coefficient = 0.5;
double shrink_coefficient   = 0.5;
double converge_fv_tol      = 1e-4;
double converge_sz_tol;
int simplex_size;

/* Variables to track current search state. */
simplex_state_t state;
simplex_t *base;
simplex_t *test;

int best_base;
int best_test;
int best_stash;
int next_id;
int send_idx;
int reported;

/*
 * Invoked once on strategy load.
 */
int strategy_init(hmesg_t *mesg)
{
    if (libvertex_init(sess) != 0) {
        mesg->data.string = "Could not initialize vertex library.";
        return -1;
    }

    init_point = vertex_alloc();
    if (!init_point) {
        mesg->data.string = "Could not allocate memory for initial point.";
        return -1;
    }

    if (strategy_cfg(mesg) != 0)
        return -1;

    strategy_best = HPOINT_INITIALIZER;
    strategy_best_perf = INFINITY;

    if (hpoint_init(&curr, sess->sig.range_len) < 0)
        return -1;

    test = simplex_alloc(simplex_size);
    if (!test) {
        mesg->data.string = "Could not allocate memory for candidate simplex.";
        return -1;
    }

    base = simplex_alloc(simplex_size);
    if (!base) {
        mesg->data.string = "Could not allocate memory for reference simplex.";
        return -1;
    }

    /* Default stopping criteria: 0.5% of dist(vertex_min, vertex_max). */
    if (converge_sz_tol == 0.0) {
        vertex_min(base->vertex[0]);
        vertex_max(base->vertex[1]);
        converge_sz_tol = vertex_dist(base->vertex[0],
                                      base->vertex[1]) * 0.005;
    }

    switch (init_method) {
    case SIMPLEX_INIT_RANDOM:     init_by_random(); break;
    case SIMPLEX_INIT_POINT:      init_by_point(0); break;
    case SIMPLEX_INIT_POINT_FAST: init_by_point(1); break;
    default:
        mesg->data.string = "Invalid initial search method.";
        return -1;
    }

    next_id = 1;
    state = SIMPLEX_STATE_INIT;

    if (hcfg_set(sess->cfg, CFGKEY_STRATEGY_CONVERGED, "0") < 0) {
        mesg->data.string =
            "Could not set " CFGKEY_STRATEGY_CONVERGED " config variable.";
        return -1;
    }

    /* PRO algorithm requires an atomic prefetch queue. */
    hcfg_set(sess->cfg, CFGKEY_PREFETCH_ATOMIC, "1");

    if (pro_next_simplex(test) != 0) {
        mesg->data.string = "Could not initiate the simplex.";
        return -1;
    }

    return 0;
}

int strategy_cfg(hmesg_t *mesg)
{
    const char *cfgval;
    char *endp;
    int retval;

    /* Make sure the simplex size is N+1 or greater. */
    cfgval = hcfg_get(sess->cfg, CFGKEY_PRO_SIMPLEX_SIZE);
    if (cfgval)
        simplex_size = atoi(cfgval);

    if (simplex_size < sess->sig.range_len + 1)
        simplex_size = sess->sig.range_len + 1;

    /* Make sure the prefetch count is either 0 or 1. */
    cfgval = hcfg_get(sess->cfg, CFGKEY_PREFETCH_COUNT);
    if (cfgval) {
        retval = atoi(cfgval);
        if (retval > 1 || strcasecmp(cfgval, "auto") == 0) {
            hcfg_set(sess->cfg, CFGKEY_PREFETCH_COUNT, "1");
        }
        else if (retval < 0) {
            hcfg_set(sess->cfg, CFGKEY_PREFETCH_COUNT, "0");
        }
    }

    cfgval = hcfg_get(sess->cfg, CFGKEY_RANDOM_SEED);
    if (cfgval) {
        srand(atoi(cfgval));
    }
    else {
        srand(time(NULL));
    }

    cfgval = hcfg_get(sess->cfg, CFGKEY_PRO_INIT_METHOD);
    if (cfgval) {
        if (strcasecmp(cfgval, "random") == 0) {
            init_method = SIMPLEX_INIT_RANDOM;
        }
        else if (strcasecmp(cfgval, "point") == 0) {
            init_method = SIMPLEX_INIT_POINT;
        }
        else if (strcasecmp(cfgval, "point_fast") == 0) {
            init_method = SIMPLEX_INIT_POINT_FAST;
        }
        else {
            mesg->data.string = "Invalid value for "
                CFGKEY_PRO_INIT_METHOD " configuration key.";
            return -1;
        }
    }

    cfgval = hcfg_get(sess->cfg, CFGKEY_PRO_INIT_PERCENT);
    if (cfgval) {
        init_percent = strtod(cfgval, &endp);
        if (*endp != '\0') {
            mesg->data.string = "Invalid value for " CFGKEY_PRO_INIT_PERCENT
                " configuration key.";
            return -1;
        }
        if (init_percent <= 0 || init_percent > 1) {
            mesg->data.string = "Configuration key " CFGKEY_PRO_INIT_PERCENT
                " must be between 0.0 and 1.0 (exclusive).";
            return -1;
        }
    }

    cfgval = hcfg_get(sess->cfg, CFGKEY_PRO_REFLECT);
    if (cfgval) {
        reflect_coefficient = strtod(cfgval, &endp);
        if (*endp != '\0') {
            mesg->data.string = "Invalid value for " CFGKEY_PRO_REFLECT
                " configuration key.";
            return -1;
        }
        if (reflect_coefficient <= 0) {
            mesg->data.string = "Configuration key " CFGKEY_PRO_REFLECT
                " must be positive.";
            return -1;
        }
    }

    cfgval = hcfg_get(sess->cfg, CFGKEY_PRO_EXPAND);
    if (cfgval) {
        expand_coefficient = strtod(cfgval, &endp);
        if (*endp != '\0') {
            mesg->data.string = "Invalid value for " CFGKEY_PRO_EXPAND
                " configuration key.";
            return -1;
        }
        if (reflect_coefficient <= 1) {
            mesg->data.string = "Configuration key " CFGKEY_PRO_EXPAND
                " must be greater than 1.0.";
            return -1;
        }
    }

    cfgval = hcfg_get(sess->cfg, CFGKEY_PRO_CONTRACT);
    if (cfgval) {
        contract_coefficient = strtod(cfgval, &endp);
        if (*endp != '\0') {
            mesg->data.string = "Invalid value for " CFGKEY_PRO_CONTRACT
                " configuration key.";
            return -1;
        }
        if (reflect_coefficient <= 0 || reflect_coefficient >= 1) {
            mesg->data.string = "Configuration key " CFGKEY_PRO_CONTRACT
                " must be between 0.0 and 1.0 (exclusive).";
            return -1;
        }
    }

    cfgval = hcfg_get(sess->cfg, CFGKEY_PRO_SHRINK);
    if (cfgval) {
        shrink_coefficient = strtod(cfgval, &endp);
        if (*endp != '\0') {
            mesg->data.string = "Invalid value for " CFGKEY_PRO_SHRINK
                " configuration key.";
            return -1;
        }
        if (reflect_coefficient <= 0 || reflect_coefficient >= 1) {
            mesg->data.string = "Configuration key " CFGKEY_PRO_SHRINK
                " must be between 0.0 and 1.0 (exclusive).";
            return -1;
        }
    }

    cfgval = hcfg_get(sess->cfg, CFGKEY_PRO_CONVERGE_FV);
    if (cfgval) {
        converge_fv_tol = strtod(cfgval, &endp);
        if (*endp != '\0') {
            mesg->data.string = "Invalid value for " CFGKEY_PRO_CONVERGE_FV
                " configuration key.";
            return -1;
        }
    }

    cfgval = hcfg_get(sess->cfg, CFGKEY_PRO_CONVERGE_SZ);
    if (cfgval) {
        converge_sz_tol = strtod(cfgval, &endp);
        if (*endp != '\0') {
            mesg->data.string = "Invalid value for " CFGKEY_PRO_CONVERGE_SZ
                " configuration key.";
            return -1;
        }
    }
    return 0;
}

int init_by_random(void)
{
    int i;

    for (i = 0; i < simplex_size; ++i)
        vertex_rand(base->vertex[i]);

    return 0;
}

int init_by_point(int fast)
{
    if (init_point->id == -1)
        vertex_center(init_point);

    if (fast)
        return simplex_from_vertex_fast(init_point, init_percent, base);

    return simplex_from_vertex(init_point, init_percent, base);
}

/*
 * Generate a new candidate configuration point in the space provided
 * by the hpoint_t parameter.
 */
int strategy_fetch(hmesg_t *mesg)
{
    if (send_idx == simplex_size) {
        mesg->status = HMESG_STATUS_BUSY;
        return 0;
    }

    test->vertex[send_idx]->id = next_id;
    if (vertex_to_hpoint(test->vertex[send_idx], &mesg->data.fetch.cand) < 0) {
        mesg->status = HMESG_STATUS_FAIL;
        return -1;
    }
    ++next_id;
    ++send_idx;

    /* Send best point information, if needed. */
    if (mesg->data.fetch.best.id < strategy_best.id) {
        mesg->data.fetch.best = HPOINT_INITIALIZER;
        if (hpoint_copy(&mesg->data.fetch.best, &strategy_best) < 0) {
            mesg->status = HMESG_STATUS_FAIL;
            return -1;
        }
    }
    else {
        mesg->data.fetch.best = HPOINT_INITIALIZER;
    }

    mesg->status = HMESG_STATUS_OK;
    return 0;
}

/*
 * Inform the search strategy of an observed performance associated with
 * a configuration point.
 */
int strategy_report(hmesg_t *mesg)
{
    int i;

    for (i = 0; i < simplex_size; ++i) {
        if (mesg->data.report.cand.id == test->vertex[i]->id)
            break;
    }

    if (i == simplex_size) {
        /* Ignore rouge vertex reports. */
        mesg->status = HMESG_STATUS_OK;
        return 0;
    }

    ++reported;
    test->vertex[i]->perf = mesg->data.report.perf;
    if (test->vertex[i]->perf < test->vertex[best_test]->perf)
        best_test = i;

    if (reported == simplex_size) {
        if (pro_algorithm() != 0) {
            mesg->status = HMESG_STATUS_FAIL;
            mesg->data.string = "Internal error: PRO algorithm failure.";
            return -1;
        }
        reported = 0;
        send_idx = 0;
    }

    /* Update the best performing point, if necessary. */
    if (strategy_best_perf > mesg->data.report.perf) {
        strategy_best_perf = mesg->data.report.perf;
        if (hpoint_copy(&strategy_best, &mesg->data.report.cand) < 0) {
            mesg->status = HMESG_STATUS_FAIL;
            mesg->data.string = strerror(errno);
            return -1;
        }
    }

    mesg->status = HMESG_STATUS_OK;
    return 0;
}

int pro_algorithm(void)
{
    do {
        if (state == SIMPLEX_STATE_CONVERGED)
            break;

        if (pro_next_state(test, best_test) != 0)
            return -1;

        if (state == SIMPLEX_STATE_REFLECT)
            check_convergence();

        if (pro_next_simplex(test) != 0)
            return -1;

    } while (simplex_outofbounds(test));

    return 0;
}

int pro_next_state(const simplex_t *input, int best_in)
{
    switch (state) {
    case SIMPLEX_STATE_INIT:
    case SIMPLEX_STATE_SHRINK:
        /* Simply accept the candidate simplex and prepare to reflect. */
        simplex_copy(base, input);
        best_base = best_in;
        state = SIMPLEX_STATE_REFLECT;
        break;

    case SIMPLEX_STATE_REFLECT:
        if (input->vertex[best_in]->perf < base->vertex[best_base]->perf) {
            /* Reflected simplex has best known performance.
             * Accept the reflected simplex, and prepare a trial expansion.
             */
            simplex_copy(base, input);
            best_stash = best_test;
            state = SIMPLEX_STATE_EXPAND_ONE;
        }
        else {
            /* Reflected simplex does not improve performance.
             * Shrink the simplex instead.
             */
            state = SIMPLEX_STATE_SHRINK;
        }
        break;

    case SIMPLEX_STATE_EXPAND_ONE:
        if (input->vertex[0]->perf < base->vertex[best_base]->perf) {
            /* Trial expansion has found the best known vertex thus far.
             * We are now free to expand the entire reflected simplex.
             */
            state = SIMPLEX_STATE_EXPAND_ALL;
        }
        else {
            /* Expanded vertex does not improve performance.
             * Revert to the (unexpanded) reflected simplex.
             */
            best_base = best_in;
            state = SIMPLEX_STATE_REFLECT;
        }
        break;

    case SIMPLEX_STATE_EXPAND_ALL:
        if (input->vertex[best_in]->perf < base->vertex[best_base]->perf) {
            /* Expanded simplex has found the best known vertex thus far.
             * Accept the expanded simplex as the reference simplex. */
            simplex_copy(base, input);
            best_base = best_in;
        }

        /* Expanded simplex may not improve performance over the
         * reference simplex.  In general, this can only happen if the
         * entire expanded simplex is out of bounds.
         *
         * Either way, reflection should be tested next. */
        state = SIMPLEX_STATE_REFLECT;
        break;

    default:
        return -1;
    }
    return 0;
}

int pro_next_simplex(simplex_t *output)
{
    int i;

    switch (state) {
    case SIMPLEX_STATE_INIT:
        /* Bootstrap the process by testing the reference simplex. */
        simplex_copy(output, base);
        break;

    case SIMPLEX_STATE_REFLECT:
        /* Reflect all original simplex vertices around the best known
         * vertex thus far. */
        simplex_transform(base, base->vertex[best_base],
                          -reflect_coefficient, output);
        break;

    case SIMPLEX_STATE_EXPAND_ONE:
        /* Next simplex should have one vertex extending the best.
         * And the rest should be copies of the best known vertex.
         */
        vertex_transform(test->vertex[best_test], base->vertex[best_base],
                         expand_coefficient, output->vertex[0]);

        for (i = 1; i < simplex_size; ++i)
            vertex_copy(output->vertex[i], base->vertex[best_base]);
        break;

    case SIMPLEX_STATE_EXPAND_ALL:
        /* Expand all original simplex vertices away from the best
         * known vertex thus far. */
        simplex_transform(base, base->vertex[best_base],
                          expand_coefficient, output);
        break;

    case SIMPLEX_STATE_SHRINK:
        /* Shrink all original simplex vertices towards the best
         * known vertex thus far. */
        simplex_transform(base, base->vertex[best_base],
                          shrink_coefficient, output);
        break;

    case SIMPLEX_STATE_CONVERGED:
        /* Simplex has converged.  Nothing to do.
         * In the future, we may consider new search at this point. */
        break;

    default:
        return -1;
    }
    return 0;
}

void check_convergence(void)
{
    int i;
    double fv_err, sz_max, dist;
    static vertex_t *centroid;

    if (!centroid)
        centroid = vertex_alloc();

    if (simplex_collapsed(base) == 1)
        goto converged;

    simplex_centroid(base, centroid);

    fv_err = 0;
    for (i = 0; i < simplex_size; ++i) {
        fv_err += ((base->vertex[i]->perf - centroid->perf) *
                   (base->vertex[i]->perf - centroid->perf));
    }
    fv_err /= simplex_size;

    sz_max = 0;
    for (i = 0; i < simplex_size; ++i) {
        dist = vertex_dist(base->vertex[i], centroid);
        if (sz_max < dist)
            sz_max = dist;
    }

    if (fv_err < converge_fv_tol && sz_max < converge_sz_tol)
        goto converged;

    return;

  converged:
    state = SIMPLEX_STATE_CONVERGED;
    hcfg_set(sess->cfg, CFGKEY_STRATEGY_CONVERGED, "1");
}
