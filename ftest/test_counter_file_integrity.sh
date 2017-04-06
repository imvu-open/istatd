#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"
TESTCOUNTER="tep.testing_increment_counter"

function the_test()
{
    local SERVER_TYPE=$1
    local comparison=$2

    let TIME_NOW=1325395000
    let TIME_10S=$((TIME_NOW - 3600))

    let TIME0=1325391400
    let TIME1=`expr $TIME0+500`
    let TIME2=`expr $TIME0+900`

    echo "BASE SIZE"
    send_stat $SERVER_TYPE "$TESTCOUNTER" $TIME0 10
    wait_for_counter $SERVER_TYPE "$TESTCOUNTER/10s"
    flush_istatd $SERVER_TYPE
    BASE_SIZE=$(counter_size $SERVER_TYPE "$TESTCOUNTER/10s")
    BASE_SIZE_APPARENT=$(counter_size_apparent $SERVER_TYPE "$TESTCOUNTER/10s")
    echo $BASE_SIZE
    echo $BASE_SIZE_APPARENT

    $comparison $BASE_SIZE $BASE_SIZE_APPARENT "Base Size Comparison"

    echo "10S SIZE WITH BUCKETS"
    for i in {0..20}; do send_stat $SERVER_TYPE "$TESTCOUNTER" $((TIME_10S+i*180)) 10; done
    wait_for_counter $SERVER_TYPE "$TESTCOUNTER/10s"
    flush_istatd $SERVER_TYPE
    SIZE_10s_DATA=$(counter_size $SERVER_TYPE "$TESTCOUNTER/10s")
    SIZE_APPARENT_10s_DATA=$(counter_size_apparent $SERVER_TYPE "$TESTCOUNTER/10s")
    echo $SIZE_10s_DATA
    echo $SIZE_APPARENT_10s_DATA
    test_counter $SERVER_TYPE "$TESTCOUNTER/10s" 20 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10

    $comparison $SIZE_10s_DATA $SIZE_APPARENT_10s_DATA "With 10S Buckets Comparison"
}



test_name counter_size_preallocated
    start_server single
    the_test single assert_equal
    kill_server single




test_name counter_size_sparse
    start_server sparse
    the_test sparse assert_lt
    kill_server sparse


cleanup_test
