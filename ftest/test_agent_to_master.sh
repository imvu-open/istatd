#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

###############################
# MAIN 
###############################

start_server master
check_get 18011 "/?a=*" '"count":0'
start_server agent

send_stat agent "test.counter" 42
send_stat agent "#beverly_hills" 90210
send_stat agent "test.other_counter^a^b^c" 42
flush_istatd agent
flush_istatd master
test_counter master "test/counter/10s" 42
test_counter master "test/other_counter/a/10s" 42
test_counter master "test/other_counter/b/10s" 42
test_counter master "test/other_counter/c/10s" 42

send_event agent "!test.event|superevent|testhost"
send_stat agent "test.postevent" 99
flush_istatd agent
flush_istatd master
test_counter master "test/postevent/10s" 99

check_get 18011 "/?a=*" '"count":1' '"_online":true' '"version":"' '"proto":"x ' '"hostname":"' '"beverly_hills":"90210'

purge_istatd 18032

#Clean up and exit
cleanup_test
rm -rf "$DBDIR"

