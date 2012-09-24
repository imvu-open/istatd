
#include "Timing.h"

Timing::Timing()
{
    clock_gettime(CLOCK_REALTIME, &stamp_);
}

Timing::~Timing()
{
}

int64_t Timing::elapsedMicros() const
{
    struct timespec tsp;
    clock_gettime(CLOCK_REALTIME, &tsp);
    return ((int64_t)tsp.tv_sec - (int64_t)stamp_.tv_sec) * 1000000LL +
        (tsp.tv_nsec - stamp_.tv_nsec) / 1000;
}

double Timing::elapsedSeconds() const
{
    struct timespec tsp;
    clock_gettime(CLOCK_REALTIME, &tsp);
    return ((double)tsp.tv_sec - (double)stamp_.tv_sec) * 1.0 +
        (tsp.tv_nsec - stamp_.tv_nsec) * 0.000000001;
}

double Timing::operator-(Timing const &t) const
{
    return ((double)stamp_.tv_sec - (double)t.stamp_.tv_sec) * 1.0 +
        (stamp_.tv_nsec - t.stamp_.tv_nsec) * 0.000000001;
}

