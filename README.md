istatd
======

Check with jwatte@imvu.com for more information. Released under MIT 
license. All rights reserved. Copyright 2011 IMVU, Inc.

The purpose of istatd is to efficiently collect, store and retrieve 
named statistics from a large number of sources. This is similar to 
Cacti, Graphite, Zabbix, and a bunch of other systems. In fact, istatd 
stated out as a storage back-end for Graphite, to replace the built-in 
carbon back-end. The specific goals of this system are:

- Support 100,000+ distinct counters with 3 different frequencies and 
  retention ages, the shortest frequency being 10 seconds.
- Automatically creating new counter files for all new counters that 
  samples are received for.
- Calculating statistics for each retention bucket -- minimum, maximum, 
  average, standard deviation.

For more documentation than what is found in this file, see:
- https://github.com/imvu-open/istatd/wiki

Quickstart:
-----------

    git clone git@github.com:imvu-open/istatd.git
    cd istatd
    ./quickstart.sh
  
    Then open localhost:18011 in a browser.

You may need to install libboost-all-dev (or whatever the boost headers 
and libraries are called on your system) and libstatgrab-dev for the 
build to succeed.

https://github.com/imvu-open/libstatgrab

Any counter will get a default retention, configured as 10 second data 
for a few days, 5 minute data for a few months, and 1 hour data for a 
few years. All updates go into all of the buckets -- there is no 
decimation, only aggregation (statistics like min, max, average and 
standard deviation are collected/calculated).

This program is implemented in C++ using boost::asio for asynchronous, 
multi-threaded, evented net handling. One version of this program used 
mmap() to do counter I/O asynchronously using madvise() and msync(). 
However, this ended up being a real performance problem, because the 
Linux kernel has one big tree (not hash table) to manage memory 
regions, and this tree is protected using a single lock, serializing 
all calls to mmap() and searching through a very deep tree for each 
call. Trying to start up, loading a few hundred thousand counter files, 
each of which has 3 mmap() calls, ends up choking the CPU and failing 
bad. Thus, the I/O interface is in terms of mmap() operations, but 
the actual implementation uses lseek() and read()/write(). A periodic 
timer iterates over all counters to make sure they are flushed to 
disk about once every 5 minutes. (Check the --flush option)


Some random thoughts
--------------------

-   Stats are received on a TCP port in a simple line-based format, or 
    on a UDP port in the same format.
-   A running istatd can serve up statistics over a simple HTTP 
    interface on a port that you configure.
-   Files in the "files" directory get served from a URL named 
    /?f=filename -- directory paths are not supported!
-   A running istatd can forward all stats it receives to another 
    instance. This allows read slave trees, simple replication, etc.
-   The istatd can collect information about the local machine (network, 
    CPU, memory and disk use) as statistics.
-   There is a simple webapp to allow browsing of the counters. This is 
    in turn served on the HTTP interface, at the root. It uses 
    Dygraph to draw simple graphs, querying for JSON counter data from 
    storage.
-   Retention is configurable only globally (for all new counters created).
-   Flush rate is "at least once every 5 minutes" although buckets are 
    aggregated over 10 seconds with the default settings.
-   There exists a command line tool to dump the counter data in a given 
    individual counter file (from disk) to csv (comma separated values) 
    format.
-   If you lose the machine, the data on the disk is always consistent in 
    the sense that each bucket except possibly the currently active bucket 
    page is in a good state.
-   If you lose the disk, you hopefully had already set up the system with 
    live replication :-)
-   The replication functionality will attempt to re-connect to the target 
    system with exponential back-off, buffering data it receives in the 
    meanwhile. However, if the daemon is then shut down, that buffered data 
    is lost from the point of view of replication (it will still live on the 
    local disk).
