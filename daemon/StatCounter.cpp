#include <iostream>
#include <algorithm>
#include <istat/istattime.h>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>

#include "istat/strfunc.h"
#include "Logs.h"
#include "StatCounter.h"
#include "IStatStore.h"
#include "Debug.h"

#include <boost/thread/thread.hpp>
#include "Retention.h"


LoopbackCounter IStatCounter::enqueueRecords_("counter.enqueue", TypeEvent);
LoopbackCounter IStatCounter::dequeueRecords_("counter.dequeue", TypeEvent);
LoopbackCounter IStatCounter::queueLenRecords_("counter.queuelen", TypeCounted);
LoopbackCounter IStatCounter::recordsFromTheFuture_("counter.from_future", TypeEvent);
LoopbackCounter IStatCounter::recordsFromThePast_("counter.from_past", TypeEvent);
LoopbackCounter IStatCounter::eagerconns_("counter.eagerconns", TypeCounted);
LoopbackCounter IStatCounter::recordsRejected_("counter.rejected", TypeEvent);
LoopbackCounter IStatCounter::countersClosed_("counter.closed", TypeEvent);
LoopbackCounter IStatCounter::countersCreated_("counter.created", TypeEvent);
LoopbackCounter IStatCounter::countersFlushed_("counter.flushed", TypeEvent);
DebugOption debugRecord("record");
DebugOption debugRejectedCounters("rejectedCounters");

StatCounter::CollationInfo::CollationInfo()
    : bucket(true),
    writes(0),
    time(0)
{
}

StatCounter::CollationInfo::CollationInfo(time_t t)
    : bucket(istat::Bucket(0, 0, 0, 0, 0, t)),
    writes(0),
    time(t)
{
}

time_t StatCounter::lastFromPastLog_ = 0;
uint32_t StatCounter::fromPastLogsPerSec_ = 0;

StatCounter::StatCounter(std::string const &pathName, bool isCollated, time_t zeroTime, istat::Mmap *mm, RetentionPolicy const &rp) :
    isCollated_(isCollated),
    collationInterval_(1)
{
    LogSpam << "StatCounter::StatCounter(" << pathName << ")";

    OneCounter oc;
    istat::StatFile::Settings set;
    set.zeroTime = zeroTime;
    if(isCollated_)
    {
        set.flags |= istat::FILE_FLAG_IS_COLLATED;
    }

    if(!boost::filesystem::exists(pathName))
    {
        LogNotice << "Creating new StatCounter:" << pathName;
        if(!boost::filesystem::create_directories(pathName))
        {
            throw std::runtime_error("Could not create counter directory: " + pathName);
        }
        ++countersCreated_;
    }

    for(size_t i = 0; i != rp.countIntervals(); ++i)
    {
        RetentionInterval const &ri = rp.getInterval(i);
        set.intervalTime = ri.interval;
        set.numSamples = ri.samples;
        if(set.flags & istat::FILE_FLAG_IS_COUNTER_AGGREGATE)
        {
            set.fixed_count = ri.interval / collationInterval_;
        }
        if (ri.lambda > 0)
        {
            set.flags |= istat::FILE_FLAG_IS_TRAILING;
            set.lambda = ri.lambda;
            set.season = ri.samples * ri.interval;
        }
        else
        {
            set.flags &= ~istat::FILE_FLAG_IS_TRAILING;
            set.lambda = 0;
        }

        oc.suffix = ri.name;
        assert(ri.stats.statHit == &ri.stats.nHits);
        assert(ri.stats.statMiss == &ri.stats.nMisses);
        oc.file = boost::make_shared<istat::StatFile>(pathName + "/" + oc.suffix, ri.stats, set, mm);

        counters_.push_back(oc);
        if(i == 0)
        {
            // Remember the first bucket's interval.
            collationInterval_ = oc.file->settings().intervalTime;

            // when a StatFile is created, it takes on the isCollated_ attribute permanently.
            // ensure that StatCounter's isCollated_ attribute is correct when opening an existing file.
            isCollated_ = oc.file->settings().flags & istat::FILE_FLAG_IS_COLLATED;

            if(isCollated_)
            {
                // Coarser intervals in a stat counter are aggregates.
                set.flags &= ~istat::FILE_FLAG_IS_COLLATED;
                set.flags |= istat::FILE_FLAG_IS_COUNTER_AGGREGATE;
            }
        }
    }
}

