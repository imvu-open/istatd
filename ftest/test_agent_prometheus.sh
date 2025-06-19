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
send_stat ${INSTANCE} "*test.counter^host.h^role.r^class.c^d.f" 20 1
send_stat ${INSTANCE} "*test.counter^host.h^role.r^class.c^d.f" 15 1
send_stat ${INSTANCE} "test.gauge.2^host.h^class.foo" 20 43
send_stat ${INSTANCE} "*test.counter.foo" 15 1
flush_istatd ${INSTANCE}
test_name GET_metrics_returns_types_and_values
check_get_metrics 18121 $TEST_OUT

send_stat ${INSTANCE} "*test.counter^host.h^role.r^class.c^d.f" 25 1
send_stat ${INSTANCE} "test.gauge" 25 43
test_name GET_metrics_returns_cumulative_counters
check_get_metrics 18121 $TEST_OUT

send_stat ${INSTANCE} "*test.counter^host.h^role.r^role.r-2" 51 1
send_stat ${INSTANCE} "test.gauge.2^host.h^class.foo" 35 44
send_stat ${INSTANCE} "test.gauge.2^host.h^class.foo" 40 45
test_name GET_metrics_returns_new_metric_if_tag_changes
check_get_metrics 18121 $TEST_OUT

kill_server agent_prometheus


start_server agent_prometheus_no_name_mapping
check_get 18121 "metrics"
check_get 18121 "metrics_wrong_path" "Malformed url"

send_stat ${INSTANCE} "test.gauge.1" 10 41
send_stat ${INSTANCE} "test.gauge.1" 20 42
send_stat ${INSTANCE} "*test.counter^host.h^role.r^class.c^d.f" 20 1
send_stat ${INSTANCE} "*test.counter^host.h^role.r^class.c^d.f" 15 1
send_stat ${INSTANCE} "test.gauge.2^host.h^class.foo" 20 43
send_stat ${INSTANCE} "*test.counter.foo" 15 1
flush_istatd ${INSTANCE}
test_name GET_metrics_returns_types_and_values_no_name_mapping
check_get_metrics 18121 $TEST_OUT

send_stat ${INSTANCE} "*test.counter^host.h^role.r^class.c^d.f" 25 1
send_stat ${INSTANCE} "test.gauge" 25 43
test_name GET_metrics_returns_cumulative_counters_no_name_mapping
check_get_metrics 18121 $TEST_OUT

send_stat ${INSTANCE} "*test.counter^host.h^role.r^role.r-2" 51 1
send_stat ${INSTANCE} "test.gauge.2^host.h^class.foo" 35 44
send_stat ${INSTANCE} "test.gauge.2^host.h^class.foo" 40 45
test_name GET_metrics_returns_new_metric_if_tag_changes_no_name_mapping
check_get_metrics 18121 $TEST_OUT

purge_istatd 18032

#Clean up and exit
cleanup_test
rm -rf "$DBDIR"
