
#if !defined(daemon_IStatStore_h)
#define daemon_IStatStore_h

#include <list>
#include <vector>

#include "IStatCounter.h"
#include "AllKeys.h"
#include <istat/Header.h>

#include <boost/asio/strand.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "Signal.h"

struct UniqueId
{
    inline UniqueId() { memset(id, 0, sizeof(id)); }
    inline bool operator==(UniqueId const &o) const { return !memcmp(id, o.id, sizeof(id)); }
    static UniqueId make();
    unsigned char id[16];
    std::string str() const;
};

class IComplete;
class Deleter;

class IStatStore : public boost::noncopyable, public boost::enable_shared_from_this<IStatStore>
{
public:
    virtual void record(std::string const &ctr, double value) = 0;
    virtual void record(std::string const &ctr, time_t time, double value) = 0;
    virtual void record(std::string const &ctr, time_t time, double value, double valueSq, double min, double max, size_t cnt) = 0;

    virtual void find(std::string const &ctr, boost::shared_ptr<IStatCounter> &statCounter, boost::asio::strand * &strand) = 0;

    virtual void listMatchingCounters(std::string const &pat, std::list<std::pair<std::string, CounterResponse> > &oList) = 0;

    virtual std::string const &getLocation() const = 0;

    virtual void flushAll(IComplete *cmp) = 0;

    virtual void getUniqueId(UniqueId &out) = 0;

    boost::signals2::signal<void (istat::Header const &)> iterateSignal;

    virtual void setAggregateCount(int ac) = 0;
    virtual void deleteCounter(std::string const &str, Deleter* deleter, IComplete *complete) = 0;
    virtual void deleteCounter(std::string const &str, IComplete *complete) = 0;
    virtual void deletePattern(std::string const &pattern, IComplete *complete) = 0;
    virtual void pruneEmptyDirsNow(const std::string &purgePath) = 0;
    virtual void refreshKeys() = 0;

protected:
    friend class boost::detail::shared_count;
    friend class boost::shared_ptr<IStatStore>;
    friend void boost::checked_delete<IStatStore>(IStatStore *);
    virtual ~IStatStore() {};

};

class NullStatStore : public IStatStore
{
public:
    NullStatStore() {}
    inline void record(std::string const &ctr, double value) {}
    inline void record(std::string const &ctr, time_t time, double value) {}
    inline void record(std::string const &ctr, time_t time, double value, double valueSq, double min, double max, size_t cnt) {}
    inline void find(std::string const &ctr, boost::shared_ptr<IStatCounter> &statCounter, boost::asio::strand * &strand) { strand = 0; }
    inline void listMatchingCounters(std::string const &pat, std::list<std::pair<std::string, CounterResponse> > &oList) {}
    inline std::string const &getLocation() const { static std::string ss; return ss; }
    inline void flushAll(IComplete *cmp) {}
    inline void getUniqueId(UniqueId &out) { out = UniqueId(); }
    inline void setAggregateCount(int ac) {}
    inline void deleteCounter(std::string const &, Deleter* deleter, IComplete *cmp) {}
    inline void deleteCounter(std::string const &, IComplete *cmp) {}
    inline void deletePattern(std::string const &, IComplete *cmp) {}
    inline void pruneEmptyDirsNow(const std::string &purgePath) {}
    inline void refreshKeys() {}
};

#endif  //  daemon_IStatStore_h