StatCounter::StatCounter(boost::shared_ptr<istat::StatFile> file, time_t interval, int history, std::string suffix, bool isCollated)
{
    isCollated_ = isCollated;

    OneCounter oc;
    oc.suffix = suffix;
    oc.file = file;

    counters_.push_back(oc);
}

StatCounter::~StatCounter()
{
    ++countersClosed_;
    if(isCollated_)
    {
        time_t t = istat::istattime(0);
        if(t - collations_[BUCKETS_PER_COLLATION_WINDOW - 1].time > 60 * 5)
        {
            // Must be recording in the past, or there was a fake time
            // but it was recently destructed. Just force shift ahead
            // by the collation length.
            fullyShiftCollated();
        }
        else
        {
            maybeShiftCollated(t + (collationInterval_ * BUCKETS_PER_COLLATION_WINDOW / 2));
        }
    }
}


bool StatCounter::isCollated() const
{
    return isCollated_;
}

void StatCounter::record(time_t time, double value, double valueSq, double min, double max, size_t cnt)
{
    if(debugRecord.enabled())
    {
        LogDebug << "record" << value << valueSq << min << max << cnt << time;
    }
    else
    {
        LogSpam << "StatCounter::record thread_id " << boost::this_thread::get_id();
    }
    ++dequeueRecords_;
    --queueLenRecords_;

    time_t nowTime;
    if(isCollated_)
    {
        if(time > istat::istattime(&nowTime) + collationInterval_)
        {
            ++recordsFromTheFuture_;
            if (debugRejectedCounters)
            {
                LogWarning << "StatCounter::record rejected counter from the future: " << time << " > " << nowTime << ": " <<
                    counters_[0].file->header().name;
            }
            return;
        }
        if(time < collations_[0].time)
        {
            ++recordsFromThePast_;
            if (debugRejectedCounters && shouldLog(time))
            {
                LogWarning << "StatCounter::record rejected counter from the past: " << time
                           << " < " << collations_[0].time
                           << " < " << collations_[1].time
                           << " < " << collations_[2].time
                           << ": " <<  counters_[0].file->header().name;
            }
            return;
        }
    }
    else
    {
        if(time > istat::istattime(&nowTime) + time_t(60))
        {
            if (debugRejectedCounters)
            {
                LogWarning << "StatCounter::record rejected counter from the future: " << time << " > " << nowTime << ": " <<
                    counters_[0].file->header().name;
            }
            ++recordsFromTheFuture_;
            return;
        }
    }

    if(isCollated_)
    {
        time -= time % collationInterval_;

        size_t i = findCollationIndex(time);
        // No matching bucket candidate in time range. Shift ahead!
        if(i == BUCKETS_PER_COLLATION_WINDOW)
        {
            if(collations_[0].time == 0)
            {
                for(size_t i = 0; i != BUCKETS_PER_COLLATION_WINDOW; ++i)
                {
                    collations_[(BUCKETS_PER_COLLATION_WINDOW - 1) - i] = CollationInfo(time - (i * collationInterval_));
                }
            }
            else
            {
               maybeShiftCollated(time);
            }
            i = findCollationIndex(time);
        }

        CollationInfo &collation(collations_[i]);
        istat::Bucket &bucket(collation.bucket);
        bucket.collatedUpdate(value / double(collationInterval_), time);
        ++collation.writes;

        // Only update the statfile on power-of-two updates.
        if((collation.writes & (collation.writes - 1)) == 0)
        {
            counters_.begin()->file->updateBucket(bucket);
        }
    }
    else
    {
        // Don't record zero-sample buckets.
        if(cnt == 0)
        {
            ++recordsRejected_;
            if (debugRejectedCounters)
            {
                LogWarning << "StatCounter::record rejected counter with 0 count: " <<
                    counters_[0].file->header().name;
            }
            return;
        }
        double avg = value / double(cnt);
        // Ensure the value ranges are sensible, or reject them.
        // We allow a small amount of epsilon (0.01%) here before rejecting counters due to double vs. float
        // conversion in Buckets transferred from istatd agents to the master
        if (min > (max + fabs(max) * 0.0001) || (avg + fabs(avg) * 0.0001) < min || avg > (max + fabs(max) * 0.0001))
        {
            if (debugRejectedCounters)
            {
                LogWarning << "StatCounter::record rejected counter with bad min/avg/max: " <<
                    counters_[0].file->header().name << min << avg << "(" << value << "/" << cnt << ")" << max;
            }
            ++recordsRejected_;
            return;
        }

        istat::Bucket b(value, float(valueSq), float(min), float(max), int(cnt), time);
        for(std::vector<OneCounter>::iterator
            ptr(counters_.begin()),
            end(counters_.end());
            ptr != end;
            ++ptr)
        {
            ptr->file->updateBucket(b);
        }
    }
}

