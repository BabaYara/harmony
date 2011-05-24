#
# Copyright 2003-2011 Jeffrey K. Hollingsworth
#
# This file is part of Active Harmony.
#
# Active Harmony is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published
# by the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Active Harmony is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with Active Harmony.  If not, see <http://www.gnu.org/licenses/>.
#
proc random_init { appName } {




    puts ":::: appname from init: $appName"




    global ${appName}_code_timestep
    set ${appName}_code_timestep 1
    global ${appName}_search_done
    set ${appName}_search_done 0
    global int_max_value
    set int_max_value 2147483647
    global ${appName}_best_perf_so_far
    global ${appName}_best_coordinate_so_far
    set ${appName}_best_perf_so_far $int_max_value
    set ${appName}_best_coordinate_so_far {}

    global ${appName}_max_search_iterations
    set ${appName}_max_search_iterations 100

    global ${appName}_code_generation_params
    set ${appName}_code_generation_params(generate_code) 0 
    set ${appName}_code_generation_params(gen_method) 2

    # method 1 parameters
    set ${appName}_code_generation_params(cserver_host) "spoon"
    set ${appName}_code_generation_params(cserver_port) 2002
    set ${appName}_code_generation_params(cserver_connection) 0

    # method 2 parameters
    set ${appName}_code_generation_params(code_generation_destination) "tiwari@spoon:/fs/spoon/tiwari/scratch/confs/"

    upvar #0 ${appName}_bundles bundles
    global ${appName}_simplex_time
    set ${appName}_simplex_time 1
    foreach bun $bundles {
        upvar #0 ${appName}_bundle_${bun}(value) bunv
        upvar #0 ${appName}_bundle_${bun}(minv) minv
        upvar #0 ${appName}_bundle_${bun}(maxv) maxv
        upvar #0 ${appName}_bundle_${bun}(domain) domain
        upvar #0 ${appName}_bundle_${bun} $bun
        set bunv [random_uniform $minv $maxv 1]
        puts $bunv
    }
}
