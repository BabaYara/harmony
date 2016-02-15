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

#include "strategy.h"
#include "session-core.h"
#include "hcfg.h"
#include "hspace.h"
#include "hpoint.h"
#include "hperf.h"
#include "hutil.h"
#include "libvertex.h"

#include <string.h> // For strcmp().
#include <math.h>   // For log(), isnan(), NAN, and HUGE_VAL.

/*
 * Configuration variables used in this plugin.
 * These will automatically be registered by session-core upon load.
 */
const hcfg_info_t plugin_keyinfo[] = {
    { CFGKEY_INIT_POINT, NULL,
      "Centroid point used to initialize the search simplex.  If this key "
      "is left undefined, the simplex will be initialized in the center of "
      "the search space." },
    { CFGKEY_INIT_RADIUS, "0.50",
      "Size of the initial simplex, specified as a fraction of the total "
      "search space radius." },
    { CFGKEY_REJECT_METHOD, "penalty",
      "How to choose a replacement when dealing with rejected points. "
      "    penalty: Use this method if the chance of point rejection is "
      "relatively low. It applies an infinite penalty factor for invalid "
      "points, allowing the strategy to select a sensible next point.  "
      "However, if the entire simplex is comprised of invalid points, an "
      "infinite loop of rejected points may occur.\n"
      "    random: Use this method if the chance of point rejection is "
      "high.  It reduces the risk of infinitely selecting invalid points "
      "at the cost of increasing the risk of deforming the simplex." },
    { CFGKEY_REFLECT, "1.0",
      "Multiplicative coefficient for simplex reflection step." },
    { CFGKEY_EXPAND, "2.0",
      "Multiplicative coefficient for simplex expansion step." },
    { CFGKEY_CONTRACT, "0.5",
      "Multiplicative coefficient for simplex contraction step." },
    { CFGKEY_SHRINK, "0.5",
      "Multiplicative coefficient for simplex shrink step." },
    { CFGKEY_FVAL_TOL, "0.0001",
      "Convergence test succeeds if difference between all vertex "
      "performance values fall below this value." },
    { CFGKEY_SIZE_TOL, "0.005",
      "Convergence test succeeds if the simplex radius becomes smaller "
      "than this percentage of the total search space.  Simplex radius "
      "is measured from centroid to furthest vertex.  Total search space "
      "is measured from minimum to maximum point." },
    { CFGKEY_DIST_TOL, NULL,
      "Convergence test succeeds if the simplex moves (via reflection) "
      "a distance less than or equal to this percentage of the total "
      "search space for TOL_CNT consecutive steps.  Total search space "
      "is measured from minimum to maximum point.  This method overrides "
      "the default size/fval method." },
    { CFGKEY_TOL_CNT, "3",
      "The number of consecutive reflection steps which travel at or "
      "below DIST_TOL before the search is considered converged." },
    { CFGKEY_ANGEL_LOOSE, "False",
      "When all leeways cannot be satisfied simultaneously, attempt to "
      "satisfy as many leeways as possible, not necessarily favoring "
      "objectives with higher priority.  If false, ANGEL will satisfy "
      "as many higher priority objectives as possible before allowing "
      "violations in lower priority objectives." },
    { CFGKEY_ANGEL_MULT, "1.0",
      "Multiplicative factor for penalty function." },
    { CFGKEY_ANGEL_ANCHOR, "True",
      "Transfer the best known solution across search phases." },
    { CFGKEY_ANGEL_SAMESIMPLEX, "True",
      "Use the same initial simplex to begin each search phase.  This "
      "reduces the total number of evaluations when combined with the "
      "caching layer." },
    { CFGKEY_ANGEL_LEEWAY, NULL,
      "Comma (or whitespace) separated list of N-1 leeway values, "
      "where N is the number of objectives.  Each value may range "
      "from 0.0 to 1.0 (inclusive), and specifies how much the search "
      "may stray from its objective's minimum value." },
    { NULL }
};

