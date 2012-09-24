#include "../daemon/Argument.h"

#include <istat/StatFile.h>
#include <istat/Bucket.h>
#include <istat/istattime.h>
#include <istat/Mmap.h>

#include <iomanip>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <iostream>
#include <algorithm>

using namespace istat;

Argument<std::string> statFilename("stat-file", "", "name of existing stat file to merge");
Argument<bool> fillEmpty("fill-empty", false, "Instead of always overwriting data, only overwrite buckets that are empty");

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

    std::string fn = statFilename.get().c_str();
    istat::RawUpdateMode updateMode = fillEmpty.get() ? istat::RAWUP_FILL_EMPTY : istat::RAWUP_OVERWRITE;

    if (fn == "")
    {
        std::cerr << "Need to specify stat file to update" << std::endl;
        usage();
        exit(1);
    }

    try
    {
        std::string line;
        StatFile sf(fn, Stats(), mm_, true);

        while (std::getline(std::cin, line))
        {
            int columns = std::count_if(line.begin(), line.end(), std::ptr_fun<int, int>(std::isspace)) + 1;
            if (columns == 6)
            {
                std::stringstream ss(line);
                double sum;
                float sumSq, min, max;
                int count;
                time_t time;

                ss >> time >> sum >> sumSq >> min >> max >> count;

                Bucket b(sum, sumSq, min, max, count, time);
                sf.rawUpdateBucket(b, updateMode);
            }
            else
            {
                if(!line.empty())
                {
                    std::cerr << "Need exactly 6 arguments, but got " << columns << ":" << std::endl;
                    std::cerr << line << std::endl;
                }
            }
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
