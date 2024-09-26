
#if !defined(daemon_StatCounter_h)
#define daemon_StatCounter_h

#include "IStatCounter.h"
#include <istat/StatFile.h>
#include "threadfunc.h"

#include <string>
#include <vector>

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio/strand.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>

class RetentionPolicy;

class StatCounter : public IStatCounter, public boost::noncopyable
{
public:
    StatCounter(std::string const &name, bool isCollated, time_t zeroTime, istat::Mmap *mm, RetentionPolicy const &rp);
    StatCounter(boost::shared_ptr<istat::StatFile> file, time_t interval, int history, std::string suffix, bool isCollated);
    virtual ~StatCounter();

    virtual bool isCollated() const;
    virtual void record(time_t time, double value, double valueSq, double min, double max, size_t cnt);
    virtual void flush(boost::shared_ptr<IStatStore> const &store);
    virtual void forceFlush(boost::shared_ptr<IStatStore> const &store);
    virtual void maybeShiftCollated(time_t t);
    virtual void select(time_t start, time_t end, bool trailing, std::vector<istat::Bucket> &oBuckets, 
                        time_t &normalized_start, time_t &normalized_end, time_t &interval, 
                        size_t max_samples);


    virtual void purge(std::string rootPath);

    boost::shared_ptr<istat::StatFile> pickStatFile(time_t startTime, time_t endTime, time_t &interval);
    boost::shared_ptr<istat::StatFile> pickTrailingStatFile(time_t season, time_t &o_interval, time_t &o_season);
    virtual void normalizeRange(time_t &start, time_t &end, time_t &interval, size_t maxSamples);
    
    friend void run_tests(void);

private:
    struct OneCounter
    {
        std::string suffix;
        boost::shared_ptr<istat::StatFile> file;
    };
    std::vector<OneCounter> counters_;

    static size_t const BUCKETS_PER_COLLATION_WINDOW = 5;
    struct CollationInfo
    {
        istat::Bucket bucket;
        uint64_t writes;
        time_t time;

        CollationInfo();
        CollationInfo(time_t t);
    };
    bool isCollated_;   
    time_t collationInterval_;
    CollationInfo collations_[BUCKETS_PER_COLLATION_WINDOW];

    static uint32_t const MAX_FROM_PAST_LOG_PER_SEC = 50;
    static time_t lastFromPastLog_;
    static uint32_t fromPastLogsPerSec_;
    lock mutex_;
    bool shouldLog(const time_t& t);

    size_t findCollationIndex(time_t t);
    void readBucketsFromFile(boost::shared_ptr<istat::StatFile> sf, time_t start_time, time_t end_time, std::vector<istat::Bucket> &oBuckets);
    void reduce(std::vector<istat::Bucket> &buckets, time_t startTime, time_t endTime, time_t interval, int collatedBucketSize);
    bool shiftCollated(time_t t);
    void fullyShiftCollated();
    std::pair<bool, std::string> wrap_remove(const boost::filesystem::path& p);
    std::pair<bool, std::string> wrap_copy_file(const boost::filesystem::path& from, const boost::filesystem::path& to);
    std::string getSampleStatFilePath();
};

#endif  //  daemon_StatCounter_h
