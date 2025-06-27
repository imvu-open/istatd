
#if !defined(daemon_PromExporter_h)
#define daemon_PromExporter_h

#include <string>
#include <map>
#include <vector>
#include <list>
#include <tr1/unordered_map>
#include <tr1/unordered_set>

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include "threadfunc.h"

#define COUNT_MAX 0xffffffff

class PromMetric
{
public:
    enum MetricType
    {
        PromTypeGauge = 0,
        PromTypeCounter = 1,
        PromTypeUnknown = 2
    };
    typedef std::list<std::tr1::unordered_map<std::string, std::string> > PromTagList;

    PromMetric(std::string const &ctr, time_t time, double val, bool translate_name = true);
    PromMetric(std::string const &ctr, PromTagList const & tags, time_t time, double val, bool translate_name = true);
    std::string toString() const;
    std::string typeString() const;
    void accumulate(PromMetric const & prom_metric);
    inline const double & getValue() const { return value_; }
    inline const std::string & getName() const { return name_; }
    inline const MetricType & getType() const { return type_; }
    inline const time_t & getTimestamp() const { return time_; }
    inline const bool getCounterStatus() const { return counter_updated_; }
    inline void setCounterStatus(bool updated) { counter_updated_ = updated; }

private:
    MetricType type_;
    time_t time_;
    double value_;
    std::string name_;
    bool counter_updated_;
    PromTagList tags_;
    void init(std::string const & ctr, bool translate_name = true);
    long timestampMilliseconds () const;
};

struct TimeComp {
  bool operator()(const time_t& a, const time_t& b) const 
  {
    return std::difftime(a, b) < 0;
  }
};

class IPromExporter : public boost::noncopyable
{
public:
    virtual ~IPromExporter() {};
    virtual void dumpMetrics(std::vector<PromMetric> & res, std::vector<PromMetric> & new_metrics) = 0;
    virtual void storeMetrics(std::string const &ctr, std::string const & basename, std::vector<std::string> const & cnames, time_t time, double val) = 0;
    virtual bool enabled() = 0;
};

class PromExporter : public IPromExporter
{
public:
    PromExporter(boost::asio::io_service &svc, std::string const &config_file, bool map_metric_name_ = true);
    virtual ~PromExporter();
    void dumpMetrics(std::vector<PromMetric> &res, std::vector<PromMetric> & new_metrics);
    void storeMetrics(std::string const &ctr, std::string const & basename, std::vector<std::string> const & cname, time_t time, double val);
    inline bool enabled() { return true; }

private:
    friend void test_prom_exporter();
    friend void test_prom_exporter_no_name_mapping();

    typedef std::multimap<time_t, PromMetric, TimeComp> PromGaugeMap;
    typedef std::tr1::unordered_map<std::string, PromMetric> CumulativeCountsMap;
    typedef std::tr1::unordered_set<std::string> TagNameSet;

    boost::asio::io_service &svc_;
    int cleanup_interval_;
    int staleness_interval_;
    PromGaugeMap data_gauges_;
    CumulativeCountsMap data_counters_;
    lock mutex_;
    bool enabled_;
    boost::asio::deadline_timer cleanup_timer_;
    boost::asio::deadline_timer staleness_timer_;
    static const TagNameSet allowed_tags_;
    TagNameSet extra_allowed_tags_;
    bool map_metric_name_;

    void load_allowed_tags(std::string const & file);
    size_t get_tag_pos(std::string const &suffix, size_t start);
    void extract_tags(
            std::string const & base,
            std::vector<std::string> const & cname,
            PromMetric::PromTagList & tags,
            std::vector<std::string> & no_tag_ctrs);
    void storeAmetric(std::string const & ctr, PromMetric const & metric);
    void cleanupNext();
    void onCleanup();
    void removeStaleCounterNext();
    void onRemoveStaleCounter();
};

class NullPromExporter : public IPromExporter
{
public:
    NullPromExporter() {};
    virtual ~NullPromExporter() {};
    inline void dumpMetrics(std::vector<PromMetric> & res, std::vector<PromMetric> & new_metrics) {}
    inline void storeMetrics(std::string const &ctr, std::string const & basename, std::vector<std::string> const & cname, time_t time, double val) {}
    inline bool enabled() { return false; }
};

#endif  //  daemon_PromExporter_h
