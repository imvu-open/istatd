
#include <istat/IRecorder.h>
#include <istat/Atomic.h>

using namespace istat;

void RecordStat::record(int64_t value)
{
    atomic_add(&stat_, value);
}

RecordStats::RecordStats()
{
    statHit = &nHits;
    statMiss = &nMisses;
}

RecordStats::RecordStats(RecordStats const &rs)
{
    (Stats &)*this = rs;
    statHit = &nHits;
    statMiss = &nMisses;
}

RecordStats &RecordStats::operator=(RecordStats const &rs)
{
    (Stats &)*this = rs;
    statHit = &nHits;
    statMiss = &nMisses;
    return *this;
}

