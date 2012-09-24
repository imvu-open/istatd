#include "istat/Bucket.h"
#include "istat/strfunc.h"
#include "../daemon/StatCounter.h"
#include "../daemon/Argument.h"
#include "../daemon/Retention.h"
#include <istat/Mmap.h>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <boost/filesystem.hpp>

using namespace istat;

Mmap *mm(NewMmap());


Argument<int> intervalSecs("interval-secs", 10, "size of autgenerated bucket stored to the stat files.");
Argument<float> storeValue("store-value", 123.45, "Value to store in the buckets.");
Argument<float> storeIncrement("store-increment", 0.00, "Amount the value increments between buckets.");
Argument<float> storeMax("store-max", 1000.00, "Max value stored in a bucket (after which it is reduced by store-max to below store-max)");
Argument<int> numFiles("num-files", 1000, "Number of counter types to create as statcounter dirs");
Argument<int> numSamples("num-samples", 1000, "Number of samples to store in the stat files");
Argument<std::string> baseName("base-path", std::string("/tmp/istat_filegen"), "path to the data store");

void usage()
{
    for (ArgumentBase::iterator ptr(ArgumentBase::begin()), end(ArgumentBase::end());
        ptr != end; ++ptr)
    {
        std::cerr << std::left << "--" << std::setw(12) << (*ptr).first << std::setw(0) << " (" <<
            std::setw(12) << (*ptr).second->str() << std::setw(0) << ")   " << (*ptr).second->help() << std::endl;
    }
}

// convert a base path "/this/is/my/path" and integer 12345 to
// /this/is/my/path/01/23/45
// depth would be 3 in this case (3 levels of 100 each)
// returns modified path
std::string pathify(std::string s, int depth, int counter) {
    char buf[16];
    
    int divisor = 1;
    int i;
    for (i = 0 ; i < (depth-1) ; i++) {
        divisor = divisor * 100;
    }
    for (int j = 0 ; j <= i; ++j) {
        int chunk = counter / divisor;
        snprintf(buf, 16, "/lev%d_%02d", j, chunk);
        s.append(buf);
        counter = counter % divisor;
        divisor = divisor / 100;
    }
    return s;
}


// basic internal stat generator class 
class StatGenerator
{
public:
    time_t cur_time;          // start date of first sample to generate
    int interval_time;       // interval at which to deliver samples in seconds
    float cur_value;          // starting value
    float delta;
    float max;

    StatGenerator(time_t stime, int itime, float sval, float d, float m) {
        cur_time = stime;
        interval_time = itime;
        cur_value = sval;
        delta = d;
        max = m;
    }

    void updateTime() {
        cur_time += interval_time;
    }

    void updateValue() {
        cur_value += delta;
        while (cur_value > max) {
            cur_value -= max;
        }
    }

    void push_next_value(StatCounter *sc) {
        sc->record(cur_time, cur_value, cur_value * cur_value, cur_value, cur_value, 1);
        updateTime();
        updateValue();
    }
};

int generate_counters(StatCounter *sc, StatGenerator *sg, int count)
{
    for (int i = 0 ; i < count ; ++i) 
    {
        sg->push_next_value(sc);
    }
    return 0;
}

int main(int argc, char const *argv[])
{

    int ix = 1;
    while (ArgumentBase::parseArg(argc, argv, ix))
    {
        // keep going
    }
    if (argv[ix])
    {
        std::cerr << "Command line error near " << argv[ix] << std::endl;
        usage();
    }

    int num_files = numFiles.get();
    std::string basename = baseName.get();
    int interval = intervalSecs.get();
    float store_value = storeValue.get();
    float store_increment = storeIncrement.get();
    float store_max = storeMax.get();


    int depth = 1;
    int power = 100;
    while (power <= num_files) {
        ++depth;
        power = power * 100;
    }

    time_t start_time = time(NULL);

    for (int i = 1 ; i <= num_files ; i++)
    {
        std::string s = pathify(basename, depth, i);
        boost::filesystem::create_directories(s);

        try
        {
            RetentionPolicy rp("10s:10d,5m:150d,1h:6y");
            RetentionPolicy xrp("");
            StatCounter sc(s, true, start_time, mm, rp, xrp); // tiny tiny length for testing
            StatGenerator sg(start_time, interval, store_value, store_increment, store_max);
            generate_counters(&sc, &sg, 1000);
        }
        catch (std::exception const &x)
        {
            std::cerr << "Exception: " << x.what() << "; for " << s << std::endl;
        }
        catch (...)
        {
            std::cerr << "Unknown exception in main(); for " << s << std::endl;
        }
    }
    return 1;
}
