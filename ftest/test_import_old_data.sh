#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

let START_TIME=1111123450
let END_TIME=$START_TIME+9000

echo "Creating stat files"
create_stat_files single foo.bar $START_TIME $END_TIME
echo "Starting server"
start_server single --fake-time $END_TIME


echo "started server"

test_name old_packets_within_file_interval_are_accepted
    let LONG_TIME_AGO=$END_TIME-12000
    send_stat single foo.bar $LONG_TIME_AGO 1337
    flush_istatd single

    assert_equal 0 `dump_counter single foo.bar 10s | grep -c ,1337,` "Not supposed to be in 10s file"
    assert_equal 1 `dump_counter single foo.bar 5m  | grep -c ,1337,` "Missing from 5m file"
    assert_equal 1 `dump_counter single foo.bar 1h  | grep -c ,1337,` "Missing from 1h file"


test_name packets_from_crazy_times_make_sane_statfile
    let TIME_0=$START_TIME-1250000
    let TIME_1=$START_TIME-250000
    let TIME_2=$START_TIME-50000
    let TIME_3=$START_TIME-10000
    let TIME_4=$START_TIME
    let TIME_5=$START_TIME+10
    let TIME_6=$START_TIME+20

    send_stat single foo.baz $TIME_4 1337
    send_stat single foo.baz $TIME_2 1337
    send_stat single foo.baz $TIME_1 1337
    send_stat single foo.baz $TIME_0 1337
    send_stat single foo.baz $TIME_3 1337
    send_stat single foo.baz $TIME_5 1337
    send_stat single foo.baz $TIME_6 1337
    flush_istatd single

    #10s file catches T4-T6, adding them to existing data
    assert_equal 3 `dump_counter single foo.baz 10s | grep -c ",1337,"` "10s buckets mismatch"

    #5m bucket aggregates T4-T6, plus existing data, and also catches T2-T3
    assert_equal 1 `dump_counter single foo.baz 5m  | grep -c ",4011,.*,1337,"` "5m buckets mismatch"
    assert_equal 3 `dump_counter single foo.baz 5m  | grep -c ",1337,"` "5m earlybuckets expected"

    #1h file aggregates T4-T6, and catches T0-T3
    assert_equal 1 `dump_counter single foo.baz 1h | grep -c ",4011,.*,1337,"` "1h buckets mismatch"
    assert_equal 5 `dump_counter single foo.baz 1h | grep -c ",1337,"` "1h earlybuckets mismatch"
cleanup_test