bool StatCounter::shouldLog(const time_t& t)
{
    grab aholdof(mutex_);
    if (t == lastFromPastLog_)
    {
        if (fromPastLogsPerSec_ < MAX_FROM_PAST_LOG_PER_SEC) {
            ++fromPastLogsPerSec_;
            return true;
        }
    }
    else if (t > lastFromPastLog_)
    {
        fromPastLogsPerSec_ = 1;
        lastFromPastLog_ = t;
        return true;
    }
    return false;
}

size_t StatCounter::findCollationIndex(time_t t)
{
    size_t i;
    for(i = 0; i != BUCKETS_PER_COLLATION_WINDOW; ++i)
    {
        if(collations_[i].time <= t
            && t < collations_[i].time + collationInterval_)
        {
            break;
        }
    }
    return i;
}

void StatCounter::maybeShiftCollated(time_t t)
{
    if(!isCollated_)
    {
        return;
    }

    LogSpam << "StatCounter::maybeShiftCollated(" << t << ")";
    while(t >= collations_[BUCKETS_PER_COLLATION_WINDOW - 1].time + collationInterval_)
    {
        if(!shiftCollated(t))
        {
            break;
        }
    }
}

bool StatCounter::shiftCollated(time_t t)
{
    if(collations_[0].time == 0)
    {
        // Nothing to flush yet!
        return false;
    }
    else
    {
        if (debugRecord.enabled())
        {
            LogDebug << "shiftCollated (time " << collations_[0].time << " shifted out)";
        }

        // Write collated buckets to the statfile.
        for(size_t i = 0; i != BUCKETS_PER_COLLATION_WINDOW; ++i)
        {
            istat::Bucket &bucket(collations_[i].bucket);
            counters_.begin()->file->updateBucket(bucket);
        }
        // Aggregate oldest bucket into coarser statfiles.
        for(std::vector<OneCounter>::iterator
            ptr(counters_.begin()),
            end(counters_.end());
            ptr != end;
            ++ptr)
        {
            if(ptr != counters_.begin())
            {
                ptr->file->updateBucket(collations_[0].bucket);
            }
        }
        // Shift collations by one interval.
        for(size_t i = 0; i != BUCKETS_PER_COLLATION_WINDOW - 1; ++i)
        {
            collations_[i] = collations_[i + 1];
        }
        // Start a fresh collation entry on top.
        CollationInfo &top(collations_[BUCKETS_PER_COLLATION_WINDOW - 1]);
        CollationInfo &oldTop(collations_[BUCKETS_PER_COLLATION_WINDOW - 2]);
        top = CollationInfo(oldTop.time + collationInterval_);
        return true;
    }
}

void StatCounter::fullyShiftCollated()
{
    maybeShiftCollated(collations_[BUCKETS_PER_COLLATION_WINDOW - 1].time + collationInterval_ * BUCKETS_PER_COLLATION_WINDOW);
}

