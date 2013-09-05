#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

start_server single

send_stat single tep.tep_die 1
send_stat single tep.tep_live.and_let_die 2

wait_for_file $DBDIR/single-store/tep/tep_live/and_let_die/10s
[ -f $DBDIR/single-store/istatd/counter/closed/10s ] && echo "file already exists" && sleep 1000
delete_ctr single tep.tep_die
flush_istatd single
wait_for_file $DBDIR/single-store/istatd/counter/closed/10s

cleanup_test
