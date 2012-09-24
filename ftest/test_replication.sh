#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"



onexit cleanup_exit
trap 'echo "child died"' SIGCHLD

mkdir -p $DBDIR

start_server master
start_server slave

wait_for_stats 18031 "replicaServer.current=1"
wait_for_stats 18032 "replicaOf.connected=1"

cleanup_test



