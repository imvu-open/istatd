#include "PromExporter.h"
#include "istat/strfunc.h"
#include "istat/istattime.h"
#include "Logs.h"
#include "Debug.h"


#include <boost/lexical_cast.hpp>
#include <boost/bind/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/assign.hpp>
#include <ctype.h>
#include <iostream>
#include <tr1/unordered_set>

#define CLEANUP_INTERVAL_SECOND 60

DebugOption debugPromExporter("promExporter");

const std::map<PromMetric::TagName, std::string> PromMetric::tag_names_ = 
    boost::assign::map_list_of<PromMetric::TagName, std::string>
        (PromTagHost, "host")
        (PromTagRole, "role")
        (PromTagClass, "class")
        (PromTagPool, "pool")
        (PromTagProxyPool, "proxy-pool")
        (PromTagCluster, "cluster");

PromMetric::PromMetric(std::string const &ctr, time_t time, double val) :
    time_(time),
    value_(val),
    counter_updated_(true)
{
    init(ctr);
}

void PromMetric::init(std::string const& ctr)
{
    name_ = ctr;
    if (ctr[0] == '*') 
    {
        name_ = ctr.substr(1);
        type_ = PromMetric::PromTypeCounter;
    }
    else 
    {
        type_ = PromMetric::PromTypeGauge;
    }

    std::map<TagName, std::string>::const_iterator it = tag_names_.begin();
    for (; it != tag_names_.end(); ++it)
    {
        if (storeTag(it->second)) break;
    }

    istat::prom_munge(name_);
    std::list<std::pair<std::string, std::string> >::iterator tit = tags_.begin();
    for(; tit != tags_.end(); ++tit)
    {
        istat::prom_munge(tit->second);
    }
}

bool PromMetric::storeTag(std::string const & tname)
{
    std::string::size_type i;
    if ((i = name_.find("." + tname + ".")) != std::string::npos) 
    {
        const std::string::size_type j = name_.find_last_of(".");
        if (j != std::string::npos && j != (name_.size() - 1) && (i + tname.size() + 1) == j)
        {
            tags_.push_back(std::pair<std::string, std::string>(tname, name_.substr(j + 1)));
            name_ = name_.substr(0, i);
            return true;
        }
    }
    return false;
}

std::string PromMetric::toString()
{
    std::stringstream ss;
    ss << name_;
    if (tags_.size() > 0)
    {
        std::list<std::pair<std::string, std::string> >::iterator it = tags_.begin();
        ss << "{" << it->first << "=\"" << it->second << "\"";
        ++it;
        while (it != tags_.end())
        {
            ss << ",\"" << it->first << "\"=\"" << it->second << "\"";
            ++it;;
        }
        ss << "}";
    }
    ss << " " << value_ << " " << (time_ * 1000) << "\n"; 
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

void PromMetric::accumulate(time_t time, double val)
{
    if (type_ == PromTypeCounter) {
        if (value_ > COUNT_MAX - val)
        {
            value_ = val;
        }
        else
        {
            value_ += val;
        }
        if (time > time_)
        {
            time_ = time;
        }
        counter_updated_ = true;
    }
}

PromExporter::PromExporter(boost::asio::io_service &svc) :
    svc_(svc),
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
    PromGaugeMap gagues_to_send;
    {
        //gauges
        grab aholdof(mutex_);
        data_gauges_.swap(gagues_to_send);

        // counters
        CumulativeCountsMap::iterator cit;
        for (cit = data_counters_.begin(); cit != data_counters_.end(); ++cit)
        {
            if ((*cit).second.getCounterStatus())
            {
                new_metrics.push_back((*cit).second);
                (*cit).second.setCounterStatus(false);
            }
        }
    }

    std::tr1::unordered_set<std::string> new_gauges;
    for (PromGaugeMap::iterator git = gagues_to_send.begin(); git != gagues_to_send.end(); ++git)
    {
        std::string mname = (*git).second.getName();
        if (new_gauges.find(mname) == new_gauges.end())
        {
            new_gauges.insert(mname);
            new_metrics.push_back((*git).second);
        }
        else
        {
            res.push_back((*git).second);
        }
    }
}

void PromExporter::storeMetrics(std::string const &name, time_t time, double val)
{
    PromMetric prom_metric(name, time, val);
    grab aholdof(mutex_);
    if (prom_metric.getType() == PromMetric::PromTypeCounter) {
        CumulativeCountsMap::iterator cit = data_counters_.find(name);
        if (cit == data_counters_.end())
        {
            data_counters_.insert(std::pair<std::string, PromMetric>(name, prom_metric));
        }
        else
        {
            (*cit).second.accumulate(time, val);
        }
    }
    else
    {
        data_gauges_.insert(std::pair<time_t, PromMetric>(time, prom_metric));
    }
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
        PromGaugeMap::iterator itUpper = data_gauges_.lower_bound(tlower);
        if (debugPromExporter.enabled())
        {
            LogDebug << "PromExporter: discard unscraped data older than " << tlower 
                << ". size before " << data_gauges_.size();
        }
        if (itUpper != data_gauges_.begin())
        {
            data_gauges_.erase(data_gauges_.begin(), itUpper);
            if (debugPromExporter.enabled())
            {
                LogDebug << "PromExporter: size after" << data_gauges_.size();
            }
        }
    }
    cleanupNext();
}

