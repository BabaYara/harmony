#!/bin/bash
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

_host_name_=$HOSTNAME

# read in the properties file


# command line arguments
# first argument to this script file is the code-generation parameters.
#  code generation parameters are passed in as bash array. Each array
#  entry corresponds to a transformation parameter. Lets consider an
#  example. Lets say we are generating code for Matrix Multiplication.
#  In the harmony tcl file (which describes the set of tunable parameters
#  and their ranges), lets assume we have described the parameters in the
#  following order -- TI, TJ, TK, UI, UJ (we are tiling the three loops
#  and unrolling the I and J loops). The first argument can look like:
#  (100 12 120 2 2), where TI=100, TJ=12, TK=120, UI=2 and UJ=2.
# We pass this array to generate_temp_script.sh, which parses the array
#  and creates the appropriate temp.script.

code_parameters=$1

# The second argument to this script is the working directory for this
#  instance of the generator. Recall that we can have multiple generators and
#  each generator has its own directory, which is set up by setup_generator_hosts.sh
#  script file.
WORK_DIR=$HOME/scratch/$2

# The third argument is the code_generator_host (which is the name of the machine
#  where the code-generator driver is running).
code_generator_host=$3

# cd into the work_directory
cd $WORK_DIR
source code.properties
echo "appname is $appname"

# some house-cleaning stuff. getting rid of old files.
rm -rf ${file_prefix}.so *.so ${file_prefix}_modified.${file_suffix} temp.script ${file_prefix}.lxf

# generate a new CHiLL script using the transformation parameters
source generate_temp_script.sh "$code_parameters"
echo "generate_temp_script.sh $code_parameters"

# Run chill and the post-processor
#  The modified file is saved as ${file_prefix}_modified.${file_suffix}
# Here I am using a particular version of CHiLL. If you want to use a particular version
#  rather than the default, then:

if [ $use_default_chill -eq 1 ]; then
    # we have exported the path to the chill, so we know where it is.
    chill temp.script
else
    ${chill_exec_name} temp.script
fi

s2f ${file_prefix}.lxf > ${file_prefix}_modified.${file_suffix}

# very basic error checking
if [ ! -e "${file_prefix}_modified.${file_suffix}" ]; then
    echo "ERROR: Generating the variant for ${appname}."
    echo "ERROR: ${__out_file_prefix}" >> error_confs.${appname}.dat
    # for the lack of better approach here, we simply copy the
    #  original version. We do not want to stall the application.
    cp ${file_prefix}_default.${file_suffix} ${file_prefix}_modified.${file_suffix};
fi

if [ ! -s "${file_prefix}_modified.${file_suffix}" ]; then
    echo "ERROR: Generating the variant for ${appname}."
    echo "ERROR: ${__out_file_prefix}" >> error_confs.${appname}.dat
    # for the lack of better approach here, we simply copy the
    #  original version. We do not want to stall the application.
    cp ${file_prefix}_default.${file_suffix} ${file_prefix}_modified.${file_suffix};
fi

# Compilation.
if [ $file_suffix == *f* ]; then
    $FC_COMMAND -c -fpic ${file_prefix}_modified.${file_suffix}
fi

if [ $file_suffix == *c* ]; then
    $CC_COMMAND -c -fpic ${file_prefix}_modified.${file_suffix}
fi

# output filename
out_file=${__out_file_prefix}${output_file_suffix}
if [ $output_file_suffix == *.so* ]; then
    $CC_COMMAND -shared -lc -o $out_file ${file_prefix}_modified.o
fi
if [ $output_file_suffix == *.exe* ]; then
    # assumption is we know what the driver file is.
    $CC_COMMAND -o $out_file ${driver_filename} ${file_prefix}_modified.o
fi

# finally move this file to appropriate location. Remember that if you are
#  using remote hosts for code generation, this has to be scp rather than 
#  mv
if [ $use_remote_hosts -eq 0 ]; then
    # keep the file around for future reference or error checking
    mv $out_file ../new_code_${appname}/
    cp ../new_code_${appname}/$out_file ../new_code/
fi

if [ $use_remote_hosts -eq 0 ]; then
    # keep the file around for future reference or error checking
    mv $out_file ../new_code_${appname}/
    # only scp if this is a remote host
    if [ $_host_name_ == code_generator_host ]; then
	cp ../new_code_${appname}/$out_file ../new_code/
    else
	# we have to scp the code to the code_generator_host
	scp ../new_code_${appname}/$out_file ${username}@${code_generator_host}:~/scratch/new_code/
    fi
fi
