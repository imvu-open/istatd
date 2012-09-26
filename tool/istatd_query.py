#!/usr/bin/env python
import sys
import urllib
import urlparse
import json
import pprint

def urlencode_params(start_time, end_time, max_samples):
    params = {}

    if start_time != "null" and start_time != 0:
        params["start"] = start_time

    if end_time != "null" and end_time != 0:
        params["end"] = end_time

    if max_samples != "null" and max_samples != 0:
        params["samples"] = max_samples

    return urllib.urlencode(params)
    
def fetch_counter_via_GET(istatd_host, counter, start_time=0, end_time=0, max_samples=0):
    params = urlencode_params(start_time, end_time, max_samples)

    url = urlparse.urljoin('http://' + istatd_host, '/%s?%s' % (counter, params))
    print 'GET', url

    try:
        result = urllib.urlopen(url)
    except IOError as ioerror:
        return {"error" : ioerror}
    except: 
        return {"error" : "unexpected error: {0}".format(sys.exc_info()[0])}

    return json.load(result)

def fetch_counters_via_POST(istatd_host, counters, start_time=0, end_time=0, max_samples=0):
    request = {}
    request["start"] = 0 if start_time == "null" else int(start_time)
    request["stop"] = 0 if end_time == "null" else int(end_time)
    request["keys"] = counters
    request["maxSamples"] = 0 if max_samples == "null" else int(max_samples)
    request_str = json.dumps(request)
    url = urlparse.urljoin('http://' + istatd_host, '/*')
    print 'POST', url, request_str

    try:
        result = urllib.urlopen(url, request_str)
    except IOError as ioerror:
        return {"error" : ioerror}
    except: 
        return {"error" : "unexpected error: {0}".format(sys.exc_info()[0])}

    return json.load(result)

def __main(argv):
    from optparse import OptionParser
    parser = OptionParser()
    parser.add_option("-H", "--host", dest="host", help="host:port of istatd")
    parser.add_option("-s", "--start-time", dest="start_time", default="null", help="start timestamp")
    parser.add_option("-e", "--end-time", dest="end_time", default="null", help="end timestamp")
    parser.add_option("-n", "--max-samples", dest="max_samples", default="null", help="max number of samples")
    parser.add_option("-r", "--raw", action='store_true', dest="raw", default=False, help="display in 'raw' JSON")
    parser.add_option("-p", "--force-post", action='store_true', dest="force_post", default=False, help="use HTTP POST to get one counter.")

    (options, args) = parser.parse_args(argv)
    if not options.host:
        parser.error("--host required")

    if options.start_time != "null" and options.start_time < 0:
        parser.error("invalid start time %s" % options.start_time)

    if options.end_time != "null" and options.end_time < 0:
        parser.error("invalid end time %s" % options.end_time)

    counters = args[1:]
    if len(counters) == 1 and not options.force_post:
        result = fetch_counter_via_GET(options.host, counters[0], options.start_time, options.end_time, options.max_samples)
    else:
        result = fetch_counters_via_POST(options.host, counters, options.start_time, options.end_time, options.max_samples)

    if options.raw:
        pprint.pprint(result)
    else:
        if "error" in result:
            print "error: {0}".format(result["error"])
        elif "buckets" in result:
            if "start" in result:
                print "start:    {0:10}".format(result["start"])

            if "end" in result:
                print "end:      {0:10}".format(result["end"])
            
            if "interval" in result:
                print "interval: {0:10}".format(result["interval"])

            for b in result["buckets"]:
                d = b["data"]
                if "avg" in d:
                    print "{0:10} {1:.2f} {2:.2f} {3:.2f} {4:d} {5:.2f} {6:.2f}".format(b["time"], d["avg"], d["min"], d["max"], d["count"], d["sum"], d["sumsq"])
                else:
                    print "{0:10} empty bucket".format(b["time"])
        else:
            if "start" in result:
                print "start:    {0:10}".format(result["start"])

            if "stop" in result:
                print "stop:     {0:10}".format(result["stop"])
            
            if "interval" in result:
                print "interval: {0:10}".format(result["interval"])

            for k,v in result.iteritems():
                if k not in ["start", "stop", "interval"]:
                    print "counter:", k
                    if v == 'Not found.':
                        print "  Not found."
                    else:
                        print "  start:    {0:6}".format(v["start"])
                        print "  end:      {0:6}".format(v["end"])
                        print "  interval: {0:6}".format(v["interval"])
                        print "  data:"
                        for d in v["data"]:
                            if "avg" in d:
                                print "{0:10} {1:.2f} {2:.2f} {3:.2f} {4:d} {5:.2f} {6:.2f}".format(d["time"], d["avg"], d["min"], d["max"], d["count"], d["sum"], d["sumsq"])
                            else:
                                print "{0:10} empty bucket".format(b["time"])

    return 0

if __name__ == "__main__":
    sys.exit(__main(sys.argv))