typedef struct span {
    double min;
    double max;
} span_t;

typedef enum reject_method {
    REJECT_METHOD_UNKNOWN = 0,
    REJECT_METHOD_PENALTY,
    REJECT_METHOD_RANDOM,

    REJECT_METHOD_MAX
} reject_method_t;

typedef enum simplex_state {
    SIMPLEX_STATE_UNKNONW = 0,
    SIMPLEX_STATE_INIT,
    SIMPLEX_STATE_REFLECT,
    SIMPLEX_STATE_EXPAND,
    SIMPLEX_STATE_CONTRACT,
    SIMPLEX_STATE_SHRINK,
    SIMPLEX_STATE_CONVERGED,

    SIMPLEX_STATE_MAX
} simplex_state_t;

/*
 * Structure to hold data for an individual PRO search instance.
 */
typedef struct data {
    hspace_t* space;
    hpoint_t  best;
    hperf_t   best_perf;

    // Search options.
    vertex_t        init_point;
    double          init_radius;
    reject_method_t reject_type;

    double reflect_val;
    double expand_val;
    double contract_val;
    double shrink_val;
    double fval_tol;
    double size_tol;
    double dist_tol;
    double move_len;
    double space_size;
    int    tol_cnt;

    double* leeway;
    double  mult;
    int     anchor;
    int     loose;
    int     samesimplex;

    // Search state.
    simplex_state_t state;
    vertex_t        centroid;
    vertex_t        reflect;
    vertex_t        expand;
    vertex_t        contract;
    simplex_t       init_simplex;
    simplex_t       simplex;

    vertex_t* next;
    int       index_best;
    int       index_worst;
    int       index_curr; // For INIT or SHRINK.
    int       next_id;

    int     phase;
    int     perf_n;
    double* thresh;
    span_t* span;
} data_t;

static data_t* data;

/*
 * Internal helper function prototypes.
 */
static int  allocate_structures(void);
static int  config_strategy(void);
static void check_convergence(void);
static int  increment_phase(void);
static int  nm_algorithm(void);
static int  nm_next_vertex(void);
static int  nm_state_transition(void);
static int  make_initial_simplex(void);
static int  update_centroid(void);

/*
 * Allocate memory for a new search instance.
 */
void* strategy_alloc(void)
{
    data_t* retval = calloc(1, sizeof(*retval));
    if (!retval)
        return NULL;

    retval->next_id =  1;
    retval->phase   = -1;

    return retval;
}

/*
 * Initialize (or re-initialize) data for this search instance.
 */
int strategy_init(hspace_t* space)
{
    data->space = space;

    if (config_strategy() != 0)
        return -1;

    if (make_initial_simplex() != 0) {
        session_error("Could not initialize initial simplex.");
        return -1;
    }

    if (session_setcfg(CFGKEY_CONVERGED, "0") != 0) {
        session_error("Could not set " CFGKEY_CONVERGED " config variable.");
        return -1;
    }

    data->next_id = 1;
    if (increment_phase() != 0)
        return -1;

    if (nm_next_vertex() != 0) {
        session_error("Could not initiate test vertex.");
        return -1;
    }

    return 0;
}

/*
 * Generate a new candidate configuration point.
 */
int strategy_generate(hflow_t* flow, hpoint_t* point)
{
    if (data->next->id == data->next_id) {
        flow->status = HFLOW_WAIT;
        return 0;
    }

    data->next->id = data->next_id;
    if (vertex_point(data->next, data->space, point) != 0) {
        session_error("Could not make point from vertex during generate");
        return -1;
    }

    flow->status = HFLOW_ACCEPT;
    return 0;
}

/*
 * Regenerate a point deemed invalid by a later plug-in.
 */
