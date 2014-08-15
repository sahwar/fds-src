#!/bin/bash
base=$1
for dir in `ls $base` 
do
    name=`echo $dir| awk -F "[.:]" '{print "type, ",$6, ", nvols,",$2,", threads,",$4," , fsize,",$12}'`
    ./get_stats.py -d $base/$dir -n "$name"
done
