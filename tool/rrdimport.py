import xml.dom.minidom
import sys, os
import getopt
import time
import subprocess

# todo
# bug: doesn't handle counter data properly (need to force n == number of rollup buckets)
# bug: requires counter file to exist before it can write

# edit these to match your actual intervals

ISTATD_STORE_ROOT="/var/db/istatd/store"
SECONDS_PER_CACTI_AGGREGATION_LEVEL = 300.0
SECONDS_PER_ISTATD_BUCKET = 10.0
NOW = time.time();
# retention: 10s:10d,5m:1y9d,1h:6y12d
RETENTION_10S = 11.0 * 60 * 60 * 24  # send 11 days of 10s second data
RETENTION_5M = 11.0 * 60 * 60 * 24  # send 11 days of 10s second data
RETENTION_1H = 11.0 * 60 * 60 * 24  # send 11 days of 10s second data
CUTOFF_10S = time.time() - RETENTION_10S

# associate intervals with their bucket counts
INTERVALS = ("10s", "5m", "1h")
BUCKETS = (1, 30, 360)
CUTOFF = (
    NOW - (NOW % 10) - 11.0 * 60 * 60 * 24,  # send 11 days of 10s second data
    NOW - (NOW % 300) - 376.0 * 60 * 60 * 24,  # send 376 days of 5m second data
    NOW - (NOW % 3600) - 2205.0 * 60 * 60 * 24,  # send 2205 days of 1h second data (just over 6 years)
)

##############################################################################
# INTERNAL CLASSES
class Bucket:
    def __init__(self, point = None):
        if (point is not None):
            self.sum_ = point
            self.count_ = 1
            self.min_ = point
            self.max_ = point
        else:
            self.sum_ = 0.0
            self.count_ = 0
            self.min_ = None
            self.max_ = None

    def add(self, point):
        if (self.min_ is not None and point < self.min_):
            self.min_ = point
        if (self.max_ is not None and point > self.max_):
            self.max_ = point
        self.sum_ += point
        self.count_ += 1

    def emit(self, timestamp, fh):
        avg = self.sum_ / self.count_
        sumsq = avg * avg * self.count_ # compute sum of squares to minimize stddev
        fh.write("%i %f %f %f %f %i\n" % (timestamp, self.sum_, sumsq, self.min_, self.max_, self.count_)) # counter: 1 for 10 second, 30 for 5m, 360 for 1h, 
        
##############################################################################
# SUBROUTINES

#extracts data points from a node
def rra_extractor(node):
    aggregation_level = ''
    data = []
    for child in node.childNodes:
        if child.nodeName == 'cf' and child.firstChild.data.lstrip().rstrip() != 'AVERAGE':
            return False
        elif child.nodeName == 'pdp_per_row':
            aggregation_level = child.firstChild.data.lstrip().rstrip()
        elif child.nodeName == 'database':
            timestamp = value = ''
            for item in child.childNodes:
                if item.nodeType == xml.dom.Node.COMMENT_NODE:
                    (_, _, timestamp) = item.data.partition('/')
                elif item.nodeType == xml.dom.Node.ELEMENT_NODE:
                    value = item.firstChild.firstChild.data.lstrip().rstrip()
                    if value != 'NaN':
                        data.append((int(timestamp.lstrip().rstrip()), float(value)))
    return {'aggregation_level':aggregation_level, 'data':data}

#finds nodes that contain data, then extracts the data
def rra_finder(node, rra_list):
    if node.nodeType == xml.dom.Node.ELEMENT_NODE and node.nodeName == 'rra':
        rra = rra_extractor(node)
        if rra:
            rra_list.append(rra)
    else:
        for child in node.childNodes:
            rra_list = rra_finder(child, rra_list)
    return rra_list


# collapse data into a timestamped list of (aggregation_level, value) pairs.
# uses min aggregation level to resolve overlap
def rra_time_summary(rra_list):
    all_data = {}
    global is_counter
    for rra in rra_list:
        for (timestamp, value) in rra['data']:
            data = all_data.get(timestamp, {})
            if 'aggregation_level' not in data or data['aggregation_level'] > rra['aggregation_level']:
                all_data[timestamp] = {
                    'aggregation_level':int(rra['aggregation_level']), 
                    'value':float(value)
                }

    return all_data


