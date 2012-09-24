#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

start_server single

send_stat 18001 tep.tep_die 1
send_stat 18001 tep.tep_live.and_let_die 2

wait_for_file $DBDIR/single-store/tep/tep_live/and_let_die/10s

PORT="18011"
PATTERN="tep.%3F"
curl -s "http://localhost:$PORT/?q=$PATTERN" | sed -e 's/},{/\n/g' | grep tep.tep_die | grep -q '"is_leaf":true' || failure "tep.tep_die should be a leaf"
curl -s "http://localhost:$PORT/?q=$PATTERN" | sed -e 's/},{/\n/g' | grep tep.tep_live | grep -q '"is_leaf":false' || failure "tep.tep_live should not be a leaf"

cleanup_test
