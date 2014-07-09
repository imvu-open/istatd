#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

start_server single --fake-time 7000
send_stat single "*test.counter" 2550 10 100 10 10 10
# 2560 will be zero sample in 2560 reduction.
send_stat single "*test.counter" 2570 10 100 10 10 10
send_stat single "*test.counter" 2580 10 100 10 10 10
send_stat single "*test.counter" 2590 10 100 10 10 10
send_stat single "*test.counter" 2600 10 100 10 10 10
# 2610 will be zero sample in 2600 reduction.
# then a bunch of data we don't care about...

send_stat single "*test.counter" 6800 10 100 10 10 10

flush_istatd single

# Reduce to 30 samples (20 second interval)
test_name reduction_test_10s
http_get_counter localhost:18011 2560 3160 30 test.counter > $TEST_OUT
assert_expected $TEST_OUT

test_name reduction_test_5m
http_get_counter localhost:18011 2560 6160 6 test.counter > $TEST_OUT
assert_expected $TEST_OUT

cleanup_test
