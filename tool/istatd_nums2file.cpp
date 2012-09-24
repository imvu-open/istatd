#include "../daemon/Argument.h"

#include <istat/StatFile.h>
#include <istat/Bucket.h>
#include <istat/istattime.h>
#include <istat/Mmap.h>

#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cstring>

using namespace istat;

Argument<bool> timeInFile("time-in-file", false, "read time from input");
Argument<bool> asCounter("counter", false, "create file as counter");
Argument<std::string> statFilename("stat-file", "", "name of stat file to update");
Argument<int> interval("interval", 10, "seconds per bucket");
Argument<int> zeroTime("zero-time", 0, "time for first bucket in file. if 0, current time is used.");
Argument<int> numSamples("num-samples", 1000, "number of samples in the file");

void usage()
{
    for (ArgumentBase::iterator ptr(ArgumentBase::begin()), end(ArgumentBase::end());
        ptr != end; ++ptr)
    {
        std::cerr << std::left << "--" << std::setw(12) << (*ptr).first << std::setw(0) << " (" <<
            std::setw(12) << (*ptr).second->str() << std::setw(0) << ")   " << (*ptr).second->help() << std::endl;
    }
}

Mmap *mm_ = NewMmap();

int main(int argc, char const *argv[])
{
    int ix = 1;
    while (ArgumentBase::parseArg(argc, argv, ix))
    {
        // keep going
    }

    bool readTime = timeInFile.get();
    std::string fn = statFilename.get().c_str();

    if (fn == "") {
        std::cerr << "Need to specify stat file to update" << std::endl;
        usage();
        exit(1);
    }

    StatFile::Settings sett;
    sett.zeroTime = zeroTime.get();
    sett.intervalTime = interval.get();
    sett.numSamples = numSamples.get();
    try
    {
        StatFile sf(fn, Stats(), sett, mm_, asCounter.get());
        while (true)
        {
            time_t t;
            if (readTime)
            {
                std::cin >> t;
            }
            float i;
            std::cin >> i;
            if (!readTime)
            {
                istat::istattime(&t);
            }
            if (std::cin.eof()) {
                break;
            }
            Bucket b(i, i*i, i, i, 1, t);
            sf.updateBucket(b);
        }
        return 0;
    }
    catch (std::exception const &x)
    {
        std::cerr << "Exception: " << x.what() << "; for " << fn << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown exception in main(); for " << fn << std::endl;
    }

   return 1;
}

