
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include <boost/lexical_cast.hpp>
#include <iostream>


void usage()
{
    std::cerr << "usage: snooze seconds" << std::endl;
    std::cerr << "seconds may be a floating point number." << std::endl;
    exit(1);
}

int main(int argc, char const *argv[])
{
    try
    {
        if (argc != 2)
        {
            usage();
        }
        double time = boost::lexical_cast<double>(argv[1]);
        if (time <= 0)
        {
            usage();
        }
        struct timespec spec;
        spec.tv_sec = (unsigned long)floor(time);
        spec.tv_nsec = (unsigned long)((time - spec.tv_sec) * 1e9);
        if (nanosleep(&spec, &spec) < 0)
        {
            std::cerr << "nanosleep(): error" << std::endl;
        }
    }
    catch (std::exception const &x)
    {
        std::cerr << "Exception: " << x.what() << std::endl;
        exit(2);
    }
    exit(0);
}

