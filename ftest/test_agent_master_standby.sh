#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

###############################
# MAIN 
###############################

start_server master
check_get 18011 "/?a=*" '"count":0'
start_server store_n_forward
start_server agent_to_store_n_forward

# these should be ignored for being too old
send_stat agent_to_store_n_forward "test.counter" -10 39
send_stat agent_to_store_n_forward "test.counter" 1 40
send_stat agent_to_store_n_forward "test.counter" 10 41
# these should be included
send_stat agent_to_store_n_forward "test.counter" 20 42
send_stat agent_to_store_n_forward "test.counter" 30 43
send_stat agent_to_store_n_forward "test.counter" 40 44
send_stat agent_to_store_n_forward "test.counter" 50 45

flush_istatd agent_to_store_n_forward
sleep 1 # terrible
flush_istatd store_n_forward
sleep 1 # terrible
flush_istatd master
sleep 1 # terrible

test_counter store_n_forward "test/counter/10s" 42 43 44 45
test_counter master "test/counter/10s" 42 43 44 45

purge_istatd 18033 # store_n_forward
purge_istatd 18032 # agent_to_store_n_forward

#Clean up and exit
cleanup_test
rm -rf "$DBDIR"