void StatCounter::purge(std::string rootPath)
{
    std::string ext = ".bak";
    std::string backupDir = rootPath + ext;
    if(!boost::filesystem::exists(backupDir))
    {
        LogNotice << "Creating new backup directory:" << backupDir;
    }

    boost::filesystem::path parentPath = boost::filesystem::path(counters_[0].file->getPath()).parent_path();
    std::string backupTargetPath = parentPath.string();
    boost::algorithm::replace_all(backupTargetPath, rootPath, backupDir);

    if(!boost::filesystem::create_directories(backupTargetPath) && !boost::filesystem::exists(backupTargetPath))
    {
        throw std::runtime_error("Could not create backup counter directory: " + backupTargetPath);
    }
    else
    {
        LogNotice << "We created the backup target root path:" << backupTargetPath;
    }

    for(std::vector<OneCounter>::iterator
        ptr(counters_.begin()),
        end(counters_.end());
        ptr != end;
        ++ptr)
    {
        ptr->file->flush();
        ++countersFlushed_;

        std::string counterBackupPath = ptr->file->getPath();
        boost::algorithm::replace_all(counterBackupPath, rootPath, backupDir);


        boost::system::error_code ec;
        boost::filesystem::create_hard_link(boost::filesystem::path(ptr->file->getPath()), boost::filesystem::path(counterBackupPath), ec);
        if (!boost::filesystem::exists(counterBackupPath))
        {
            throw std::runtime_error("We failed to create the backup counter hard link: " + counterBackupPath + " : " + ec.message());
        }
        else
        {
            LogNotice << "We created the backup counter hard link:" << counterBackupPath;
            std::pair<bool, std::string> op_status = wrap_remove(ptr->file->getPath());
            if (boost::filesystem::exists(ptr->file->getPath()))
            {
                throw std::runtime_error("We failed to delete the old counter file: " + ptr->file->getPath() + " : Reason: " + op_status.second);
            }
            else
            {
                LogNotice << "We have removed the old counter file " << ptr->file->getPath();
            }
        }
    }
    counters_.clear();

    std::pair<bool, std::string> op_status = wrap_remove(parentPath);

    if(boost::filesystem::exists(parentPath))
    {
        LogWarning << "Parent directory still exists at " << parentPath.string() <<  ":" << op_status.second;
    }

}
//Newer versions of boost (>1.40) have a throwless filesystem api that takes an optional erorr struct to populate
//with any errors. We are sorty of mirroring that here because we have to support this old version of boost.
std::pair<bool, std::string> StatCounter::wrap_remove(const boost::filesystem::path& p)
{
    try
    {
        boost::filesystem::remove(p);
    }
    catch (const boost::filesystem::filesystem_error &e)
    {
       return std::pair<bool, std::string>(false, e.what());
    }

    return std::pair<bool, std::string>(true, "");
}

std::pair<bool, std::string> StatCounter::wrap_copy_file(const boost::filesystem::path& from, const boost::filesystem::path& to)
{
    try
    {
        boost::filesystem::copy_file(from, to);
    }
    catch (const boost::filesystem::filesystem_error &e)
    {
       return std::pair<bool, std::string>(false, e.what());
    }

    return std::pair<bool, std::string>(true, "");
}

void StatCounter::flush(boost::shared_ptr<IStatStore> const &store)
{
    LogSpam << "StatCounter::flush()";
    if(isCollated_)
    {
        maybeShiftCollated(istat::istattime(0) + (collationInterval_ * (BUCKETS_PER_COLLATION_WINDOW / 2)));
    }
    for(std::vector<OneCounter>::iterator
        ptr(counters_.begin()),
        end(counters_.end());
        ptr != end;
        ++ptr)
    {
        ptr->file->flush();
        store->iterateSignal(ptr->file->header());
        ++countersFlushed_;
    }
}

void StatCounter::forceFlush(boost::shared_ptr<IStatStore> const &store)
{
    if(isCollated_)
    {
        fullyShiftCollated();
    }
    flush(store);
}


boost::shared_ptr<istat::StatFile> StatCounter::pickStatFile(time_t startTime, time_t endTime, time_t &interval)
{
    // validate requested time range is sane
    if (endTime < startTime) {
        return boost::shared_ptr<istat::StatFile>((istat::StatFile*)0);
    }

    time_t now = istat::istattime(0);

    // if endTime is in the future, we can't have data after than now time
    if (endTime > now) {
        endTime = now;
    }

    // it is assumed that elements for the vector are stat files sorted in
    // FINEST to COARSEST resolution order and that each files has
    // contemporaneous updates in their most recently updated buckets
    for (std::vector<StatCounter::OneCounter>::iterator ptr(counters_.begin()), end(counters_.end()); ptr != end; ++ptr)
    {
        boost::shared_ptr<istat::StatFile> sf = (*ptr).file;
        int64_t startIndex = sf->mapTimeToBucketIndex(startTime);
        interval = sf->settings().intervalTime;
        if (sf->isBucketIndexInFile(startIndex)) {
            return sf;
        }
    }

    // if we are here, we are either to far in the future or too far
    // in the past for the coarsest resolution of data we have.
    // if in the future, return the finest interval time.
    if (startTime > counters_[0].file->lastBucketTime())
    {
        interval = counters_[0].file->settings().intervalTime;
    }

    return boost::shared_ptr<istat::StatFile>((istat::StatFile*)0);

}

