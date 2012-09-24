#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

start_server single --fake-time 50000
check_get 18011 "/?a=*" '"count":0'
wait_for_stats 18031 "fakeTime.value=50000"

#Clean up and exit
cleanup_test
rm -rf "$DBDIR"