int strategy_rejected(hflow_t* flow, hpoint_t* point)
{
    hpoint_t* hint = &flow->point;

    if (hint && hint->id) {
        // Update our state to include the hint point.
        hint->id = point->id;
        if (vertex_set(data->next, data->space, hint) != 0) {
            session_error("Could not copy hint into simplex during reject");
            return -1;
        }

        if (hpoint_copy(point, hint) != 0) {
            session_error("Could not return hint during reject");
            return -1;
        }
    }
    else if (data->reject_type == REJECT_METHOD_PENALTY) {
        // Apply an infinite penalty to the rejected point.
        hperf_reset(&data->next->perf);

        // Allow the algorithm to choose the next point.
        if (nm_algorithm() != 0) {
            session_error("Nelder-Mead algorithm failure");
            return -1;
        }

        data->next->id = data->next_id;
        if (vertex_point(data->next, data->space, point) != 0) {
            session_error("Could not copy next point during reject");
            return -1;
        }
    }
    else if (data->reject_type == REJECT_METHOD_RANDOM) {
        // Replace the rejected point with a random point.
        if (vertex_random(data->next, data->space, 1.0) != 0) {
            session_error("Could not randomize point during reject");
            return -1;
        }

        data->next->id = data->next_id;
        if (vertex_point(data->next, data->space, point) != 0) {
            session_error("Could not copy random point during reject");
            return -1;
        }
    }

    flow->status = HFLOW_ACCEPT;
    return 0;
}

/*
 * Analyze the observed performance for this configuration point.
 */
int strategy_analyze(htrial_t* trial)
{
    if (trial->point.id != data->next->id) {
        session_error("Rouge points not supported.");
        return -1;
    }
    hperf_copy(&data->next->perf, &trial->perf);

    // Update the observed value ranges.
    for (int i = 0; i < data->perf_n; ++i) {
        if (data->span[i].min > data->next->perf.obj[i])
            data->span[i].min = data->next->perf.obj[i];

        if (data->span[i].max < data->next->perf.obj[i] &&
            data->next->perf.obj[i] < HUGE_VAL)
            data->span[i].max = data->next->perf.obj[i];
    }

    double penalty = 0.0;
    double penalty_base = 1.0;
    for (int i = data->phase - 1; i >= 0; --i) {
        if (data->next->perf.obj[i] > data->thresh[i]) {
            if (!data->loose) {
                penalty += penalty_base;
            }

            double fraction = ((data->next->perf.obj[i] - data->thresh[i]) /
                               (data->span[i].max       - data->thresh[i]));
            penalty += 1.0 / (1.0 - log(fraction));
        }
        penalty_base *= 2;
    }

    if (penalty > 0.0) {
        if (data->loose) {
            penalty += 1.0;
        }

        double span = (data->span[data->phase].max -
                       data->span[data->phase].min);
        data->next->perf.obj[data->phase] += penalty * span * data->mult;
    }

    // Update the best performing point, if necessary.
    if (!data->best_perf.len ||
        data->best_perf.obj[data->phase] > data->next->perf.obj[data->phase])
    {
        if (hperf_copy(&data->best_perf, &data->next->perf) != 0) {
            session_error("Could not store best performance");
            return -1;
        }

        if (hpoint_copy(&data->best, &trial->point) != 0) {
            session_error("Could not copy best point during analyze");
            return -1;
        }
    }

    if (nm_algorithm() != 0) {
        session_error("Nelder-Mead algorithm failure");
        return -1;
    }

    if (data->state != SIMPLEX_STATE_CONVERGED)
        ++data->next_id;

    return 0;
}

/*
 * Return the best performing point thus far in the search.
 */
int strategy_best(hpoint_t* point)
{
    if (hpoint_copy(point, &data->best) != 0) {
        session_error("Could not copy best point during strategy_best()");
        return -1;
    }
    return 0;
}

/*
 * Internal helper function implementation.
 */
