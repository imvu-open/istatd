
#if !defined(daemon_PromExporter_h)
#define daemon_PromExporter_h

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

    PromMetric(std::string const &ctr, time_t time, double val);
    PromMetric(std::string const &ctr, PromTagList const & tags, time_t time, double val);
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
    void init(std::string const & ctr);
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
    PromExporter(boost::asio::io_service &svc);
    virtual ~PromExporter();
    void dumpMetrics(std::vector<PromMetric> &res, std::vector<PromMetric> & new_metrics);
    void storeMetrics(std::string const &ctr, std::string const & basename, std::vector<std::string> const & cname, time_t time, double val);
    inline bool enabled() { return true; }

private:
    friend void test_prom_exporter();

    typedef std::multimap<time_t, PromMetric, TimeComp> PromGaugeMap;
    typedef std::tr1::unordered_map<std::string, PromMetric> CumulativeCountsMap;

    boost::asio::io_service &svc_;
    int cleanup_interval_;
    PromGaugeMap data_gauges_;
    CumulativeCountsMap data_counters_;
    lock mutex_;
    bool enabled_;
    boost::asio::deadline_timer cleanup_timer_;
    static const std::tr1::unordered_set<std::string> allowed_tags_;

    void extract_tags(
            std::string const & base,
            std::vector<std::string> const & cname,
            PromMetric::PromTagList & tags,
            std::vector<std::string> & no_tag_ctrs);
    void storeAmetric(std::string const & ctr, PromMetric const & metric);
    void cleanupNext();
    void onCleanup();
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