//  You can ask for a very long season, which won't be present. This
//  function will then return information about the longest season present.
//  If asking for a season length of 0, then you get the shortest season.
boost::shared_ptr<istat::StatFile> StatCounter::pickTrailingStatFile(time_t season, time_t &o_interval, time_t &o_season)
{
    o_interval = 0;
    o_season = 0;

    // it is assumed that elements for the vector are stat files sorted in
    // FINEST to COARSEST resolution order and that each files has
    // contemporaneous updates in their most recently updated buckets
    for (std::vector<StatCounter::OneCounter>::iterator ptr(counters_.begin()), end(counters_.end()); ptr != end; ++ptr)
    {
        boost::shared_ptr<istat::StatFile> sf = (*ptr).file;
        if (!(sf->header().flags & istat::FILE_FLAG_IS_TRAILING))
        {
            continue;
        }
        //  return some information about found files
        o_season = sf->header().season;
        o_interval = sf->header().cfg_interval;
        //  season is not big enough
        if (sf->header().season < (uint64_t)season)
        {
            continue;
        }
        return sf;
    }

    return boost::shared_ptr<istat::StatFile>((istat::StatFile*)0);
}

//  Interval starts out as the native interval (bucket size) of the file,
//  and can be modified to the desired resolution to hit the number of
//  samples desired.
void StatCounter::normalizeRange(time_t &start, time_t &end, time_t &interval, size_t maxSamples)
{
    if (maxSamples == 1)
    {
        //  do a single bucket!
        interval = end - start;
        start = start - (start % interval);
        time_t endOverrun = (end % interval);
        end = end - endOverrun + (endOverrun ? interval : 0);
        //  make sure there's only one bucket! It may be off by 1/2 the interval, but that's OK
        interval = end - start;
        return;
    }

    //C++ Standard 2003 says in 5.6/4 that [...] If the second operand of / or % is zero the behavior is undefined; [...]
    if (interval == 0)
    {
        LogDebug << "0 Interval passed. Massaging it to 10 our minimum reduction";
        interval = 10;
    }
    // initial normalization
    start = start - (start % interval);

    time_t endOverrun = (end % interval);
    end = end - endOverrun + (endOverrun ? interval : 0);

    assert(end >= start);
    if (!maxSamples || (size_t)((end - start)/interval) <= maxSamples)
    {
        return;
    }

    static time_t reductions[] =
    {
        10, 20, 30, 60, 120, 300, 600, 900, 1200, 1800,
        3600, 2*3600, 3*3600, 4*3600, 6*3600, 8*3600, 12*3600,
        86400   /* refuse to reduce to greater-than-day granularity */
    };

    time_t targetInterval = 0;
    for (size_t i = 0; i != sizeof(reductions) / sizeof(time_t); ++i)
    {
        if (reductions[i] <= interval)
        {
            continue;
        }
        if (reductions[i] % interval)
        {
            continue;
        }

        targetInterval = reductions[i];
        if ((end - start) / targetInterval <= time_t(maxSamples))
        {
            break;
        }
    }

    if (!targetInterval)
    {
        LogWarning << "cannot reduce evenly samples of interval " << interval
                   << " in range " << start
                   << " to " << end
                   << " to be less than max sample count " << maxSamples;
        targetInterval = (end - start) / maxSamples;
        if (targetInterval < interval * 2)
        {
            return;
        }
    }

    interval = targetInterval;

    // final normalization
    start = start - (start % interval);

    endOverrun = (end % interval);
    end = end - endOverrun + (endOverrun ? interval : 0);
}

namespace
{
    struct BucketRemovalPredicate
    {
        BucketRemovalPredicate(time_t start, time_t end)
            : start_(start), end_(end)
        {
        }

        bool operator() (istat::Bucket const &bucket)
        {
            return bucket.time() < start_ || bucket.time() > end_;
        }

        time_t start_, end_;
    };
}

void StatCounter::readBucketsFromFile(boost::shared_ptr<istat::StatFile> sf, time_t start_time, time_t end_time, std::vector<istat::Bucket> &oBuckets)
{
    int64_t start_index = sf->mapTimeToBucketIndex(start_time, /* round up */ false);
    int count = sf->numBucketsBetween(start_time, end_time, /* round up */ false);

    if (count > 0)
    {
    	oBuckets.resize(count);
    	int64_t n = sf->readBuckets(&oBuckets[0], count, start_index);
    	oBuckets.resize(n);
    	time_t interval = sf->settings().intervalTime;

        if (isCollated_)
        {
            //  transform un-counted buckets into counted buckets saying 0 value at 1 count
            time_t firstTime = start_time;
            BOOST_FOREACH(istat::Bucket &b, oBuckets)
            {
                if (b.time() >= start_time && b.time() < end_time)
                {
                    //  this is an actual sample
                    firstTime = b.time() + interval;
                }
                else
                {
                    b = istat::Bucket(0, 0, 0, 0, 1, firstTime);
                    firstTime += interval;
                }
            }
        }
        else
        {
            // Sanitize the data returned, to only include buckets actually in the requested timeframe.
            oBuckets.erase(std::remove_if(oBuckets.begin(), oBuckets.end(),
                BucketRemovalPredicate(start_time, end_time)), oBuckets.end());
        }
    }
}

