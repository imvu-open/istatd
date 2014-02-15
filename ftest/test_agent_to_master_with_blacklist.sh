#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

###############################
# MAIN 
###############################

mkdir -p /var/tmp/blacklist/
echo $(hostname) > /var/tmp/blacklist/ftest_blacklist.set

start_server master_w_blacklist
check_get 18011 "/?a=*" '"count":0'
start_server agent

send_stat agent "test.counter" 42
send_stat agent "#beverly_hills" 90210
send_stat agent "test.counter^a^b^c" 42
flush_istatd agent
sleep 1 # terrible
flush_istatd master_w_blacklist
test_counter_does_not_exist master_w_blacklist "test/counter/10s" 42
test_counter_does_not_exist master_w_blacklist "test/counter/a/10s" 42
test_counter_does_not_exist master_w_blacklist "test/counter/b/10s" 42
test_counter_does_not_exist master_w_blacklist "test/counter/c/10s" 42

check_get_re 18011 "/?a=*" 'count.+1' 'hostname.+blacklisted.+true.+_online.+false'

kill_server agent
sleep 1
start_server agent

echo > /var/tmp/blacklist/ftest_blacklist.set
sleep 2 # 2xterrible

send_stat agent "test.postevent" 99
flush_istatd agent
sleep 1 # terrible
flush_istatd master_w_blacklist
test_counter master_w_blacklist "test/postevent/10s" 99
test_counter_does_not_exist master_w_blacklist "test/counter/10s" 42
sleep 1 # terrible

check_get_re 18011 "/?a=*" 'count.+2' 'hostname.+_online.+true' 'hostname.+blacklisted.+true.+_online.+false'

rm -f /var/tmp/blacklist/ftest_blacklist.set
sleep 2 # 2xterrible

send_stat agent "test.event.after.blacklist.delete" 99
flush_istatd agent
sleep 1 # terrible
flush_istatd master_w_blacklist
test_counter master_w_blacklist "test/event/after/blacklist/delete/10s" 99
sleep 1 # terrible

check_get_re 18011 "/?a=*" 'count.+2' 'hostname.+_online.+true' 'hostname.+blacklisted.+true.+_online.+false'

purge_istatd 18032

#Clean up and exit
cleanup_test
rm -rf "$DBDIR"
rm -rf /var/tmp/blacklist

