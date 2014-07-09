#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

# start a server with fake time
# push 15 stats into the server via tcp & netcat
# flush
# verify that the peak eager connection count is <= 5
start_server single --fake-time 84000
for i in `seq 84001 84015` ; do 
    # counter timestamp val valsq valmin valmax count
    send_stat single "*testconn" $i 1 1 1 1 1
done

flush_istatd single

echo checking peak eager connection count
for max in `dump_counter single istatd.counter.eagerconns 10s | grep -v DATE | cut -f5 -d,` ; do 
    if [ $max -gt 5 ] ; then
        failure "Peak eager connections exceeded 5 during the test"
    fi
done

cleanup_test

