
#if !defined(daemon_IStatCounter_h)
#define daemon_IStatCounter_h


#include <ctime>
#include <vector>
#include <istat/Bucket.h>
#include "LoopbackCounter.h"


class IStatCounter
{
public:
    static LoopbackCounter enqueueRecords_;
    static LoopbackCounter dequeueRecords_;
    static LoopbackCounter queueLenRecords_;
    static LoopbackCounter recordsFromTheFuture_;
    static LoopbackCounter recordsFromThePast_;
    static LoopbackCounter eagerconns_;
    static LoopbackCounter recordsRejected_;
    static LoopbackCounter countersClosed_;
    static LoopbackCounter countersCreated_;
    static LoopbackCounter countersFlushed_;

    virtual ~IStatCounter() {};

    virtual bool isCollated() const = 0;
    virtual void record(time_t time, double value, double valueSq, double min, double max, size_t cnt) = 0;
    virtual void flush(boost::shared_ptr<IStatStore> const &store) = 0;
    virtual void forceFlush(boost::shared_ptr<IStatStore> const &store) = 0;
    virtual void maybeShiftCollated(time_t t) = 0;
    virtual void select(time_t start, time_t end, bool trailing, std::vector<istat::Bucket> &oBuckets, time_t &normalized_start, time_t &normalized_end, time_t &interval, size_t max_samples) = 0;

    virtual void purge(std::string rootPath) = 0;

};

#endif  //  daemon_IStatCounter_h
