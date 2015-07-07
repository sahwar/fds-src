#!/bin/bash -l

###################################################################################
###################################################################################
###################################################################################
###################################################################################
# This script is deprecated by jenkins_tests_copy_for_build_test_coroner_cleanup.sh
###################################################################################
###################################################################################
###################################################################################
###################################################################################

function message
{
   echo "================================================================================"
   echo "$*"
   echo "================================================================================"
}

message "CONFIGURE ulimit AND COREFILE SETTINGS"

ulimit -c unlimited
mkdir -p /corefiles
sysctl -w "kernel.core_pattern=/corefiles/%e-%p.core"
ulimit -n 400000

message "CURRENT ulimit SETTINGS"

ulimit -a


function performance_report
{
   unit=$1
   seconds=$2

   if [[ ${seconds} -lt 60 ]]
   then
      message "${unit} TIME = ${seconds} seconds"
   else
      message "${unit} TIME = ${seconds} seconds or $(( ${seconds} / 60 )) minute(s) $(( ${seconds} % 60 )) seconds"
   fi
}

function python_unit_tests
{
   message "RUNNING PYTHON UNIT TESTS"

   # Run Unit Test
   cd ${root_dir}/jenkins_scripts
   start_time=$(date +%s)
   ./run-python-unit-tests.sh
   [[ $? -ne 0 ]] && echo "PYTHON UNIT TESTS:  FAILED" && exit 98
   end_time=$(date +%s)
   performance_report PYTHON_UNIT_TESTS $(( ${end_time} - ${start_time} ))
   cd -
}

function unit_tests
{
   message "RUNNING UNIT TESTS"

   # Run Unit Test
   cd ${root_dir}/jenkins_scripts
   start_time=$(date +%s)
   ./run-unit-tests.py
   [[ $? -ne 0 ]] && echo "UNIT TEST:  FAILED" && exit 98
   end_time=$(date +%s)
   performance_report UNIT_TEST $(( ${end_time} - ${start_time} ))
   cd -
}

function system_tests
{
   message "RUNNING SYSTEM TEST"

   # Run Unit Test
   cd ${root_dir}/source/test/testsuites
   start_time=$(date +%s)
   ./ScenarioDriverSuite.py -q ./BuildSmokeTest_onpr.ini -d dummy --verbose
   [[ $? -ne 0 ]] && echo "SYSTEM TEST:  FAILED" && exit 97
   end_time=$(date +%s)
   performance_report SYSTEM_TEST $(( ${end_time} - ${start_time} ))
   cd -
   
   # Run QoS test.
   #cd "${root_dir}/source/Build/linux-x86_64.debug/bin"
   #./iodriver
   #[[ $? -ne 0 ]] && echo "SYSTEM TEST:  FAILED" && exit 99
   #cd -
}

root_dir=$(pwd)

python_unit_tests
unit_tests
system_tests

exit 0
