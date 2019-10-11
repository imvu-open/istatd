#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

###############################
# MAIN 
###############################

start_server master
check_get 18011 "/?a=*" '"count":0'
start_server store_n_forward

send_stat store_n_forward "test.counter" 42
send_stat store_n_forward "test.other_counter^a^b^c" 42
flush_istatd store_n_forward
flush_istatd master
test_counter master "test/counter/10s" 42
test_counter master "test/other_counter/a/10s" 42
test_counter master "test/other_counter/b/10s" 42
test_counter master "test/other_counter/c/10s" 42

check_get 18011 "/?a=*" '"count":1' '"_online":true' '"version":"' '"proto":"x ' '"hostname":"'
check_get 18013 "/?a=*" '"count":2' '"_online":false'

purge_istatd 18033 # storenforward

#Clean up and exit
cleanup_test
rm -rf "$DBDIR"