int allocate_structures(void)
{
    void* newbuf;

    if (simplex_init(&data->init_simplex, data->space->len) != 0) {
        session_error("Could not allocate initial simplex");
        return -1;
    }

    for (int i = 0; i < data->init_simplex.len; ++i) {
        if (hperf_init(&data->init_simplex.vertex[i].perf,
                       data->perf_n) != 0)
        {
            session_error("Could not allocate initial simplex performance");
            return -1;
        }
    }

    if (simplex_init(&data->simplex, data->space->len) != 0) {
        session_error("Could not allocate base simplex");
        return -1;
    }

    for (int i = 0; i < data->simplex.len; ++i) {
        if (hperf_init(&data->simplex.vertex[i].perf, data->perf_n) != 0) {
            session_error("Could not allocate initial simplex performance");
            return -1;
        }
    }

    if (hpoint_init(&data->best, data->space->len) != 0 ||
        hperf_init(&data->best_perf, data->perf_n) != 0)
    {
        session_error("Could not allocate best point");
        return -1;
    }

    if (vertex_init(&data->reflect, data->space->len) != 0 ||
        hperf_init(&data->reflect.perf, data->perf_n) != 0)
    {
        session_error("Could not allocate reflection vertex");
        return -1;
    }

    if (vertex_init(&data->expand, data->space->len) != 0 ||
        hperf_init(&data->expand.perf, data->perf_n) != 0)
    {
        session_error("Could not allocate expansion vertex");
        return -1;
    }

    if (vertex_init(&data->contract, data->space->len) != 0 ||
        hperf_init(&data->contract.perf, data->perf_n) != 0)
    {
        session_error("Could not allocate contraction vertex");
        return -1;
    }

    newbuf = realloc(data->leeway, (data->perf_n - 1) * sizeof(*data->leeway));
    if (!newbuf) {
        session_error("Could not allocate leeway vector");
        return -1;
    }
    data->leeway = newbuf;

    newbuf = realloc(data->span, data->perf_n * sizeof(*data->span));
    if (!newbuf) {
        session_error("Could not allocate span container");
        return -1;
    }
    data->span = newbuf;

    newbuf = realloc(data->thresh, (data->perf_n - 1) * sizeof(*data->thresh));
    if (!newbuf) {
        session_error("Could not allocate threshold container");
        return -1;
    }
    data->thresh = newbuf;

    return 0;
}