-   Some counters will want to aggregate. For example, CPU idle, kernel and 
    user times will add up to 100%, so they could generally be plotted together 
    in a single graph. This is solved in two ways:
    -   For a counter with multiple levels; you can configure global 
        aggregation up the tree for a few levels. So, for example, for a 
        counter with the name cpu.idle.hostname, the sample will be aggregated 
        into both cpu.idle.hostname, and cpu.idle.
    -   For more advanced counter aggregation, you can use the caret format of 
        counter names. The counter cpu.idle^host.a^class.b^type.c will aggregate 
        into counters named cpu.idle.host.a, cpu.idle.class.b and cpu.idle.type.c.
        You typically want to specify these using --localstat, for example.
- Counters that start with "*" are treated as events -- count number of events 
  per second. These are aggregated over the shortest retention interval; longer 
  retention intervals treat the event rates as gauges with a fixed count equal 
  to the number of seconds in the bucket.
- StatFile scales at least 10x better than a SQL database, perhaps much better. 
  For large clusters (IMVU currently has 800 motherboards in production) this 
  constant factor really matters.
- There is a simple key/value interface that supports storing JSON-style keys 
  mapping to strings (only) given different names. This is used to support per-
  user personalization, and storing saved dashboards of counters. That system 
  is not intended to be scalable beyond a few hundred keys per container, and 
  a few hundred containers. Nor is it intended to support a constant churn of 
  key/value updates. If you need that, use Redis!

Build stuff
-----------

Just doing "make" should build on a modern Linux machine with dependencies 
installed. Nothing else is supported right now. If other things become 
supported, they will do so without the use of autotools, for religious reasons.

You will likely want to do something like:
    sudo apt-get install libboost-all-dev

The libstatgrab library is available only through source download. Configure 
it and make + make install (into /usr/local is fine).
    http://github.com/imvu-open/libstatgrab
    
g++ and GNU make are needed, too.

The make file is automatic:

-   Anything in lib/ named .cpp gets included into libistat.
-   Headers for those things should live in include/istat (this will 
    simplify later installation builds, if appropriate). 
-   Anything in daemon named .cpp gets included into the bin/istatd executable.
    Headers just live in daemon as well.
-   Anything in tool/ named .cpp gets built as a separate command-line tool, 
    with the libistat library linked (as well as boost dependencies etc).
-   Anything in test/ named .cpp gets built as a separate command-line program, 
    and executed as part of the make. This ensures that unit tests pass.
-   Unit tests are not as comprehensive as could be, and the daemon is not as 
    factored into testable parts as could be. That being said, it is a lot 
    better than nothing.
-   The build uses one small lib with a "public" API (libistat) and one large 
    lib with everything else except main.cpp. The reason for the second lib is 
    largely to support unit testing.
-   Dependencies (included files, outputs) are automatically tracked, so 
    toucing a .h file will automatically re-build the necessary .cpp files etc.
-   Files in ftest named test_* are run as functional tests at the end of a 
    build. These make use of a common set of functions in ftest/functions and 
    also a common set of configurations in ftest/*.cfg

Because the daemon may want to open privileged files for local stats, bind 
to ports that may be privileged, and update the number of files that may be 
opened at one time (important!), it wants to run as suid root. It will drop 
privileges after starting up. However, as the file gets re-written each time 
it is built, a script named suid.sh is called after linking if it exists and 
is executable. This will attempt to sudo chmod the output file -- prepare to 
enter your password, or abort.

There are scripts to build a dpkg in the "debian" directory. Other packaging 
systems might be accepted as community contributions. Hint, hint ;-)

Again, for more detailed documentation, see the Github wiki!

BUGS
----

-   Istatd currently does not automatically shard across hosts for storage. 
    Once we hit sufficient size, we should write a gateway that works as 
    shard distributor for both incoming stats and queries.
-   We have seen "Going back in time" errors from lib/StatFile.cpp for local 
    stats -- how is that possible? (This was a long time ago -- we may have 
    fixed it, but something to keep an eye on.)
-   There are a nearly unlimited number of features and enhancements we would 
    want to add, such as support for discrete "events" and support for strongly 
    consistent multi-host replication.
-   The UI is functional and looks good, but we could always have more display 
    modes, such as stacked, min/max, etc.

