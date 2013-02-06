#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"
TESTCOUNTER="tep.testing_ignore_counter"

start_server single

test_name counter_ignored_correctly

    send_stat single "*$TESTCOUNTER" 10 10
    ignore_istatd single $TESTCOUNTER
    send_stat single "*$TESTCOUNTER" 10 100
    
    flush_istatd single    
    test_counter single "$TESTCOUNTER/10s" 1

cleanup_test
