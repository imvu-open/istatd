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

PromMetric::PromMetric(std::string const &ctr, time_t time, double val) :
    time_(time),
    value_(val)
{
    init(ctr);
}

void PromMetric::init(std::string const& ctr)
{
    name_ = ctr;
    if (ctr[0] == '*') 
    {
        name_ = ctr.substr(1);
        type_ = PromTypeCounter;
    }
    else 
    {
        type_ = PromTypeGauge;
    }
    istat::prom_munge(name_);
}

std::string PromMetric::toString()
{
    std::stringstream ss;
    ss << name_ << " " << value_ << " " << (time_ * 1000) << "\n"; 
    return ss.str();
}

std::string PromMetric::typeString()
{
    std::stringstream ss;
    ss << "# TYPE " << name_ << " " ;
    switch (type_) 
    {
        case PromTypeGauge:
            ss << "gauge";
            break;
        case PromTypeCounter:
            ss << "counter";
            break;
        default:
            ss << "untyped";
            break;
    }
    ss << "\n"; 
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

void PromExporter::dumpMetrics(std::vector<PromMetric> & res, std::vector<PromMetric> & new_metrics)
{
    PromDataMap sending_data;
    grab aholdof(mutex_);
    data_.swap(sending_data);

    for (PromDataMap::iterator pit = sending_data.begin(); pit != sending_data.end(); ++pit)
    {
        res.push_back((*pit).second);
        std::string mname = (*pit).second.getName();
        MetricType mtype = (*pit).second.getType();
        if (metric_type_map_.find(mname) == metric_type_map_.end()) 
        {
            metric_type_map_.insert(std::pair<std::string, MetricType>(mname, mtype));
            new_metrics.push_back((*pit).second);
        }
    }
    sending_data.clear();
}

void PromExporter::storeMetrics(std::string const &name, time_t time, double val)
{
    PromMetric prom_metric(name, time, val);
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

