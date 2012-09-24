
#include "Retention.h"
#include "istat/strfunc.h"
#include <stdexcept>
#include <iostream>

RetentionPolicy::RetentionPolicy()
{
}

RetentionPolicy::RetentionPolicy(RetentionInterval const *ri, size_t cnt)
{
    init(ri, cnt);
}

RetentionPolicy::RetentionPolicy(char const *ivals)
{
    addIntervals(ivals);
}

size_t RetentionPolicy::countIntervals() const
{
    return intervals.size();
}

RetentionInterval const &RetentionPolicy::getInterval(size_t ix) const
{
    if (intervals.size() <= ix)
    {
        throw std::runtime_error("interval out of bounds");
    }
    return intervals[ix];
}

void RetentionPolicy::addInterval(RetentionInterval const &ri)
{
    if (!intervals.empty() && (intervals.back().interval >= ri.interval))
    {
        throw std::runtime_error("Each retention interval must be longer than the previous.");
    }
    intervals.push_back(ri);
}

void RetentionPolicy::addInterval(std::string const &str)
{
    std::string left;
    std::string right;
    if (istat::split(str, ':', left, right) != 2)
    {
        throw std::runtime_error("bad interval argument for retention: " + str);
    }
    std::string name;
    if (istat::split(right, ':', right, name) != 2)
    {
        name = left;
    }
    RetentionInterval ri;
    ri.interval = istat::interval_to_seconds(left);
    if (!ri.interval)
    {
        throw std::runtime_error("interval cannot be 0: " + left);
    }
    ri.samples = istat::interval_to_seconds(right);
    ri.samples = ri.samples / ri.interval;
    if (ri.samples == 0)
    {
        throw std::runtime_error("interval samples cannot be 0: " + right);
    }
    ri.name = name;
    addInterval(ri);
}

void RetentionPolicy::addIntervals(std::string const &ivals)
{
    std::string left, right(ivals);
    while (istat::split(right, ',', left, right) > 0)
    {
        addInterval(left);
    }
}


void RetentionPolicy::init(RetentionInterval const *ptr, size_t cnt)
{
    for (size_t ix = 0; ix != cnt; ++ix)
    {
        addInterval(ptr[ix]);
    }
}


