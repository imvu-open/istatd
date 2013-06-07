#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

###############################
# MAIN 
###############################

start_server master
check_get 18011 "/?a=*" '"count":0'
start_server agent

INSTANCE=agent
# these should be ignored for being too old
send_stat ${INSTANCE} "test.counter" -10 39
send_stat ${INSTANCE} "test.counter" 1 40
send_stat ${INSTANCE} "test.counter" 10 41
# these should be forwarded
send_stat ${INSTANCE} "test.counter" 20 42
send_stat ${INSTANCE} "test.counter" 30 43
send_stat ${INSTANCE} "test.counter" 40 44
send_stat ${INSTANCE} "test.counter" 50 45
flush_istatd ${INSTANCE}
sleep 1 # terrible
flush_istatd master

test_counter master "test/counter/10s" 42 43 44 45

purge_istatd 18032

#Clean up and exit
cleanup_test
rm -rf "$DBDIR"

