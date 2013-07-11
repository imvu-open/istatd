#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"


start_server trailing

send_stat trailing a 100000 2
send_stat trailing b 100000 3
send_stat trailing a 100050 2
send_stat trailing b 100050 3

wait_for_file $DBDIR/trailing-store/a/10s

check_counters 18011 'a' 'a'

flush_istatd trailing

send_stat trailing a 103601 5
send_stat trailing b 107200 0
send_stat trailing a 107209 10
flush_istatd trailing
send_stat trailing a 107210 1
send_stat trailing a 107220 1
send_stat trailing a 107230 1
send_stat trailing a 107240 1
flush_istatd trailing
send_stat trailing a 107250 1
send_stat trailing a 107260 1
send_stat trailing a 107320 1
flush_istatd trailing
send_stat trailing a 107380 1
send_stat trailing a 107440 1
send_stat trailing a 107500 1
send_stat trailing a 107560 1

wait_for_file $DBDIR/trailing-store/a/ma-1h
flush_istatd trailing

# these may take 10 seconds to show up
wait_for_counter trailing istatd/pagecache/10s
check_counters 18011 'istatd.*' 'istatd.pagecache.misses.time_10s' 'istatd.pagecache.hits.time_10s'

#cleanup_test

