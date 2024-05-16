#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

###############################
# MAIN 
###############################

start_server master
check_get 18011 "/?a=*" '"count":0'
start_server agent_prometheus
check_get 18121 "metrics"
check_get 18121 "metrics_wrong_path" "Malformed url"

INSTANCE=agent
send_stat ${INSTANCE} "test.gauge.1" 10 41
send_stat ${INSTANCE} "test.gauge.1" 20 42
send_stat ${INSTANCE} "*test.counter^host.h^role.r^class.c" 20 1
send_stat ${INSTANCE} "*test.counter^host.h^role.r^class.c" 15 1
send_stat ${INSTANCE} "*test.counter.2^host.hh^role.rr" 20 1
send_stat ${INSTANCE} "test.gauge.2" 30 43
flush_istatd ${INSTANCE}
test_name GET_metrics_returns_types_and_values
check_get_metrics 18121 $TEST_OUT

send_stat ${INSTANCE} "*test.counter^host.h^role.r^class.c" 25 1
send_stat ${INSTANCE} "test.gauge" 25 43
send_stat ${INSTANCE} "*test.counter^c"  1
test_name GET_metrics_returns_cumulative_counters
check_get_metrics 18121 $TEST_OUT

purge_istatd 18032

#Clean up and exit
cleanup_test
rm -rf "$DBDIR"

