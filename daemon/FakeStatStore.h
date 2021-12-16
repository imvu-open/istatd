
#if !defined(daemon_FakeStatStore_h)
#define daemon_FakeStatStore_h

#include <boost/version.hpp>

#include "StatStore.h"
#include "IComplete.h"

class Deleter;

class FakeStatStore : public IStatStore {
public:
    FakeStatStore(boost::asio::io_service &svc);
    ~FakeStatStore();

    boost::asio::io_service &svc_;
    std::string location_;
    std::map<std::string, boost::shared_ptr<IStatCounter> > fakeCounters_;
    std::map<std::string, std::vector<istat::Bucket> > recorded_;

    virtual void record(std::string const &ctr, double value);
    virtual void record(std::string const &ctr, time_t time, double value);
    virtual void record(std::string const &ctr, time_t time, double value, double valueSq, double min, double max, size_t cnt);
#if BOOST_VERSION >= 106700
    virtual void find(std::string const &ctr, boost::shared_ptr<IStatCounter> &statCounter, boost::asio::strand<boost::asio::io_service::executor_type> *&strand);
#else
    virtual void find(std::string const &ctr, boost::shared_ptr<IStatCounter> &statCounter, boost::asio::strand *&strand);
#endif
    virtual void listMatchingCounters(std::string const &pat, std::list<std::pair<std::string, CounterResponse> > &oList);
    virtual std::string const &getLocation() const;
    void manufactureTruth(std::string const &name, time_t from, time_t to, time_t interval);
    void flushAll(IComplete *cmp) { cmp->on_complete(); }
    void getUniqueId(UniqueId &oid) { memset((char*)&oid, 0, sizeof(oid)); }
    void setAggregateCount(int ac) {};
    void deleteCounter(std::string const &str, Deleter* deleter, IComplete *complete) { complete->on_complete(); }
    void deleteCounter(std::string const &str, IComplete *complete) { complete->on_complete(); }
    void deletePattern(std::string const &pattern, IComplete *complete) { complete->on_complete(); }
    void pruneEmptyDirsNow(const std::string &purgePath) {}
    void refreshKeys() {}
};

#endif // daemon_FakeStatStore_h
