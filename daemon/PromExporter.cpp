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

#define CLEANUP_INTERVAL_SECOND 30

DebugOption debugPromExporter("promExporter");

PromMetric::PromMetric(std::string const &ctr, time_t time, double val) :
    time_(time),
    value_(val),
    counter_updated_(true)
{
    init(ctr);
}

PromMetric::PromMetric(std::string const &ctr, PromTagList const & tags, time_t time, double val) :
    time_(time),
    value_(val),
    counter_updated_(true),
    tags_(tags)
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
        tags_.push_back(std::pair<std::string, std::string>("counter", "1"));
    }
    else
    {
        type_ = PromMetric::PromTypeGauge;
    }

    istat::prom_munge(name_);
    PromTagList::iterator tit = tags_.begin();
    for(; tit != tags_.end(); ++tit)
    {
        istat::prom_munge(tit->first);
    }
}

std::string PromMetric::toString() const
{
    std::stringstream ss;
    ss << name_;
    if (tags_.size() > 0)
    {
        PromTagList::const_iterator it = tags_.begin();
        ss << "{" << it->first << "=\"" << it->second << "\"";
        ++it;
        while (it != tags_.end())
        {
            ss << "," << it->first << "=\"" << it->second << "\"";
            ++it;;
        }
        ss << "}";
    }
    ss << " " << value_ << " " << (time_ * 1000) << "\n"; 
    return ss.str();
}

std::string PromMetric::typeString() const
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

void PromMetric::accumulate(PromMetric const &  prom_metric)
{
    const double & val = prom_metric.getValue();
    const time_t & time = prom_metric.getTimestamp();
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

const std::tr1::unordered_set<std::string> PromExporter::allowed_tags_ =
    boost::assign::list_of<std::string>
        ("host")
        ("role")
        ("class")
        ("pool")
        ("proxy-pool")
        ("cluster");

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
    grab aholdof(mutex_);
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
    //gauges
    std::tr1::unordered_set<std::string> new_gauges;
    for (PromGaugeMap::iterator git = data_gauges_.begin(); git != data_gauges_.end(); ++git)
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

void PromExporter::storeMetrics(std::string const &ctr, std::string const &basename, std::vector<std::string> const & cnames, time_t time, double val)
{
    PromMetric::PromTagList tags;
    std::vector<std::string> no_tag_names;
    extract_tags(basename, cnames, tags, no_tag_names);
    grab aholdof(mutex_);
    if (! tags.empty())
    {
        storeAmetric(ctr, PromMetric(basename, tags, time, val));
    }
    for (std::vector<std::string>::iterator it = no_tag_names.begin(); it != no_tag_names.end(); ++it)
    {
        storeAmetric((*it), PromMetric(*it, time, val));
    }
}

void PromExporter::extract_tags(
            std::string const & base,
            std::vector<std::string> const & cnames,
            PromMetric::PromTagList & tags,
            std::vector<std::string> & no_tag_ctrs)
{
    size_t bsize = base.size();
    if (bsize != 0)
    {
        std::vector<std::string>::const_iterator it;
        for (it = cnames.begin(); it != cnames.end(); ++it)
        {
            if (bsize > (*it).size() || (*it).substr(0, bsize) != base || (bsize < (*it).size() && (*it)[bsize] != '.'))
            {
                LogError << "PromExporter::extract_tags metric name mismatch base:" << base  << ", full name:" << (*it) << ".";
                continue;
            }
            else if (bsize < (*it).size())
            {
                std::string maybe_tag = (*it).substr(bsize);
                size_t pos = maybe_tag.find_first_of('.', 1);
                if (pos != std::string::npos && pos + 1 < maybe_tag.size() )
                {
                    std::string tname = maybe_tag.substr(1, pos-1);
                    if (allowed_tags_.find(tname) != allowed_tags_.end())
                    {
                        if (tname == "host")
                        {
                            tags.push_back(std::pair<std::string, std::string>(maybe_tag.substr(1, pos-1), maybe_tag.substr(pos+1)));
                        }
                        else
                        {
                            tags.push_back(std::pair<std::string, std::string>(maybe_tag.substr(1), "1"));
                        }
                        continue;
                    }
                }
            }
             no_tag_ctrs.push_back(*it);
        }
    }
    else
    {
        no_tag_ctrs = cnames;
    }
}

void PromExporter::storeAmetric(std::string const & ctr, PromMetric const & prom_metric)
{

    if (PromMetric::PromTypeCounter == prom_metric.getType()) {
        CumulativeCountsMap::iterator cit = data_counters_.find(ctr);
        if (cit == data_counters_.end())
        {
            data_counters_.insert(std::pair<std::string, PromMetric>(ctr, prom_metric));
        }
        else
        {
            (*cit).second.accumulate(prom_metric);
        }
    }
    else
    {
        data_gauges_.insert(std::pair<time_t, PromMetric>(prom_metric.getTimestamp(), prom_metric));
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

