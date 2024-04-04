
#if !defined(daemon_PromExporter_h)
#define daemon_PromExporter_h

#include <map>
#include <vector>

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include "threadfunc.h"

enum MetricType
{
    PromTypeGauge = 0,
    PromTypeCounter = 1,
    PromTypeUnknown = 2
};

class PromMetric
{
public:
    PromMetric(std::string &ctr, time_t time, double val);
    PromMetric(std::string &ctr, time_t time, double val, MetricType type);
    std::string toString();
    inline double getValue() { return value_; }
    inline MetricType getType() { return type_; }

private:
    MetricType type_;
    time_t time_;
    double value_;
    std::string name_;
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
    virtual void dumpMetrics(std::vector<PromMetric> &res) = 0;
    virtual void storeMetrics(std::string const &ctr, time_t time, double val) = 0;
    virtual bool enabled() = 0;
};

class PromExporter : public IPromExporter
{
public:
    PromExporter(boost::asio::io_service &svc, bool enabled);
    virtual ~PromExporter();
    void dumpMetrics(std::vector<PromMetric> &res);
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

    void cleanupNext();
    void onCleanup();
};

class NullPromExporter : public IPromExporter
{
public:
    NullPromExporter() {};
    virtual ~NullPromExporter() {};
    inline void dumpMetrics(std::vector<PromMetric> &res) {}
    inline void storeMetrics(std::string const &ctr, time_t time, double val) {}
    inline bool enabled() { return false; }
};

#endif  //  daemon_PromExporter_h