int config_strategy(void)
{
    const char* cfgstr;
    double cfgval;

    data->loose = hcfg_bool(session_cfg, CFGKEY_ANGEL_LOOSE);
    data->anchor = hcfg_bool(session_cfg, CFGKEY_ANGEL_ANCHOR);
    data->samesimplex = hcfg_bool(session_cfg, CFGKEY_ANGEL_SAMESIMPLEX);

    cfgval = hcfg_real(session_cfg, CFGKEY_ANGEL_MULT);
    if (isnan(cfgval)) {
        session_error("Invalid value for " CFGKEY_ANGEL_MULT
                      " configuration key.");
        return -1;
    }
    data->mult = cfgval;

    cfgval = hcfg_real(session_cfg, CFGKEY_INIT_RADIUS);
    if (cfgval <= 0 || cfgval > 1) {
        session_error("Configuration key " CFGKEY_INIT_RADIUS
                      " must be between 0.0 and 1.0 (exclusive).");
        return -1;
    }
    data->init_radius = cfgval;

    cfgstr = hcfg_get(session_cfg, CFGKEY_REJECT_METHOD);
    if (cfgstr) {
        if (strcmp(cfgstr, "penalty") == 0) {
            data->reject_type = REJECT_METHOD_PENALTY;
        }
        else if (strcmp(cfgstr, "random") == 0) {
            data->reject_type = REJECT_METHOD_RANDOM;
        }
        else {
            session_error("Invalid value for "
                          CFGKEY_REJECT_METHOD " configuration key.");
            return -1;
        }
    }

    cfgval = hcfg_real(session_cfg, CFGKEY_REFLECT);
    if (isnan(cfgval) || cfgval <= 0.0) {
        session_error("Configuration key " CFGKEY_REFLECT
                      " must be positive.");
        return -1;
    }
    data->reflect_val = hcfg_real(session_cfg, CFGKEY_REFLECT);

    cfgval = hcfg_real(session_cfg, CFGKEY_EXPAND);
    if (isnan(cfgval) || cfgval <= data->reflect_val) {
        session_error("Configuration key " CFGKEY_EXPAND
                      " must be greater than the reflect coefficient.");
        return -1;
    }
    data->expand_val = cfgval;

    cfgval = hcfg_real(session_cfg, CFGKEY_CONTRACT);
    if (isnan(cfgval) || cfgval <= 0.0 || cfgval >= 1.0) {
        session_error("Configuration key " CFGKEY_CONTRACT
                      " must be between 0.0 and 1.0 (exclusive).");
        return -1;
    }
    data->contract_val = cfgval;

    cfgval = hcfg_real(session_cfg, CFGKEY_SHRINK);
    if (isnan(cfgval) || cfgval <= 0.0 || cfgval >= 1.0) {
        session_error("Configuration key " CFGKEY_SHRINK
                      " must be between 0.0 and 1.0 (exclusive).");
        return -1;
    }
    data->shrink_val = cfgval;

    data->perf_n = hcfg_int(session_cfg, CFGKEY_PERF_COUNT);
    if (data->perf_n < 1) {
        session_error("Invalid value for " CFGKEY_PERF_COUNT
                      " configuration key.");
        return -1;
    }

    if (allocate_structures() != 0)
        return -1;

    // Use the expand and reflect vertex variables as temporaries to
    // calculate the size tolerance.
    if (vertex_minimum(&data->expand, data->space) != 0 ||
        vertex_maximum(&data->reflect, data->space) != 0)
        return -1;
    data->space_size = vertex_norm(&data->expand, &data->reflect,
                                   VERTEX_NORM_L2);

    cfgval = hcfg_real(session_cfg, CFGKEY_DIST_TOL);
    if (!isnan(cfgval)) {
        if (cfgval <= 0.0 || cfgval >= 1.0) {
            session_error("Configuration key " CFGKEY_DIST_TOL
                          " must be between 0.0 and 1.0 (exclusive).");
            return -1;
        }
        data->dist_tol = cfgval * data->space_size;

        data->tol_cnt = hcfg_int(session_cfg, CFGKEY_TOL_CNT);
        if (data->tol_cnt < 1) {
            session_error("Configuration key " CFGKEY_TOL_CNT
                          " must be greater than zero");
            return -1;
        }
    }
    else {
        // CFGKEY_DIST_TOL is not defined.  Use the size/fval method.
        data->fval_tol = hcfg_real(session_cfg, CFGKEY_FVAL_TOL);
        if (isnan(data->fval_tol)) {
            session_error("Invalid value for " CFGKEY_FVAL_TOL
                          " configuration key.");
            return -1;
        }

        cfgval = hcfg_real(session_cfg, CFGKEY_SIZE_TOL);
        if (isnan(cfgval) || cfgval <= 0.0 || cfgval >= 1.0) {
            session_error("Configuration key " CFGKEY_SIZE_TOL
                          " must be between 0.0 and 1.0 (exclusive).");
            return -1;
        }
        data->size_tol = cfgval * data->space_size;
    }

    if (hcfg_get(session_cfg, CFGKEY_ANGEL_LEEWAY)) {
        if (hcfg_arr_len(session_cfg, CFGKEY_ANGEL_LEEWAY) !=
            data->perf_n - 1)
        {
            session_error("Incorrect number of leeway values provided.");
            return -1;
        }

        for (int i = 0; i < data->perf_n - 1; ++i) {
            data->leeway[i] = hcfg_arr_real(session_cfg,
                                            CFGKEY_ANGEL_LEEWAY, i);
            if (isnan(data->leeway[i])) {
                session_error("Invalid value for " CFGKEY_ANGEL_LEEWAY
                              " configuration key.");
                return -1;
            }
        }
    }
    else {
        session_error(CFGKEY_ANGEL_LEEWAY " must be defined.");
        return -1;
    }

    for (int i = 0; i < data->perf_n; ++i) {
        data->span[i].min =  HUGE_VAL;
        data->span[i].max = -HUGE_VAL;
    }

    return 0;
}

