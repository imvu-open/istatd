
#if !defined(daemon_PromExporter_h)
#define daemon_PromExporter_h

#include <map>
#include <vector>
#include <list>
#include <tr1/unordered_map>

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include "threadfunc.h"

class PromMetric
{
public:
    enum MetricType
    {
        PromTypeGauge = 0,
        PromTypeCounter = 1,
        PromTypeUnknown = 2
    };

    PromMetric(std::string const &ctr, time_t time, double val);
    std::string toString();
    std::string typeString();
    inline double getValue() { return value_; }
    inline std::string getName() { return name_; }
    inline MetricType getType() { return type_; }

private:
    enum TagName
    {
        PromTagHost = 0,
        PromTagRole,
        PromTagClass,
        PromTagPool,
        PromTagProxyPool,
        PromTagCluster,
    };

    MetricType type_;
    time_t time_;
    double value_;
    std::string name_;
    std::list<std::pair<std::string, std::string> > tags_;
    static const std::map<TagName, std::string> tag_names_;

    void init(std::string const & ctr);
    bool hasTag(std::string const & tname);
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
    virtual void storeMetrics(std::string const &ctr, time_t time, double val) = 0;
    virtual bool enabled() = 0;
};

class PromExporter : public IPromExporter
{
public:
    PromExporter(boost::asio::io_service &svc, bool enabled);
    virtual ~PromExporter();
    void dumpMetrics(std::vector<PromMetric> &res, std::vector<PromMetric> & new_metrics);
    void storeMetrics(std::string const &ctr, time_t time, double val);
    inline bool enabled() { return enabled_; }

private:
    friend void test_prom_exporter();

    typedef std::multimap<time_t, PromMetric, TimeComp> PromDataMap;
    boost::asio::io_service &svc_;
    int cleanup_interval_;
    PromDataMap data_;
    lock mutex_;
    bool enabled_;
    boost::asio::deadline_timer cleanup_timer_;
    std::tr1::unordered_map<std::string, PromMetric::MetricType> metric_type_map_;

    void cleanupNext();
    void onCleanup();
};

class NullPromExporter : public IPromExporter
{
public:
    NullPromExporter() {};
    virtual ~NullPromExporter() {};
    inline void dumpMetrics(std::vector<PromMetric> & res, std::vector<PromMetric> & new_metrics) {}
    inline void storeMetrics(std::string const &ctr, time_t time, double val) {}
    inline bool enabled() { return false; }
};

#endif  //  daemon_PromExporter_h
