#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"


start_server no-recurse-single

send_stat no-recurse-single foo.bar.baz 2
send_stat no-recurse-single foo.bar.quux 3

wait_for_file $DBDIR/no-recurse-single-store/foo/bar/quux/10s

check_counters 18011 'foo.bar.*' 'foo.bar.quux' 'foo.bar.baz'
test_counter_does_not_exist no-recurse-single 'foo.10s'
test_counter_does_not_exist no-recurse-single 'foo.bar.10s'

cleanup_test

