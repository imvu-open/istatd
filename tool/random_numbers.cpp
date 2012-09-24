
#include <istat/istattime.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <boost/cstdint.hpp>
#include <string.h>
#include <time.h>


void usage()
{
    fprintf(stderr, "usage: random_numbers [-t] [count [average]]\n");
    exit(1);
}

int main(int argc, char const *argv[])
{
    bool timestamp = false;

    if (argc > 1 && !strcmp(argv[1], "-t"))
    {
        timestamp = true;
        ++argv;
        --argc;
    }

    int count = 100;
    if (argc > 1)
    {
        count = atoi(argv[1]);
        if (count < 1 || count > 1000000)
        {
            fprintf(stderr, "count out of range\n");
            usage();
        }
        argc--;
        argv++;
    }

    int center = 100;
    int swing = 0;
    if (argc > 1)
    {
        center = atoi(argv[1]);
        if ((center == 0 && !strcmp(argv[0], "0")) || (center < -1000000) || (center > 1000000))
        {
            fprintf(stderr, "center is out of range\n");
            usage();
        }
        argc--;
        argv++;
    }
    if (center >= -100 && center < 100)
    {
        swing = 100;
    }
    else
    {
        swing = abs(center);
    }
    center -= swing/2;

    if (argc > 1)
    {
        fprintf(stderr, "unknown argument: %s\n", argv[1]);
        usage();
    }

    time_t now;
    istat::istattime(&now);

    while (count > 0)
    {
        if (timestamp)
        {
            long long int i64 = (int64_t)now;
            fprintf(stdout, "%lld ", i64);
            while (rand() > RAND_MAX / 2)
            {
                now += 1;
            }
        }
        fprintf(stdout, "%ld\n", (long)((double)rand() * swing / RAND_MAX + center));
        --count;
    }
    return 0;
}

