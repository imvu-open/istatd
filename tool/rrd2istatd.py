import xml.dom.minidom
import sys, os
import getopt
import time

SECONDS_PER_CACTI_AGGREGATION_LEVEL = 300.0
SECONDS_PER_ISTATD_BUCKET = 10.0
RETENTION_10S = 11.0 * 60 * 60 * 24  # send 11 days of 10s second data
CUTOFF_10S = time.time() - RETENTION_10S

# TODO
#  * if counter, need to convert to "counter based" sampling
# retention: 10s:10d,5m:1y9d,1h:6y12d
#  

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


#collapse data into a timestamped list of (aggregation_level, value) pairs.
#uses min aggregation level to resolve overlap
def rra_time_summary(rra_list):
    all_data = {}
    global is_counter
    for rra in rra_list:
        for (timestamp, value) in rra['data']:
            data = all_data.get(timestamp, {})
            if 'aggregation_level' not in data or data['aggregation_level'] > rra['aggregation_level']:
                if is_counter:
                    multiplier = SECONDS_PER_CACTI_AGGREGATION_LEVEL
                else:
                    multiplier = 1.0
                all_data[timestamp] = {
                    'aggregation_level':int(rra['aggregation_level']), 
                    'value':float(value)*multiplier
                }

    return all_data


#expands (aggregation_level,value) pairs into even 5m chunks
def rra_time_summary_expander(time_summary):
    output = {}
    global is_counter
    for (timestamp, datapoint) in time_summary.iteritems():
        scale = datapoint['aggregation_level']
        for i in range(scale):
            bucket_time = timestamp+i*SECONDS_PER_CACTI_AGGREGATION_LEVEL
            if bucket_time < CUTOFF_10S:
                output[bucket_time] = datapoint['value']
            else: # need to split cacti sample into istatd buckets
                value = datapoint['value']
                if is_counter:
                    value = value / (SECONDS_PER_CACTI_AGGREGATION_LEVEL / SECONDS_PER_ISTATD_BUCKET)
                now = bucket_time
                later = bucket_time + SECONDS_PER_CACTI_AGGREGATION_LEVEL
                while now < later:
                    output[now] = value
                    now += SECONDS_PER_ISTATD_BUCKET

    return output


args = sys.argv[1:]
prefix = ""
is_counter = False
options, files = getopt.gnu_getopt(args, "", ["counter",])
for o,a in options:
    if o == "--counter":
        is_counter = True
        prefix = "*"

if (len(files) != 2):
    print "USAGE: python rrd2istatd.py [--counter] COUNTERMAPFILE RRADIR"
    print "COUNTERMAPFILE should contain lines of 'COUNTERID COUNTERNAME'"
    print "  specify --counter if the values being converted should be stored as counters"
    print "  otherwise the values will be assumed to be gauges"
    print ""
    sys.exit(1)

(counter_map_file, rra_dir) = files

for line in open(counter_map_file, 'r').readlines():
    (counter_id, counter_name) = line.rstrip().split(' ', 2)

    dom = xml.dom.minidom.parse(os.path.join(rra_dir, "value_%s.xml"%counter_id))
    summary = rra_time_summary_expander(rra_time_summary(rra_finder(dom, [])))
    timestamps = summary.keys()
    timestamps.sort()

    for timestamp in timestamps:
        print "%s%-15s %10i %15.5f" % (prefix, counter_name, timestamp, summary[timestamp])
