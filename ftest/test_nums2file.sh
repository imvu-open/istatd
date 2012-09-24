#!/bin/bash

bin/istatd_nums2file --stat-file /tmp/foobar$$.st --interval 30 << 'EOF'
1
2
3
4
5
6
7
EOF
[ -f /tmp/foobar$$.st ] || failure "nums2file did not work"

rm /tmp/foobar$$.st

bin/istatd_nums2file --stat-file /tmp/foobar$$.st --interval 30 --time-in-file --zero-time 1000 << 'EOF'
1000 1
1010 2
1020 3
1030 4
1040 5
1050 6
1060 7
EOF
[ -f /tmp/foobar$$.st ] || failure "nums2file did not work"

bin/istatd_filedump /tmp/foobar$$.st > /tmp/result$$.st

cat > /tmp/expected$$.st << EOF
DATE,SUM,SUMSQ,MIN,MAX,COUNT,AVG,SDEV
"1970-01-01T00:16:30Z",3,5,1,2,2,1.5,0.707107
"1970-01-01T00:17:00Z",12,50,3,5,3,4,1
"1970-01-01T00:17:30Z",13,85,6,7,2,6.5,0.707107
EOF

diff /tmp/expected$$.st /tmp/result$$.st || failure "results to not match expected values"

rm /tmp/foobar$$.st /tmp/expected$$.st /tmp/result$$.st
