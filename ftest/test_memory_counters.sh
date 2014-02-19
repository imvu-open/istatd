#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"


start_server single

wait_for_counter single istatd/app/memory/vmsize/10s
wait_for_counter single istatd/app/memory/vmrss/10s
check_counters 18011 'istatd.*' 'istatd.app.memory.vmsize' 'istatd.app.memory.vmrss'

cleanup_test

