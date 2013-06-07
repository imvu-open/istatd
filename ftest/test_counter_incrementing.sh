#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"
TESTCOUNTER="tep.testing_increment_counter"

start_server single

test_name counter_increments_get_rolled_up_correctly
    let TIME0=1325394000
    let TIME1=`expr $TIME0+1`
    let TIME2=`expr $TIME0+2`
    let TIME60=`expr $TIME0+60`
    let TIME120=`expr $TIME0+120`
    let TIME180=`expr $TIME0+180`
    let TIME500=`expr $TIME0+500`
    let TIME600=`expr $TIME0+600`
    let TIME900=`expr $TIME0+900`

    # Send a bunch of stats, first dense and then sparse.
    send_stat single "*$TESTCOUNTER" $TIME0 10
    send_stat single "*$TESTCOUNTER" $TIME1 10
    send_stat single "*$TESTCOUNTER" $TIME2 10    
    send_stat single "*$TESTCOUNTER" $TIME60 100
    send_stat single "*$TESTCOUNTER" $TIME120 100
    send_stat single "*$TESTCOUNTER" $TIME180 100
    send_stat single "*$TESTCOUNTER" $TIME500 1000
    send_stat single "*$TESTCOUNTER" $TIME600 1000
    send_stat single "*$TESTCOUNTER" $TIME900 1000

    flush_istatd single    
    test_counter single "$TESTCOUNTER/10s" 0 0 0 0 3 0 0 0 0 0 10 0 0 0 0 0 10 0 0 0 0 0 10 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 100 0 0 0 0 0 0 0 0 0 100 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 100 0 0 0 0 0 0 0 0 0 0 0
    test_counter single "$TESTCOUNTER/5m" 33 100 100 100
    test_counter single "$TESTCOUNTER/1h" 333

cleanup_test
