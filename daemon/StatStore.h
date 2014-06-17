
#if !defined(daemon_StatStore_h)
#define daemon_StatStore_h

#include "ShardedMap.h"
#include "threadfunc.h"
#include "IStatCounterFactory.h"
#include "IStatStore.h"
#include "StatCounter.h"
#include "AllKeys.h"

#include <string>
#include <map>
#include <list>
#include <vector>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <istat/StatFile.h>


class Deleter;

class StatStore : public IStatStore
{
public:
    StatStore(std::string const &path, int uid,
        boost::asio::io_service &svc,
        boost::shared_ptr<IStatCounterFactory> factory,
        istat::Mmap *mm, long flushMs = -1,
        long minimumRequiredSpace = -1,
        int pruneEmptyDirsMs = 60000
        );

    virtual ~StatStore();

    virtual bool hasAvailableSpace();

    virtual void record(std::string const &ctr, double value);
    virtual void record(std::string const &ctr, time_t time, double value);
    virtual void record(std::string const &ctr, time_t time, double value, double valueSq, double min, double max, size_t cnt);

    virtual void find(std::string const &ctr, boost::shared_ptr<IStatCounter> &statCounter, boost::asio::strand * &strand);

    virtual void listMatchingCounters(std::string const &pat, std::list<std::pair<std::string, CounterResponse> > &oList);

    inline virtual std::string const &getLocation() const { return path_; }

    //  expensive!
    void flushAll(IComplete *complete);

    void getUniqueId(UniqueId &oid);

    void deleteCounter(std::string const &ctr, IComplete *complete);
    void deleteCounter(std::string const &ctr, Deleter* deleter, IComplete *complete);
    void deletePattern(std::string const &pattern, IComplete *complete);

    virtual void pruneEmptyDirsNow(const std::string &purgePath);

    void refreshKeys();

    struct AsyncCounter
    {
        AsyncCounter(boost::asio::io_service &svc,
                     boost::shared_ptr<IStatCounter> statCounter)
            : statCounter_(statCounter),
            strand_(svc) {
        }
        boost::shared_ptr<IStatCounter> statCounter_;
        boost::asio::strand strand_;
    };
    typedef std::map<std::string, boost::shared_ptr<AsyncCounter> > CounterMap;

    int aggregateCount();
    void setAggregateCount(int ac);

private:
    typedef ShardedMap<CounterMap> Shards;
    Shards counterShards_;
    void flushOne(Shards::iterator &iterator);
    void pruneEmptyDirs(const std::string &purgePath);
    std::string path_;
    boost::asio::io_service &svc_;
    boost::asio::deadline_timer pruneEmptyDirsTimer_;
    boost::asio::strand refreshStrand_;
    boost::asio::deadline_timer syncTimer_;
    boost::asio::deadline_timer availCheckTimer_;
    Shards::iterator syncIterator_;
    boost::shared_ptr<IStatCounterFactory> factory_;
    std::list< boost::shared_ptr<AsyncCounter> > pendedDeletes_;
    ::lock deleteMutex_;
    ::lock pruneEmptyDirsMutex_;
    istat::Mmap *mm_;
    long flushMs_;
    long minimumRequiredSpace_;
    UniqueId myId_;
    AllKeys keys_;
    AllKeys keysWA_;
    int64_t numLoaded_;
    int aggregateCount_;
    int pruneEmptyDirsMs_;
    volatile int queuedRefreshes_;

    //  openCounter() takes an un-munged name!
    boost::shared_ptr<AsyncCounter> openCounter(std::string const &name, bool create=false, time_t zeroTime=0);

    void scheduleRefresh();
    void refreshAllKeys();

    void syncNext();
    void onSync();

    void pruneEmptyDirsNext();
    void onPruneEmptyDirs(const std::string & purgePath, const boost::system::error_code& e);

    void availCheckNext();
    void onAvailCheck();

    void scanDir(std::string const &path);
    void loadCtr(std::string const &file);
};

#endif  //  daemon_StatStore_h
