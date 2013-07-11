
#include "Retention.h"
#include "Logs.h"
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
    LogDebug << "RetentionPolicy::addInterval()" << ri.interval << ri.samples << ri.name << ri.lambda;
    if (!intervals.empty())
    {
        if ((ri.lambda == 0) && (intervals.back().interval >= ri.interval))
        {
            throw std::runtime_error("Each retention interval must be longer than the previous.");
        }
        if ((ri.lambda == 0) && (intervals.back().lambda > 0))
        {
            throw std::runtime_error("Season retention intervals must be specified after regular intervals.");
        }
    }
    intervals.push_back(ri);
}

void RetentionPolicy::addInterval(std::string const &str)
{
    std::string interval;
    std::string retention;
    std::string name;
    std::string lambda;
    int n = istat::splitn(str, ':', interval, retention, name, lambda);
    if (n < 2)
    {
        throw std::runtime_error("must specify at least interval:retention: " + str);
    }
    if (name.empty())
    {
        name = interval;
    }
    RetentionInterval ri;
    ri.interval = istat::interval_to_seconds(interval);
    if (!ri.interval)
    {
        throw std::runtime_error("interval cannot be 0: " + interval);
    }
    ri.samples = istat::interval_to_seconds(retention);
    ri.samples = ri.samples / ri.interval;
    if (ri.samples == 0)
    {
        throw std::runtime_error("interval samples cannot be 0, based on retention: " + retention);
    }
    if (lambda.length())
    {
        char *end;
        ri.lambda = strtod(lambda.c_str(), &end);
        if (ri.lambda <= 0 || ri.lambda >= 1)
        {
            throw std::runtime_error("lambda must be > 0 and < 1 if specified: '" + lambda + "'");
        }
    }
    else
    {
        ri.lambda = 0;
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


