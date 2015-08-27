/*
 * Copyright 2003-2015 Jeffrey K. Hollingsworth
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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>

#include "hclient.h"
#include "defaults.h"

#define MAX_LOOP 5000

/* A simple performance function is defined here for illustration purposes. */
double application(long ival, double rval, const char* string)
{
    int i;
    double sval = 0.0;
    for (i = 0; string[i]; ++i)
        sval += string[i];
    return sval * ival / rval;
}

int main(int argc, char* argv[])
{
    hdesc_t* hd;
    int i, retval;
    double perf;

    retval = 0;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "Usage: %s [KEY_1=VAL_1] ... [KEY_N=VAL_N]\n\n",
                    argv[0]);
            return 0;
        }
    }

    /* Initialize a Harmony client. */
    hd = ah_init();
    if (hd == NULL) {
        fprintf(stderr, "Error initializing a Harmony session");
        goto error;
    }
    ah_args(hd, &argc, argv);

    /* Load a session definition file. */
    if (ah_load(hd, "session.cfg") != 0) {
        fprintf(stderr, "Error loading session file");
        goto error;
    }

    /* Begin a new tuning session. */
    printf("Starting Harmony...\n");
    if (ah_launch(hd, NULL, 0, NULL) != 0) {
        fprintf(stderr, "Error launching tuning session");
        goto error;
    }

    /* Main tuning loop. */
    for (i = 0; !ah_converged(hd) && i < MAX_LOOP; ++i) {
        int hresult = ah_fetch(hd);
        if (hresult < 0) {
            fprintf(stderr, "Error fetching values from tuning session");
            goto error;
        }

        /* Run one full iteration of the application (or code variant).
         *
         * Here our application is rather simple. Definition of performance can
         * be user-defined. Depending on application, it can be MFlops/sec,
         * time to complete the entire run of the application, cache hits vs.
         * misses and so on.
         *
         * For searching the parameter space in a Transformation framework,
         * just run different parameterized code variants here. A simple
         * mapping between the parameters and the code-variants is needed to
         * call the appropriate code variant.
         */
        perf = application(ah_get_int(hd, "i_var"),
                           ah_get_real(hd, "r_var"),
                           ah_get_enum(hd, "fruits"));

        printf("(%4ld, %.4lf, \"%s\") = %lf\n",
               ah_get_int(hd, "i_var"),
               ah_get_real(hd, "r_var"),
               ah_get_enum(hd, "fruits"),
               perf);

        /* Report the performance we've just measured. */
        if (ah_report(hd, &perf) != 0) {
            fprintf(stderr, "Error reporting performance to server");
            goto error;
        }
    }

    if (!ah_converged(hd)) {
        printf("*\n");
        printf("* Leaving tuning session after %d iterations.\n", MAX_LOOP);
        printf("*\n");
    }

    if (ah_best(hd) < 0) {
        fprintf(stderr, "Error retrieving best tuning point");
        goto error;
    }
    perf = application(ah_get_int(hd, "i_var"),
                       ah_get_real(hd, "r_var"),
                       ah_get_enum(hd, "fruits"));

    printf("(%4ld, %.4lf, \"%s\") = %lf (* Best point found. *)\n",
           ah_get_int(hd, "i_var"),
           ah_get_real(hd, "r_var"),
           ah_get_enum(hd, "fruits"),
           perf);
    goto cleanup;

  error:
    fprintf(stderr, ": %s\n", ah_error_string(hd));
    retval = -1;

  cleanup:
    /* Leave the tuning session. */
    if (ah_leave(hd) != 0)
        fprintf(stderr, "Error disconnecting from Harmony session: %s.\n",
                ah_error_string(hd));

    ah_fini(hd);
    return retval;
}
