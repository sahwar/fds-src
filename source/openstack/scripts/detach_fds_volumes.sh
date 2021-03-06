#!/bin/bash
BIN_PATH=~/bin
source $BIN_PATH/fds_params.sh

for i in `seq 1 $VM_COUNT`; do
    server_id=`nova list --name ${VM_NAME}-${i} | grep ${VM_NAME}-${i} | awk '{print $2}'`
    vol_id=`cinder list --display-name ${FDS_VOL_NAME}-$i | grep ${FDS_VOL_NAME}-$i | awk '{print $2}'`
    echo nova volume-detach $server_id $vol_id
    nova volume-detach $server_id $vol_id
done
