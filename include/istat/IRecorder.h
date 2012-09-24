
#if !defined(istat_IRecorder_h)
#define istat_IRecorder_h

#include <istat/StatFile.h>

namespace istat
{

    class IRecorder
    {
    public:
        virtual void record(int64_t value) = 0;
    };

    class RecordStat : public IRecorder
    {
    public:
        RecordStat() : stat_(0) {}
        virtual void record(int64_t value);
        int64_t stat_;
    };

    class RecordStats : public Stats
    {
    public:
        RecordStats();
        RecordStats(RecordStats const &rs);
        RecordStats &operator=(RecordStats const &rs);
        RecordStat nHits;
        RecordStat nMisses;
    };
}

#endif  //  istat_IRecorder_h

