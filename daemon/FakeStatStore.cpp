#include <iostream>
#include <algorithm>
#include "FakeStatStore.h"

FakeStatStore::FakeStatStore(boost::asio::io_service &svc) : svc_(svc) {}
FakeStatStore::~FakeStatStore() {}
class FakeStatCounter : public IStatCounter
{
public:
    boost::asio::strand strand_;
    time_t from_, to_, interval_;
    FakeStatStore *fss_;
    std::string name_;

    FakeStatCounter(boost::asio::io_service &svc, time_t from, time_t to, time_t interval, FakeStatStore *fss, std::string const &name)
        : strand_(svc), from_(from), to_(to), interval_(interval), fss_(fss), name_(name)
    {
    }

    virtual bool isCollated() const
    {
        return false;
    }

    virtual void record(time_t time, double value, double valueSq, double min, double max, size_t cnt)
    {
        fss_->recorded_[name_].push_back(istat::Bucket(value, valueSq, min, max, cnt, time));
    }

    virtual void flush(boost::shared_ptr<IStatStore> const &store) {}
    virtual void forceFlush(boost::shared_ptr<IStatStore> const &store) {}
    virtual void maybeShiftCollated(time_t t) {}

    virtual void select(time_t start, time_t end, bool trailing, std::vector<istat::Bucket> &oBuckets, time_t &normalized_start, time_t &normalized_end, time_t &interval, size_t max_samples)
    {
        normalized_start = start;
        normalized_end = end;
        interval = interval_;

        if(end < start || end < from_ || start > to_)
        {
            return;
        }

        start = std::max(from_, start);
        end = std::min(to_, end);

        size_t count = (end - start) / interval;

        oBuckets.clear();
        for(size_t i = 0; i != count; ++i)
        {
            oBuckets.push_back(istat::Bucket(1, 1, 1, 1, 1, i * interval + start));
        }
    }

    void purge(std::string rootPath) {}
};

void FakeStatStore::record(std::string const &ctr, double value)
{
    record(ctr, 0, value, value*value, value, value, 1);
}

void FakeStatStore::record(std::string const &ctr, time_t time, double value)
{
    record(ctr, time, value, value*value, value, value, 1);
}

void FakeStatStore::record(std::string const &name, time_t time, double value, double valueSq, double min, double max, size_t cnt)
{
    if (!fakeCounters_[name])
    {
        fakeCounters_[name] = boost::shared_ptr<IStatCounter>(new FakeStatCounter(svc_, time-1000, time+1000, 10, this, name));
    }
    fakeCounters_[name]->record(time, value, valueSq, min, max, cnt);
}

void FakeStatStore::find(std::string const &ctr, boost::shared_ptr<IStatCounter> &statCounter, boost::asio::strand *&strand) {
    std::map<std::string, boost::shared_ptr<IStatCounter> >::iterator ptr(fakeCounters_.find(ctr));
    if (ptr == fakeCounters_.end()) {
        return;
    }
    statCounter = (*ptr).second;

    FakeStatCounter* fake = static_cast<FakeStatCounter *>(ptr->second.get());
    strand = &fake->strand_;
}


void FakeStatStore::listMatchingCounters(std::string const &pat, std::list<std::pair<std::string, CounterResponse> > &oList) {}

std::string const &FakeStatStore::getLocation() const { return location_; }


void FakeStatStore::manufactureTruth(std::string const &name, time_t from, time_t to, time_t interval)
{
    fakeCounters_[name] = boost::shared_ptr<IStatCounter>(new FakeStatCounter(svc_, from, to, interval, this, name));
}
