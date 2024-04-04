#include "PromExporter.h"
#include "istat/strfunc.h"
#include "istat/istattime.h"
#include "Logs.h"
#include "Debug.h"


#include <boost/lexical_cast.hpp>
#include <boost/bind/bind.hpp>
#include <boost/make_shared.hpp>
#include <ctype.h>
#include <iostream>

#define CLEANUP_INTERVAL_SECOND 60

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
    enabled_(enabled),
    cleanup_timer_(svc_)
{
    cleanup_interval_ = CLEANUP_INTERVAL_SECOND;
    cleanupNext();
}

PromExporter::~PromExporter()
{
    cleanup_timer_.cancel();
}

void PromExporter::dumpMetrics(std::vector<PromMetric> &res)
{
    PromDataMap sending_data;
    {
        grab aholdof(mutex_);
        data_.swap(sending_data);
    }
    PromDataMap::iterator pit;

    for (pit = sending_data.begin(); pit != sending_data.end(); ++pit)
    {
        res.push_back((*pit).second);
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
    data_.insert(std::pair<time_t, PromMetric>(time, prom_metric));
}

void PromExporter::cleanupNext()
{
    LogSpam << "PromExporter::cleanupNext() every " << cleanup_interval_ << "s";
    cleanup_timer_.expires_from_now(boost::posix_time::seconds(cleanup_interval_));
    cleanup_timer_.async_wait(boost::bind(&PromExporter::onCleanup, this));
}

void PromExporter::onCleanup()
{
    time_t now;
    istat::istattime(&now);
    time_t tlower = now - cleanup_interval_;
    {
        grab aholdof(mutex_);
        PromDataMap::iterator itUpper = data_.lower_bound(tlower);
        if (debugPromExporter.enabled())
        {
            LogDebug << "PromExporter: discard unscraped data older than " << tlower 
                << ". size before " << data_.size();
        }
        if (itUpper != data_.begin())
        {
            data_.erase(data_.begin(), itUpper);
            if (debugPromExporter.enabled())
            {
                LogDebug << "PromExporter: size after" << data_.size();
            }
        }
    }
    cleanupNext();
}

