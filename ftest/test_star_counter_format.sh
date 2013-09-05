#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

start_server single

send_stat single *tep^tep_die^tep_die2 1

wait_for_file $DBDIR/single-store/tep/tep_die/10s
bin/istatd_fileinfo $DBDIR/single-store/tep/tep_die/10s | grep 'flags' | grep -q 0x1 || failure 'stat file is not a counter'
wait_for_file $DBDIR/single-store/tep/tep_die2/10s
bin/istatd_fileinfo $DBDIR/single-store/tep/tep_die2/10s | grep 'flags' | grep -q 0x1 || failure 'stat file is not a counter'

cleanup_test
