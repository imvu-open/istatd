import sys
import urllib
import urlparse
import json

def fetch_counter(istatd_host, dotted_counter_name, start_time=0, end_time=0):
    params = urllib.urlencode({"start" : start_time, "end" : end_time})
    url = urlparse.urljoin('http://' + istatd_host, '/%s?%s' % (dotted_counter_name, params))
    return json.load(urllib.urlopen(url))

def __main(argv):
    from optparse import OptionParser
    parser = OptionParser()
    parser.add_option("-H", "--host", dest="host", help="host:port of istatd")
    parser.add_option("-c", "--counter", dest="counter_name", help="name of counter to measure")
    parser.add_option("-s", "--start-time", dest="start_time", default=0, help="start timestamp")
    parser.add_option("-e", "--end-time", dest="end_time", default=0, help="end timestamp")

    (options, args) = parser.parse_args(argv)
    if not options.host:
        parser.error("--host required")

    if not options.counter_name:
        parser.error("--counter required")

    if options.start_time < 0:
        parser.error("invalid start time %s" % options.start_time)

    if options.end_time < 0:
        parser.error("invalid end time %s" % options.end_time)

    import pprint
    pprint.pprint(fetch_counter(options.host, options.counter_name, options.start_time, options.end_time))
    return 0

if __name__ == "__main__":
    sys.exit(__main(sys.argv))
