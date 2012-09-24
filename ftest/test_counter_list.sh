#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"


start_server single

send_stat 18001 foo.bar.baz 2
send_stat 18001 foo.bar.quux 3

wait_for_file $DBDIR/single-store/foo/bar/quux/10s

check_counters 18011 'foo.bar.*' 'foo.bar.quux' 'foo.bar.baz'

# do some other testing first!
check_post 18011 "?s=global" '{"test.a":1,"test.b":"some string"}' '"success":true'
check_post 18011 "?s=local" '{"first.key":"aa","second.key":"bb"}' '"success":true'
check_get 18011 "?s=local&sk=first.*" '"first.key":"aa"'
check_get 18011 "?s=local&sk=*.key" '"first.key":"aa"' '"second.key":"bb"'
# note: the integer turned to string here!
check_get 18011 "?s=global" '"test.a":"1"' '"test.b":"some string"'

flush_istatd 18031
wait_for_file $DBDIR/single-settings/global.set
wait_for_file $DBDIR/single-settings/local.set

# verify that files are re-loaded after a flush
cat > $DBDIR/single-settings/local.set << 'EOF'
# istatd settings 1
from.disk=yes
to.disk=no
EOF

flush_istatd 18031
check_get 18011 "?s=local&sk=*.disk" '"from.disk":"yes"' '"to.disk":"no"'

# these may take 10 seconds to show up
wait_for_counter single istatd/pagecache/10s
check_counters 18011 'istatd.*' 'istatd.pagecache.misses.time_10s' 'istatd.pagecache.hits.time_10s'

cleanup_test