namespace
{
    inline void appendReduced(std::vector<istat::Bucket> &result, istat::Bucket &reduction, time_t currentTime, int collatedBucketCount)
    {
        if(collatedBucketCount)
        {
            // Force min to be 0 if any buckets were skipped.
            if(reduction.count() < collatedBucketCount)
            {
                reduction.update(istat::Bucket(0, 0, 0, 0, 0, currentTime));
            }
            reduction.setCount(collatedBucketCount);
        }
        result.push_back(reduction);
    }
}

void StatCounter::reduce(std::vector<istat::Bucket> &buckets, time_t startTime, time_t endTime, time_t reducedInterval, int collatedBucketCount)
{
    istat::Bucket zero(true);
    std::vector<istat::Bucket> result;
    time_t currentTime = startTime;
    time_t nextTime = currentTime + reducedInterval;
    istat::Bucket reduction(true);

    for (size_t i = 0, n = buckets.size(); i != n; ++i)
    {
        istat::Bucket &bucket(buckets[i]);
        if (bucket.time() >= nextTime)
        {
            appendReduced(result, reduction, currentTime, collatedBucketCount);

            reduction = istat::Bucket(true);
            //  death detection counter -- paranoia if bTime == MAX_INT or something!
            size_t ddn = 10000000;
            while (bucket.time() >= nextTime && ddn > 0)
            {
                --ddn;
                currentTime = nextTime;
                nextTime += reducedInterval;
            }
        }
        if (reduction.count() == 0)
        {
            reduction = istat::Bucket(bucket, currentTime);
        }
        else
        {
            reduction.update(bucket);
        }
    }
    if (reduction.time() > 0)
    {
        appendReduced(result, reduction, currentTime, collatedBucketCount);
    }
    result.swap(buckets);
}

void StatCounter::select(time_t start_time, time_t end_time, bool trailing, std::vector<istat::Bucket> &oBuckets, time_t &normalized_start, time_t &normalized_end, time_t &interval, size_t max_samples)
{
    // Supply reasonable defaults if either start or end are zero.
    if (!end_time && !start_time) {
        end_time = istat::istattime(0);
        start_time = end_time - 900;
    }
    else if (!end_time)
    {
        end_time = start_time + 900;
    }
    else if (!start_time) {
        start_time = end_time - 900;
    }

    // Validate requested time range is sane.
    if (end_time < start_time)
    {
        return;
    }

    normalized_start = start_time;
    normalized_end   = end_time;

    boost::shared_ptr<istat::StatFile> sf = trailing ?
        pickTrailingStatFile(istat::istattime(0) - start_time, interval, end_time) :
        pickStatFile(start_time, end_time, interval);

    normalizeRange(normalized_start, normalized_end, interval, max_samples);

    if (!sf) {
        LogDebug << "No data found in range " << normalized_start << " to " << normalized_end;
        return;
    }

    readBucketsFromFile(sf, normalized_start, normalized_end, oBuckets);

    if (sf->settings().intervalTime < interval) {
        LogDebug << "reducing from interval" << sf->settings().intervalTime << "to" << interval;

        // Force collated counters to have the exact number of collationInterval_ samples,
        int collatedBucketCount = 0;
        if(isCollated_)
        {
            int multiplier = 1;
            istat::StatFile::Settings const &set(sf->settings());
            if(set.flags & istat::FILE_FLAG_IS_COUNTER_AGGREGATE)
            {
                multiplier = set.fixed_count;
            }
            collatedBucketCount = interval / set.intervalTime * multiplier;
        }

        reduce(oBuckets, normalized_start, normalized_end, interval, collatedBucketCount);
    }
}

std::string StatCounter::getSampleStatFilePath() {
    if (counters_.size() > 0) {
        return counters_[0].file->header().name;
    }
    return "Undefined";
}