void check_convergence(void)
{
    static int flat_cnt;

    // Converge if all simplex objective values remain the same after 3 moves.
    int flat = 1;
    double perf_0 = data->simplex.vertex[0].perf.obj[data->phase];
    for (unsigned i = 1; i < data->simplex.len; ++i) {
        double perf_i = data->simplex.vertex[i].perf.obj[data->phase];

        if (perf_i < perf_0 || perf_0 < perf_i) {
            flat = 0;
            break;
        }
    }
    if (flat) {
        if (++flat_cnt >= 3) {
            flat_cnt = 0;
            goto converged;
        }
    }
    else flat_cnt = 0;

    // Converge if all simplex verticies map to the same underlying hpoint.
    if (simplex_collapsed(&data->simplex, data->space))
        goto converged;

    // Converge if the simplex moves via reflection below a distance tolerance.
    if (!isnan(data->dist_tol)) {
        static int cnt;

        if (data->move_len < data->dist_tol) {
            if (++cnt >= data->tol_cnt) {
                cnt = 0;
                goto converged;
            }
        }
        else cnt = 0;
    }
    // If a dist_tol is not set, converge based on simplex size and flatness.
    else {
        double fval_err = 0;
        double base_val = data->centroid.perf.obj[data->phase];
        for (int i = 0; i < data->simplex.len; ++i) {
            double fval = data->simplex.vertex[i].perf.obj[data->phase];
            fval_err += (fval - base_val) * (fval - base_val);
        }
        fval_err /= data->simplex.len;

        double size_max = 0;
        for (int i = 0; i < data->simplex.len; ++i) {
            double dist = vertex_norm(&data->simplex.vertex[i],
                                      &data->centroid, VERTEX_NORM_L2);
            if (size_max < dist)
                size_max = dist;
        }

        if (fval_err < data->fval_tol && size_max < data->size_tol)
            goto converged;
    }
    return;

  converged:
    if (data->phase == data->perf_n - 1) {
        data->state = SIMPLEX_STATE_CONVERGED;
        session_setcfg(CFGKEY_CONVERGED, "1");
    }
    else {
        increment_phase();
    }
}

int increment_phase(void)
{
    if (data->phase >= 0) {
        // Calculate the threshold for the previous phase.
        double tval;
        tval  = data->span[data->phase].max - data->span[data->phase].min;
        tval *= data->leeway[data->phase];
        tval += data->span[data->phase].min;

        data->thresh[data->phase] = tval;
    }
    ++data->phase;

    char intbuf[16];
    snprintf(intbuf, sizeof(intbuf), "%d", data->phase);
    session_setcfg(CFGKEY_ANGEL_PHASE, intbuf);

    // Use the centroid to store the previous phase's best vertex.
    if (vertex_copy(&data->centroid,
                    &data->simplex.vertex[data->index_best]) != 0)
    {
        session_error("Could not copy best vertex during phase increment");
        return -1;
    }

    if (!data->samesimplex) {
        // Re-initialize the initial simplex, if needed.
        if (make_initial_simplex() != 0) {
            session_error("Could not reinitialize the initial simplex.");
            return -1;
        }
    }
    simplex_copy(&data->simplex, &data->init_simplex);

    if (data->best.id > 0) {
        if (data->anchor) {
            double min_dist = HUGE_VAL;
            int idx = -1;
            for (int i = 0; i < data->simplex.len; ++i) {
                double curr_dist = vertex_norm(&data->centroid,
                                               &data->simplex.vertex[i],
                                               VERTEX_NORM_L2);
                if (min_dist > curr_dist) {
                    min_dist = curr_dist;
                    idx = i;
                }
            }

            if (vertex_copy(&data->simplex.vertex[idx],
                            &data->centroid) != 0)
            {
                session_error("Could not anchor simplex to best point");
                return -1;
            }
        }
    }
    hperf_reset(&data->best_perf);
    data->best.id = 0;

    data->state = SIMPLEX_STATE_INIT;
    return 0;
}

