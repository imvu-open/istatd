#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

echo "Testing a simple delete closes the counter"
start_server single

send_stat single tep.tep_die 1
send_stat single tep.tep_live.and_let_die 2

wait_for_file $DBDIR/single-store/tep/tep_live/and_let_die/10s
[ -f $DBDIR/single-store/istatd/counter/closed/10s ] && echo "file already exists" && sleep 1000
delete_ctr single tep.tep_die
flush_istatd single
wait_for_file $DBDIR/single-store/istatd/counter/closed/10s

kill_server single

rm -rf $DBDIR/single-store/istatd/counter/closed





echo "Testing a pattern delete closes counters, removes/backsup files, and refreshes keys"
start_server single

send_stat single tep.asd.dds 1
send_stat single tep.asd.aaa 2
send_stat single tep.tep_die 1

wait_for_file $DBDIR/single-store/tep/asd/dds/10s
[ -f $DBDIR/single-store/istatd/counter/closed/10s ] && echo "file already exists" && sleep 1000

test_name GET_before_delete_and_purge
curl -s "http://localhost:18011/?q=*" > $TEST_OUT
cat $TEST_OUT | grep -q "tep.asd.dds" || failure "Couldnt find expected tep.asd.dds counter in json response"
cat $TEST_OUT | grep -q "tep.asd.aaa" || failure "Couldnt find expected tep.asd.aaa counter in json response"
cat $TEST_OUT | grep -q "tep.tep_die" || failure "Couldnt find expected tep.tep_die counter in json response"

delete_pattern single 'tep.asd*'
flush_istatd single
wait_for_file $DBDIR/single-store/istatd/counter/closed/10s
[ -f $DBDIR/single-store/tep/asd/dds/10s ] && failure "This should be gone, $DBDIR/single-store/tep/asd/dds/10s"
[ -f $DBDIR/single-store/tep/asd/aaa/10s ] && failure "This should be gone, $DBDIR/single-store/tep/asd/aaa/10s"
[ -f $DBDIR/single-store/tep/asd/10s ] && failure "This should be gone, $DBDIR/single-store/tep/asd/10s"
wait_for_file $DBDIR/single-store.bak/tep/asd/dds/10s
wait_for_file $DBDIR/single-store.bak/tep/asd/aaa/10s
wait_for_file $DBDIR/single-store.bak/tep/asd/10s

[ -d $DBDIR/single-store/tep/asd ] && failure "This should be gone, $DBDIR/single-store/tep/asd"

test_name GET_after_delete_and_purge
curl -s "http://localhost:18011/?q=*" > $TEST_OUT

cat $TEST_OUT | grep -q "tep.asd.dds" && failure "Found unexpected tep.asd.dds counter in json response after purge"
cat $TEST_OUT | grep -q "tep.tep_live" || failure "Did not find expected tep.tep_live counter in json response after purge"


kill_server single

rm -rf $DBDIR/single-store/istatd/counter/closed




echo "Testing a pattern delete on a deeper tree closes counters, removes/backsup files, and refreshes keys"
start_server single

send_stat single tep.asd.dds 1
send_stat single tep.asd.aaa 2
send_stat single tep.tep_die 1
send_stat single tep.asd.dds.hrr 1
send_stat single tep.asd.dds.hrr.mrr 1
send_stat single tep.asd.dds.hrr.trr 1
send_stat single tep.asd.dds.srr 1
send_stat single tep.asd.dds.srr.lrr 1
send_stat single tep.asd.dds.srr.drr 1

wait_for_file $DBDIR/single-store/tep/asd/dds/10s
[ -f $DBDIR/single-store/istatd/counter/closed/10s ] && echo "file already exists" && sleep 1000

