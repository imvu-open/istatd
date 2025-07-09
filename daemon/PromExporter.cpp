#include "PromExporter.h"
#include "istat/strfunc.h"
#include "istat/istattime.h"
#include "Logs.h"
#include "Debug.h"


#include <boost/lexical_cast.hpp>
#include <boost/bind/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/assign.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <ctype.h>
#include <iostream>
#include <fstream>

#define COUNTER_RESOLUTION_SECOND 5
#define STALE_COUNTER_INTERVAL_SECOND 86400

DebugOption debugPromExporter("promExporter");
std::string metrics_dropped_counter("*istatd.agent.dropped.metrics");

PromMetric::PromMetric(std::string const &ctr, time_t time, double val, bool translate_name) :
    time_(time),
    value_(val),
    counter_updated_(true)
{
    init(ctr, translate_name);
}

PromMetric::PromMetric(std::string const &ctr, PromTagList const & tags, double val, bool translate_name) :
    value_(val),
    counter_updated_(true),
    tags_(tags)
{
    istat::istattime(&time_);
    init(ctr, translate_name);
}

PromMetric::PromMetric(std::string const &ctr, PromTagList const & tags, time_t time, double val, bool translate_name) :
    time_(time),
    value_(val),
    counter_updated_(true),
    tags_(tags)
{
    init(ctr, translate_name);
}

void PromMetric::init(std::string const& ctr, bool translate_name)
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
    istat::prom_munge_or_munge(name_, translate_name);
}

std::string PromMetric::toString() const
{
    std::stringstream ss;
    if (tags_.empty())
    {
        if (type_ == PromMetric::PromTypeCounter) {
            ss << name_ << "{counter=\"1\"} " << value_ << " " << timestampMilliseconds() << "\n";
        }
        else {
            ss << name_ << " " << value_ << " " << timestampMilliseconds() << "\n";
        }
    }
    else
    {
        for (PromTagList::const_iterator tit = tags_.begin(); tit != tags_.end(); ++tit)
        {
            ss << name_;
            std::tr1::unordered_map<std::string, std::string>::const_iterator mit = (*tit).begin();
            ss << "{" << mit->first << "=\"" << mit->second << "\"";
            ++mit;
            while (mit != (*tit).end())
            {
                ss << "," << mit->first << "=\"" << mit->second << "\"";
                ++mit;;
            }
            if (type_ == PromMetric::PromTypeCounter) {
                ss << ",counter=\"1\"";
            }
            ss << "}";
            ss << " " << value_ << " " << timestampMilliseconds() << "\n";
        }
    }
    return ss.str();
}