int nm_algorithm(void)
{
    do {
        if (data->state == SIMPLEX_STATE_CONVERGED)
            break;

        if (nm_state_transition() != 0)
            return -1;

        if (data->state == SIMPLEX_STATE_REFLECT) {
            if (update_centroid() != 0)
                return -1;

            check_convergence();
        }

        if (nm_next_vertex() != 0)
            return -1;

    } while (!vertex_inbounds(data->next, data->space));

    return 0;
}

int nm_state_transition(void)
{
    switch (data->state) {
    case SIMPLEX_STATE_INIT:
    case SIMPLEX_STATE_SHRINK:
        // Simplex vertex performance value.
        if (++data->index_curr == data->space->len + 1) {
            update_centroid();
            data->state = SIMPLEX_STATE_REFLECT;
            data->index_curr = 0;
        }
        break;

    case SIMPLEX_STATE_REFLECT:
        if (data->reflect.perf.obj[data->phase] <
            data->simplex.vertex[data->index_best].perf.obj[data->phase])
        {
            // Reflected point performs better than all simplex points.
            // Attempt expansion.
            //
            data->state = SIMPLEX_STATE_EXPAND;
        }
        else if (data->reflect.perf.obj[data->phase] <
                 data->simplex.vertex[data->index_worst].perf.obj[data->phase])
        {
            // Reflected point performs better than worst simplex point.
            // Replace the worst simplex point with reflected point
            // and attempt reflection again.
            //
            if (vertex_copy(&data->simplex.vertex[data->index_worst],
                            &data->reflect) != 0)
                return -1;

            update_centroid();
        }
        else {
            // Reflected point performs worse than all simplex points.
            // Attempt contraction.
            //
            data->state = SIMPLEX_STATE_CONTRACT;
        }
        break;

    case SIMPLEX_STATE_EXPAND:
        if (data->expand.perf.obj[data->phase] <
            data->simplex.vertex[data->index_best].perf.obj[data->phase])
        {
            // Expanded point performs even better than reflected point.
            // Replace the worst simplex point with the expanded point
            // and attempt reflection again.
            //
            if (vertex_copy(&data->simplex.vertex[data->index_worst],
                            &data->expand) != 0)
                return -1;
        }
        else {
            // Expanded point did not result in improved performance.
            // Replace the worst simplex point with the original
            // reflected point and attempt reflection again.
            //
            if (vertex_copy(&data->simplex.vertex[data->index_worst],
                            &data->reflect) != 0)
                return -1;
        }
        update_centroid();
        data->state = SIMPLEX_STATE_REFLECT;
        break;

    case SIMPLEX_STATE_CONTRACT:
        if (data->contract.perf.obj[data->phase] <
            data->simplex.vertex[data->index_worst].perf.obj[data->phase])
        {
            // Contracted point performs better than the worst simplex point.
            // Replace the worst simplex point with contracted point
            // and attempt reflection.
            //
            if (vertex_copy(&data->simplex.vertex[data->index_worst],
                            &data->contract) != 0)
                return -1;

            update_centroid();
            data->state = SIMPLEX_STATE_REFLECT;
        }
        else {
            // Contracted test vertex has worst known performance.
            // Shrink the entire simplex towards the best point.
            //
            data->index_curr = -1; // Indicates the beginning of SHRINK.
            data->state = SIMPLEX_STATE_SHRINK;
        }
        break;

    default:
        return -1;
    }
    return 0;
}

