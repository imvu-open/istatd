#!/bin/bash

SCRIPTDIR=`dirname $0`
source "$SCRIPTDIR/functions"


start_server single

send_stat single foo.bar.baz 2
send_stat single foo.bar.quux 3

wait_for_file $DBDIR/single-store/foo/bar/quux/10s

check_counters_gzip 18011 'foo.bar.*' 'foo.bar.quux' 'foo.bar.baz'

# do some other testing first!
check_post_gzip 18011 "?s=global" '{"test.a":1,"test.b":"some string"}' '"success":true'
check_post_gzip 18011 "?s=local" '{"first.key":"aa","second.key":"bb"}' '"success":true'
check_get_gzip 18011 "?s=local&sk=first.*" '"first.key":"aa"'
check_get_gzip 18011 "?s=local&sk=*.key" '"first.key":"aa"' '"second.key":"bb"'
# note: the integer turned to string here!
check_get_gzip 18011 "?s=global" '"test.a":"1"' '"test.b":"some string"'

flush_istatd single
wait_for_file $DBDIR/single-settings/global.set
wait_for_file $DBDIR/single-settings/local.set

# verify that files are re-loaded after a flush
cat > $DBDIR/single-settings/local.set << 'EOF'
# istatd settings 1
from.disk=yes
to.disk=no
EOF

flush_istatd single
check_get_gzip 18011 "?s=local&sk=*.disk" '"from.disk":"yes"' '"to.disk":"no"'

# these may take 10 seconds to show up
wait_for_counter single istatd/pagecache/10s
check_counters_gzip 18011 'istatd.*' 'istatd.pagecache.misses.time_10s' 'istatd.pagecache.hits.time_10s'

check_counters_gzip 18011 '*' 'istatd.pagecache.misses.time_10s' 'istatd.pagecache.hits.time_10s'

for i in {1..30}; do
    send_stat single foo.bar.baz.$i 2
done

SIZE=$(check_get_resp_size 18011 '?q=*')
GZIP_SIZE=$(check_get_resp_size_gzip 18011 '?q=*')

if [ "$GZIP_SIZE" -gt "$SIZE" ]; then
    failure "GZIP size was larger than size: $GZIP_SIZE -- $SIZE"
fi

cleanup_test

