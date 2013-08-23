#!/bin/bash

# If a counter only occurs occasionally, and then a recent range of time is requested from istatd for this counter, this may be PAST
# the last time the counter was incremented.  Make sure istatd still returns zeroed buckets for the requested interval

# -----------------------------------------------------------------> (time)
#   X
#   ^-- counter increment ^                      ^
#                         +-- start of range     +--- end of range

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"
TESTCOUNTER="tep.testing_increment_counter"

start_server single

test_name test_recent_buckets_returned_for_occasional_counters
    let TIME0=1325394000
    # ltime is the "end of block" time (the "last" time that's written to the file)
    let LTIME=1325395100
    let LTIME20=`expr $LTIME+20`
    let LTIME80=`expr $LTIME+80`

    # Send a bunch of stats, first dense and then sparse.
    send_stat single "*$TESTCOUNTER" $TIME0 10
    flush_istatd single

    set_faketime single $LTIME80

    check_get 18011 "${TESTCOUNTER}?start=${LTIME20}&end=${LTIME80}&count=6" '"avg":0,"count":1'

cleanup_test