int nm_next_vertex(void)
{
    switch (data->state) {
    case SIMPLEX_STATE_INIT:
        // Test individual vertices of the initial simplex.
        data->next = &data->simplex.vertex[data->index_curr];
        break;

    case SIMPLEX_STATE_REFLECT:
        // Test a vertex reflected from the worst performing vertex
        // through the centroid point.
        //
        if (vertex_transform(&data->centroid,
                             &data->simplex.vertex[data->index_worst],
                             data->reflect_val, &data->reflect) != 0)
            return -1;

        data->move_len  = vertex_norm(&data->simplex.vertex[data->index_worst],
                                      &data->reflect, VERTEX_NORM_L2);
        data->move_len /= data->space_size;

        data->next = &data->reflect;
        break;

    case SIMPLEX_STATE_EXPAND:
        // Test a vertex that expands the reflected vertex even
        // further from the the centroid point.
        //
        if (vertex_transform(&data->centroid,
                             &data->simplex.vertex[data->index_worst],
                             data->expand_val, &data->expand) != 0)
            return -1;

        data->next = &data->expand;
        break;

    case SIMPLEX_STATE_CONTRACT:
        // Test a vertex contracted from the worst performing vertex
        // towards the centroid point.
        //
        if (vertex_transform(&data->simplex.vertex[data->index_worst],
                             &data->centroid, -data->contract_val,
                             &data->contract) != 0)
            return -1;

        data->next = &data->contract;
        break;

    case SIMPLEX_STATE_SHRINK:
        if (data->index_curr == -1) {
            // Shrink the entire simplex towards the best known vertex
            // thus far.
            //
            if (simplex_transform(&data->simplex,
                                  &data->simplex.vertex[data->index_best],
                                  -data->shrink_val, &data->simplex) != 0)
                return -1;

            data->index_curr = 0;
        }

        // Test individual vertices of the initial simplex.
        data->next = &data->simplex.vertex[data->index_curr];
        break;

    case SIMPLEX_STATE_CONVERGED:
        // Simplex has converged.  Nothing to do.
        // In the future, we may consider new search at this point.
        //
        data->next = &data->simplex.vertex[data->index_best];
        break;

    default:
        return -1;
    }

    hperf_reset(&data->next->perf);
    return 0;
}

int make_initial_simplex(void)
{
    const char* cfgval = hcfg_get(session_cfg, CFGKEY_INIT_POINT);
    if (cfgval) {
        if (vertex_parse(&data->init_point, data->space, cfgval) != 0) {
            session_error("Could not convert initial point to vertex");
            return -1;
        }
    }
    else {
        if (vertex_center(&data->init_point, data->space) != 0) {
            session_error("Could not create central vertex");
            return -1;
        }
    }

    if (simplex_set(&data->init_simplex, data->space,
                    &data->init_point, data->init_radius) != 0)
    {
        session_error("Could not generate initial simplex");
        return -1;
    }

    return 0;
}

int update_centroid(void)
{
    data->index_best = 0;
    data->index_worst = 0;

    for (int i = 1; i < data->simplex.len; ++i) {
        if (data->simplex.vertex[i].perf.obj[data->phase] <
            data->simplex.vertex[data->index_best].perf.obj[data->phase])
            data->index_best = i;

        if (data->simplex.vertex[i].perf.obj[data->phase] >
            data->simplex.vertex[data->index_worst].perf.obj[data->phase])
            data->index_worst = i;
    }

    unsigned stashed_id = data->simplex.vertex[data->index_worst].id;
    data->simplex.vertex[data->index_worst].id = 0;
    if (simplex_centroid(&data->simplex, &data->centroid) != 0)
        return -1;

    data->simplex.vertex[data->index_worst].id = stashed_id;
    return 0;
}