long PromMetric::timestampMilliseconds () const
{
    if (type_ == PromTypeCounter)
    {
        time_t now;
        istat::istattime(&now);
        return (now / COUNTER_RESOLUTION_SECOND) * COUNTER_RESOLUTION_SECOND * 1000;
    }
    else
    {
        return time_ * 1000;
    }
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
        ("cluster");

PromExporter::PromExporter(boost::asio::io_service &svc, std::string const &config_file, bool map_metric_name, size_t max_count, int cleanup_period) :
    svc_(svc),
    cleanup_interval_(cleanup_period),
    cleanup_timer_(svc_),
    staleness_timer_(svc_),
    map_metric_name_(map_metric_name),
    max_metric_count_(max_count)
{
    load_allowed_tags(config_file);
    std::string hostname = boost::asio::ip::host_name();
    istat::prom_munge_or_munge(hostname, map_metric_name_);
    agent_tags_ = boost::assign::list_of(boost::assign::map_list_of("host", hostname));
    cleanupNext();
    staleness_interval_ = STALE_COUNTER_INTERVAL_SECOND;
    removeStaleCounterNext();
}

PromExporter::~PromExporter()
{
    cleanup_timer_.cancel();
    staleness_timer_.cancel();
}

void PromExporter::dumpMetrics(std::vector<PromMetric> & res, std::vector<PromMetric> & new_metrics)
{
    grab aholdof(mutex_);
    // counters
    CumulativeCountsMap::iterator cit;
    for (cit = data_counters_.begin(); cit != data_counters_.end(); ++cit)
    {
        new_metrics.push_back((*cit).second);
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
        storeAmetric(ctr, PromMetric(basename, tags, time, val, map_metric_name_));
    }
    for (std::vector<std::string>::iterator it = no_tag_names.begin(); it != no_tag_names.end(); ++it)
    {
        storeAmetric((*it), PromMetric(*it, time, val, map_metric_name_));
    }
}

void PromExporter::load_allowed_tags(std::string const & tagnames_file)
{
    if (tagnames_file != "") {
       std::ifstream infile(tagnames_file.c_str());
       if (infile.is_open())
       {
            std::string tag_name;
            while(std::getline(infile, tag_name))
            {
                istat::trim(tag_name);
                extra_allowed_tags_.insert(tag_name);
            }
       }
       else
       {
            LogError << "PromExporter::load_allowed_tags unable to open: " << tagnames_file;
       }
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
                size_t pos = get_tag_pos(maybe_tag, 1);
                if (pos != std::string::npos)
                {
                    std::string tname = maybe_tag.substr(1, pos-1);
                    istat::prom_munge_or_munge(tname, map_metric_name_);
                    std::string tvalue = maybe_tag.substr(pos+1);
                    bool tag_added = false;
                    for (PromMetric::PromTagList::iterator tit = tags.begin(); tit != tags.end(); ++tit) {
                        if ((*tit).find(tname) == (*tit).end())
                        {
                            (*tit)[tname] = tvalue;
                            tag_added = true;
                            break;
                        }
                    }
                    if (! tag_added)
                    {
                        std::tr1::unordered_map<std::string, std::string> tag_grp =
                            boost::assign::map_list_of<std::string, std::string>(tname, tvalue);
                        tags.push_back(tag_grp);
                    }
                    continue;
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

size_t PromExporter::get_tag_pos(std::string const &suffix, size_t start)
{
    size_t pos = suffix.find_first_of('.', start);
    if (pos == std::string::npos) {
        return pos;
    }
    if (pos + 1 < suffix.size() )
    {
        std::string candidate = suffix.substr(1, pos-1);
        if (allowed_tags_.find(candidate) != allowed_tags_.end()
            || extra_allowed_tags_.find(candidate) != extra_allowed_tags_.end())
        {
            return pos;
        }
        return get_tag_pos(suffix, pos+1);
    }
    return std::string::npos;
}

void PromExporter::storeAmetric(std::string const & ctr, PromMetric const & prom_metric)
{
    if (max_count_reached())
    {
        insertCounter(metrics_dropped_counter, PromMetric(metrics_dropped_counter, agent_tags_, 1, map_metric_name_));
    }
    else
    {
        if (PromMetric::PromTypeCounter == prom_metric.getType()) {
            insertCounter(ctr, prom_metric);
        } else {
            data_gauges_.insert(std::pair<time_t, PromMetric>(prom_metric.getTimestamp(), prom_metric));
        }
    }
}

void PromExporter::insertCounter(std::string const & ctr, PromMetric const & prom_metric)
{
    CumulativeCountsMap::iterator cit = data_counters_.find(ctr);
    if (cit == data_counters_.end()) {
        data_counters_.insert(std::pair<std::string, PromMetric>(ctr, prom_metric));
    } else {
        (*cit).second.accumulate(prom_metric);
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

void PromExporter::removeStaleCounterNext()
{
    LogSpam << "PromExporter::removeStaleCounterNext() every " << staleness_interval_ << "s";
    staleness_timer_.expires_from_now(boost::posix_time::seconds(staleness_interval_));
    staleness_timer_.async_wait(boost::bind(&PromExporter::onRemoveStaleCounter, this));
}

void PromExporter::onRemoveStaleCounter()
{
    time_t now;
    istat::istattime(&now);
    time_t tlower = now - staleness_interval_;
    {
        grab aholdof(mutex_);
        CumulativeCountsMap::iterator cit = data_counters_.begin();
        while (cit != data_counters_.end())
        {
            CumulativeCountsMap::iterator current = cit;
            ++cit;
            if ((*current).second.getTimestamp() < tlower)
            {
                if (debugPromExporter.enabled())
                {
                    LogDebug << "PromExporter: discard staled counter " << (*current).second.getName()
                        << "older than " << tlower;
                }
               data_counters_.erase(current);
            }
        }
    }
    removeStaleCounterNext();
}