##############################
def emit_istatd_buckets(fh, time_summary, interval, start_time, end_time):
    """ given a set of cacti data, create a sequence of istatd buckets
if the rra data is *finer grained*, then roll up
if the rra data is *coarser grained* then split
inputs: 
  time_summary -- structure from rra_finder
  interval -- istatd bucket size in seconds
  start_time -- start of the istatd bucket interval (reject data before start time)
  end_time -- end of the istatd bucket interval (reject data after end time)
"""
    output = {}
    global is_counter
    multiplier = 1.0
    if is_counter:
        multiplier = interval / SECONDS_PER_ISTATD_BUCKET
    timestamps = time_summary.keys()
    timestamps.sort()
    if (start_time < timestamps[0]):
        # move start time forward to first bucket
        start_time = timestamps[0] - (timestamps[0] % interval)

    ts_idx = 0
    ts_max = len(timestamps)
    bucket_time = start_time
    bucket_end = bucket_time + interval
    while (bucket_end <= end_time):
        # step cacti forward until it's in our current bucket
        while (timestamps[ts_idx] < bucket_time):
            ts_idx += 1
            if ts_idx == ts_max:
                # we are done ... no more cacti data
                return
        # OK, let's set up on istatd bucket
        b = Bucket(time_summary[timestamps[ts_idx]]['value'] * multiplier)
        # and keep putting in cacti buckets until the next cacti bucket is no longer in our istatd bucket
        while ((ts_idx+1) < ts_max and timestamps[ts_idx+1] < bucket_end):
            ts_idx += 1
            b.add(time_summary[timestamps[ts_idx]]['value'] * multiplier)
        # emit the bucket data
        if is_counter:
            # force the number of entries to match the size of the bucket
            b.count_ = interval / SECONDS_PER_ISTATD_BUCKET;
        b.emit(bucket_time, fh)

        bucket_time += interval
        bucket_end = bucket_time + interval

args = sys.argv[1:]
is_counter = False
max_time = NOW
options, files = getopt.gnu_getopt(args, "", ["counter","max-time="])
for o,a in options:
    if o == "--counter":
        is_counter = True
    elif o == "--max-time":
        max_time = int(a)

if (len(files) != 2):
    print "USAGE: python rrdimport.py [--max-time unix epoch time] [--counter] COUNTERMAPFILE RRADIR"
    print "COUNTERMAPFILE should contain lines of 'COUNTERID COUNTERNAME'"
    print "  --counter:  specified if the values being converted should be stored as counters"
    print "              otherwise the values will be assumed to be gauges"
    print "  --max-time: if specified, will cut off import from rra dump file at the specified"
    print "              time.  If left unspecified, all values are imported.  This value is specified"
    print "              as an int in unix epoch format."
    print ""
    sys.exit(1)

(counter_map_file, rra_dir) = files

for line in open(counter_map_file, 'r').readlines():
    (counter_id, counter_name) = line.rstrip().split(' ', 2)
    bucket_file = ISTATD_STORE_ROOT + "/" + counter_name.replace(".","/")

    dom = xml.dom.minidom.parse(os.path.join(rra_dir, "value_%s.xml"%counter_id))
    # this is now an array of bucket data 
    rra_data = rra_time_summary(rra_finder(dom, []))

    for i in range(3):
        istatd_file = "%s/%s" % (bucket_file, INTERVALS[i])
        bucket_size =  BUCKETS[i] * SECONDS_PER_ISTATD_BUCKET
        if (os.path.exists(istatd_file)):
            print "Emitting istatd file for", istatd_file, "at resolution", bucket_size
            p = subprocess.Popen(["/home/cit/git/istatd/bin/istatd_import",
                                  "--stat-file",
                                  istatd_file
                                  ],
                                 stdin=subprocess.PIPE
                                 )
            emit_istatd_buckets(p.stdin, rra_data, bucket_size, CUTOFF[i], max_time)
            # emit_istatd_buckets(sys.stdout, rra_data, bucket_size, CUTOFF[i], NOW)
            p.stdin.close()
        else:
            print "Skipping istatd file for", istatd_file, "at resolution", bucket_size, "(does not exist)"

