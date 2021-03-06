#!/bin/bash -l

. ./jenkins_scripts/message.sh

# return 0 if core or hprof file found, 1 otherwise
function core_hunter
{
    message  "POKING around for core files"
    # TODO, remove /corefiles sometime
    for d in /corefiles /fds ../Build/linux-x86_64.debug/bin/; do
        for c in core hprof; do 
        
            find ${d} -type f -name "*.${c}" |grep -e ".*" > /dev/null
            return_code=$?
            
            if [ ${return_code} -eq 0 ]; then
                return ${return_code}
            fi
        done    

        find ${d} -type f -name "*hs_err_pid*.log" |grep -e ".*" > /dev/null
        return_code=$?
        
        if [ ${return_code} -eq 0 ]; then
            return ${return_code}
        fi

    done        

    return 1
}

