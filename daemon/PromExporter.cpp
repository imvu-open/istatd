#include "PromExporter.h"
#include "istat/strfunc.h"
#include "Logs.h"
#include "Debug.h"


#include <boost/lexical_cast.hpp>
#include <boost/bind/bind.hpp>
#include <boost/make_shared.hpp>
#include <ctype.h>
#include <iostream>


DebugOption debugPromExporter("promExporter");

PromMetric::PromMetric(std::string &ctr, time_t time, double val) :
    type_(PromTypeUnknown),
    time_(time),
    value_(val),
    name_(ctr)
{
}

PromMetric::PromMetric(std::string &ctr, time_t time, double val, MetricType type) :
    type_(type),
    time_(time),
    value_(val),
    name_(ctr)
{
}

std::string PromMetric::toString()
{
    std::stringstream ss;
    ss << name_ << " " << value_ << " " << (time_ * 1000) << "\n"; 
    return ss.str();
}

PromExporter::PromExporter(boost::asio::io_service &svc, bool enabled) :
    svc_(svc),
    cleanup_interval_(60),
    last_scrape_(0),
    enabled_(enabled)
{
}

PromExporter::~PromExporter()
{
}

void PromExporter::dumpMetrics(std::vector<PromMetric> &res)
{
    std::map<time_t, std::vector<PromMetric> > sending_data;
    {
        grab aholdof(mutex_);
        data_.swap(sending_data);
    }
    std::map<time_t, std::vector<PromMetric> >::iterator pit;

    for (pit = sending_data.begin(); pit != sending_data.end(); ++pit)
    {
        res.insert(res.end(), (*pit).second.begin(), (*pit).second.end());
    }
    sending_data.clear();
}

void PromExporter::storeMetrics(std::string const &name, time_t time, double val)
{
    std::string metric_name(name);
    MetricType type;
    if (name[0] == '*') 
    {
        metric_name = name.substr(1);
        type = PromTypeCounter;
    }
    else 
    {
        type = PromTypeGauge;
    }
    istat::prom_munge(metric_name);
    PromMetric prom_metric(metric_name, time, val, type);

    grab aholdof(mutex_);
    PromDataMap::iterator ptr(data_.find(time));
    if (ptr != data_.end())
    {
        (*ptr).second.push_back(prom_metric);
    }
    else
    {
        std::vector<PromMetric> metric_list;
        metric_list.push_back(prom_metric);
        data_.insert(std::pair<time_t, std::vector<PromMetric> >(time, metric_list));
    }
}

void PromExporter::cleanup()
{
}
