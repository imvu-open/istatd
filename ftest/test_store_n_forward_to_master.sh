#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

###############################
# MAIN 
###############################

start_server master
check_get 18011 "/?a=*" '"count":0'
start_server store_n_forward

send_stat 18002 "test.counter" 42
send_stat 18002 "test.counter^a^b^c" 42
flush_istatd 18032
flush_istatd 18031
test_counter master "test/counter/10s" 42
test_counter master "test/counter/a/10s" 42
test_counter master "test/counter/b/10s" 42
test_counter master "test/counter/c/10s" 42

test_counter master "istatd/admin/connections/10s" 0 0 0.1 0 0
test_counter master "istatd/admin/commands/10s" 0 0 0.1 0 0

check_get 18011 "/?a=*" '"count":1' '"_online":true' '"version":"' '"proto":"x ' '"hostname":"'
check_get 18012 "/?a=*" '"count":2' '"_online":false'

purge_istatd 18032

#Clean up and exit
cleanup_test
rm -rf "$DBDIR"

