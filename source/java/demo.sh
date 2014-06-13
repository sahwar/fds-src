#!/bin/bash


for i in ../lib/java/fds-1.0-bin/fds-1.0/* ; do {
    CLASSPATH=$CLASSPATH:$i ;
} done

CLASSPATH=$CLASSPATH:../lib/java/classes

java -classpath $CLASSPATH \
     com.formationds.demo.Main $*
