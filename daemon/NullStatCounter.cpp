#include "Logs.h"
#include "NullStatCounter.h"
#include "Debug.h"

NullStatCounter::NullStatCounter(const std::string& name):
	name_(name)
{
    LogDebug << "NullStatCounter::NullStatCounter(" << name << ")";
}

NullStatCounter::~NullStatCounter()
{
}

bool NullStatCounter::isCollated() const
{
    return false;
}

void NullStatCounter::record(time_t time,
							 double value,
							 double valueSq, 
							 double min, 
							 double max, 
							 size_t cnt)
{
	LogSpam << "NullStatCounter::record(...)";
}

void NullStatCounter::flush(boost::shared_ptr<IStatStore> const &store)
{
    LogSpam << "NullStatCounter::flush(...)";
}

void NullStatCounter::forceFlush(boost::shared_ptr<IStatStore> const &store)
{
	LogSpam << "NullStatCounter::forceFlush(...)";
}

void NullStatCounter::maybeShiftCollated(time_t t)
{
    LogSpam << "NullStatCounter::maybeShiftCollated(" << t << ")";
}

void NullStatCounter::select(time_t start_time, 
							 time_t end_time, 
							 std::vector<istat::Bucket> &oBuckets, 
							 time_t &normalized_start, 
							 time_t &normalized_end, 
							 time_t &interval, 
							 size_t max_samples)
{
	LogSpam << "NullStatCounter::select(...)";
}