delete_pattern single 'tep.asd*'
flush_istatd single
wait_for_file $DBDIR/single-store/istatd/counter/closed/10s
[ -f $DBDIR/single-store/tep/asd/dds/10s ] && failure "This should be gone, $DBDIR/single-store/tep/asd/dds/10s"
[ -f $DBDIR/single-store/tep/asd/aaa/10s ] && failure "This should be gone, $DBDIR/single-store/tep/asd/aaa/10s"
[ -f $DBDIR/single-store/tep/asd/10s ] && failure "This should be gone, $DBDIR/single-store/tep/asd/10s"
wait_for_file $DBDIR/single-store.bak/tep/asd/dds/10s
wait_for_file $DBDIR/single-store.bak/tep/asd/aaa/10s
wait_for_file $DBDIR/single-store.bak/tep/asd/10s

[ -d $DBDIR/single-store/tep/asd ] && failure "This should be gone, $DBDIR/single-store/tep/asd"

test_name GET_after_delete_and_purge
curl -s "http://localhost:18011/?q=*" > $TEST_OUT

cat $TEST_OUT | grep -q "tep.asd.dds" && failure "Found unexpected tep.asd.dds counter in json response after purge"
cat $TEST_OUT | grep -q "tep.tep_live" || failure "Did not find expected tep.tep_live counter in json response after purge"


kill_server single

rm -rf $DBDIR/single-store/istatd/counter/closed





echo "Delete counters quickly in order to stress the queueing of key refreshes"
start_server single

send_stat single tep.tep_die 1
send_stat single tep.asd.dds 1
send_stat single tep.asd.aaa 2
send_stat single tep.tep_die 1
send_stat single tep.asd.dds.hrr 1
send_stat single tep.asd.dds.hrr.mrr 1
send_stat single tep.asd.dds.hrr.trr 1
send_stat single tep.asd.dds.srr 1
send_stat single tep.asd.dds.srr.lrr 1
send_stat single tep.asd.dds.srr.drr 1
send_stat single tep.tep_live.and_let_die 2

wait_for_file $DBDIR/single-store/tep/tep_live/and_let_die/10s
[ -f $DBDIR/single-store/istatd/counter/closed/10s ] && echo "file already exists" && sleep 1000
delete_ctr single tep.tep_die
delete_ctr single tep.asd.dds.srr.lrr
delete_ctr single tep.asd.dds.hrr
delete_ctr single tep.asd.aaa

flush_istatd single
wait_for_file $DBDIR/single-store/istatd/counter/closed/10s

wait_for_file $DBDIR/single-store.bak/tep/tep_die/10s
wait_for_file $DBDIR/single-store.bak/tep/asd/dds/srr/lrr/10s
wait_for_file $DBDIR/single-store.bak/tep/asd/dds/hrr/10s
wait_for_file $DBDIR/single-store.bak/tep/asd/aaa/10s


[ -d $DBDIR/single-store/tep/tep_die ] && failure "This should be gone, $DBDIR/single-store/tep/tep_die"
[ -d $DBDIR/single-store/tep/aaa ] && failure "This should be gone, $DBDIR/single-store/tep/aaa"
[ -d $DBDIR/single-store/tep/asd/dds/srr/lrr ] && failure "This should be gone, $DBDIR/single-store/tep/asd/dds/srr/lrr"
[ -d $DBDIR/single-store/tep/asd/dds/srr ] || failure "This should be here, $DBDIR/single-store/tep/asd/dds/srr"

test_name GET_after_delete_and_purge
curl -s "http://localhost:18011/?q=*" > $TEST_OUT

cat $TEST_OUT | grep -q "tep.tep_die" && failure "Found unexpected tep.tep_die counter in json response after purge"
cat $TEST_OUT | grep -q "tep.asd.aaa" && failure "Found unexpected tep.asd.aaa counter in json response after purge"
cat $TEST_OUT | grep -q "tep.tep_live" || failure "Did not find expected tep.tep_live counter in json response after purge"

kill_server single

rm -rf $DBDIR/single-store/istatd/counter/closed

cleanup_test
