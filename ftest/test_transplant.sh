#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"

SLAVEDIR="$DBDIR/slave"
mkdir -p "$SLAVEDIR"
MASTERDIR="$DBDIR/master"
mkdir -p "$MASTERDIR"
TRANSPLANT="$SCRIPTDIR/../bin/istatd_transplant"

echo "bin/istatd_translant --test"
      bin/istatd_transplant --test || failure "unit tests failed"
# clean up from previous runs, if they timed out
echo "killall -q -s 5s -v istatd_transplant"
      killall -q -s 5s -v istatd_transplant || true

echo "generating files..."
bin/istatd_nums2file --stat-file "$SLAVEDIR/some/counter/10s" --interval 10 --time-in-file --zero-time 100000 << 'EOF'
100000 100
100010 200
100020 300
100200 250
100210 210
100220 170
100230 130
100400 90
100410 60
100420 30
EOF

bin/istatd_nums2file --stat-file "$MASTERDIR/some/counter/10s" --interval 10 --time-in-file --zero-time 100000 << 'EOF'
100010 200
100020 300
100030 400
100040 250
100210 210
100220 170
100230 130
100400 90
100410 60
100420 30
EOF

bin/istatd_nums2file --stat-file "$MASTERDIR/other/counter/10s" --interval 10 --time-in-file --zero-time 100000 << 'EOF'
100000 0
100010 1
100020 2
100030 3
100040 4
100050 5
100060 6
100070 7
100080 8
100090 9
100100 10
EOF

bin/istatd_nums2file --stat-file "$SLAVEDIR/missing/counter/10s" --interval 10 --time-in-file --zero-time 100000 << 'EOF'
100000 0
100010 1
100020 2
100030 3
100040 4
100050 5
100060 6
100070 7
100080 8
100090 9
100100 10
EOF


function kill_transplant {
    echo "Running kill_transplant hook"
    killall -q -v istatd_transplant || true
}

echo "starting server"
EXIT_HOOK="kill_transplant"
echo "$TRANSPLANT" --listen 9156 --store "$MASTERDIR" --pidfile master-pid.pid --debug
     "$TRANSPLANT" --listen 9156 --store "$MASTERDIR" --pidfile master-pid.pid --debug > "$DBDIR/master.log" 2>&1 </dev/null &
MASTERPID="$!"

wait_for_file "$MASTERDIR/master-pid.pid"
sleep 1 # wait for master to be able to accept -- this should somehow be sync

echo "starting client"
echo "$TRANSPLANT" --connect localhost:9156 --store "$SLAVEDIR" --fromtime 100000 --totime 110000 --pidfile client-pid.pid --progress 1 --debug
     "$TRANSPLANT" --connect localhost:9156 --store "$SLAVEDIR" --fromtime 100000 --totime 110000 --pidfile client-pid.pid --progress 1 --debug > "$DBDIR/slave.log" 2>&1

echo "client done; shutting down server"

sleep 1
kill -INT $MASTERPID
sleep 1
EXIT_HOOK=""

bin/istatd_filedump "$SLAVEDIR/some/counter/10s" > $DBDIR/slave-out.txt
bin/istatd_filedump "$MASTERDIR/some/counter/10s" > $DBDIR/master-out.txt
diff -u "$DBDIR/slave-out.txt" "$DBDIR/slave-out.txt" || failure "files are not properly synced."

cleanup_test
