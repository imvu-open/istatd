
#include "StatStore.h"
#include "istat/strfunc.h"
#include "Logs.h"
#include "IComplete.h"
#include <istat/Mmap.h>
#include <istat/istattime.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <stdexcept>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <iostream>
#include <dirent.h>
#include <algorithm>

using namespace istat;

//  Flush all counters in round-robin fashion during this time
#define FLUSH_INTERVAL_MS 300000
//  Don't wait more than this to flush at least one counter (if there are few counters)
#define FLUSH_INTERVAL_MS_MAX 3000
// The second key refresh queued will catch all changes made so there is no need to queue any more.
#define MAX_QUEUED_KEY_REFRESHES 2

long const DEFAULT_MIN_REQUIRED_SPACE = 100 * 1024 * 1024;

StatStore::StatStore(std::string const &path, int uid,
    boost::asio::io_service &svc,
    boost::shared_ptr<IStatCounterFactory> factory,
    istat::Mmap *mm, long flushMs,
    long minimumRequiredSpace,
    int pruneEmptyDirsMs,
    bool recursivelyCreateCounters
    ) :
    path_(path),
    svc_(svc),
    pruneEmptyDirsTimer_(svc_),
    refreshStrand_(svc_),
    syncTimer_(svc_),
    availCheckTimer_(svc_),
    factory_(factory),
    mm_(mm),
    flushMs_(flushMs),
    minimumRequiredSpace_(minimumRequiredSpace),
    myId_(UniqueId::make()),
    numLoaded_(0),
    aggregateCount_(0),
    pruneEmptyDirsMs_(pruneEmptyDirsMs),
    recursivelyCreateCounters_(recursivelyCreateCounters),
    queuedRefreshes_(0)
{
    if(minimumRequiredSpace_ == -1)
    {
        minimumRequiredSpace_ = DEFAULT_MIN_REQUIRED_SPACE;
    }
    if (!boost::filesystem::create_directories(path) && !boost::filesystem::exists(path))
    {
        throw std::runtime_error("Can't create store directory: " + path);
    }
    if (uid >= 0)
    {
        struct stat st;
        if (::stat(path.c_str(), &st) < 0)
        {
            throw std::runtime_error("Can't stat(" + path + ")");
        }
        if (chown(path.c_str(), uid, st.st_gid) < 0)
        {
          throw std::runtime_error("Can't chown(" + path + ", " +
                    boost::lexical_cast<std::string>(uid) + ")");
        }
    }
    scanDir("");
    if (flushMs_ < 0)
    {
        flushMs_ = FLUSH_INTERVAL_MS_MAX;
    }
    syncNext();
    availCheckNext();
    pruneEmptyDirsNext();
}

StatStore::~StatStore()
{
    syncTimer_.cancel();
}

int StatStore::aggregateCount() 
{
    return aggregateCount_;
}

void StatStore::setAggregateCount(int ac)
{
    aggregateCount_ = ac;
}


bool StatStore::hasAvailableSpace()
{
    char const *dir = (path_.size() ? path_.c_str() : ".");
    return mm_->availableSpace(dir) >= minimumRequiredSpace_;
}

void StatStore::record(std::string const &ctr, double value)
{
    time_t t;
    istat::istattime(&t);
    record(ctr, t, value, value*value, value, value, 1);
}

void StatStore::record(std::string const &ctr, time_t time, double value)
{
    record(ctr, time, value, value*value, value, value, 1);
}

void StatStore::record(std::string const &ctr, time_t time, double value, double valueSq, double min, double max, size_t cnt)
{
    std::string cname(ctr);

    LogSpam << "StatStore::record()";
    //  I will only aggregate up to N levels -- more than that, and contention becomes too bad
    int maxAgg = 1 + aggregateCount_;
    while (maxAgg > 0)
    {
        --maxAgg;

        boost::shared_ptr<StatStore::AsyncCounter> asyncCounter = openCounter(cname, true, false, time);
        if (!!asyncCounter)
        {
            ++IStatCounter::enqueueRecords_;
            ++IStatCounter::queueLenRecords_;
            svc_.post(asyncCounter->strand_.wrap(boost::bind(&IStatCounter::record, asyncCounter->statCounter_, time, value, valueSq, min, max, cnt)));
        }
        if (!stripext(cname))
        {
            break;
        }
    }
}

void StatStore::find(std::string const &ctr, boost::shared_ptr<IStatCounter> &statCounter, boost::asio::strand * &strand)
{
    boost::shared_ptr<StatStore::AsyncCounter> asyncCounter = openCounter(ctr);
    if (!asyncCounter) {
        statCounter = boost::shared_ptr<IStatCounter>((IStatCounter*)0);
        return;
    }
    statCounter = asyncCounter->statCounter_;
    strand = &asyncCounter->strand_;

}

//  this takes the name un-munged
boost::shared_ptr<StatStore::AsyncCounter> StatStore::openCounter(std::string const &name, bool create, bool onlyExisting, time_t zeroTime)
{
    if (name == "")
    {
        return boost::shared_ptr<StatStore::AsyncCounter>((StatStore::AsyncCounter *)0);
    }

    std::string xform(name);
    bool isCollated = false;
    if (name[0] == '*')
    {
        xform = name.substr(1);
        if (!xform.length())
        {
            return boost::shared_ptr<StatStore::AsyncCounter>((StatStore::AsyncCounter *)0);
        }
        isCollated = true;
    }
    munge(xform);
    boost::shared_ptr<StatStore::AsyncCounter> asyncCounter;
    {
        grab aholdof(counterShards_.lock(xform));
        CounterMap &counters(counterShards_.map(xform));
        CounterMap::iterator ptr(counters.find(xform));
        if (ptr != counters.end())
        {
            //  This is the common case
            LogSpam << "StatStore::openCounter(" << name << ") ... returning ptr.second for search of munged " << xform;
            return (*ptr).second;
        }
        if (!create)
        {
            LogSpam << "StatStore::openCounter(" << name << ") ... skipping creation";
            return boost::shared_ptr<StatStore::AsyncCounter>((StatStore::AsyncCounter *)0);
        }
        //  I hold the lock while creating the file, which is sub-optimal
        //  but avoids racing.
        LogSpam << "StatStore::openCounter(" << name << ") ... creating";
        boost::shared_ptr<IStatCounter> statCounter = factory_->create(xform, isCollated, zeroTime, onlyExisting);
        if (!statCounter)
        {
            return boost::shared_ptr<StatStore::AsyncCounter>((StatStore::AsyncCounter *)0);
        }

        asyncCounter = boost::make_shared<StatStore::AsyncCounter>(boost::ref(svc_), statCounter);
        if (!asyncCounter)
        {
            return asyncCounter;
        }
        counters[xform] = asyncCounter;
        keys_.add(xform, asyncCounter->statCounter_->isCollated());
    }

    if(recursivelyCreateCounters_ && create)
    {
        //  strip the leaf name "extension"
        std::string sex(name);
        stripext(sex);
        if (sex != name)
        {
            //  recursively create counters up the chain
            openCounter(sex, create, onlyExisting, zeroTime);
        }
    }
    return asyncCounter;
}

void StatStore::flushOne(Shards::iterator &iterator)
{
    std::string key;
    boost::shared_ptr<AsyncCounter> value;
    if (!iterator.move_next(key, value))
    {
        iterator = counterShards_.begin();
        return;
    }
    LogSpam << "StatStore::flushOne(" << key << ")";
    svc_.post(value->strand_.wrap(boost::bind(&IStatCounter::flush, value->statCounter_, shared_from_this())));
}

void StatStore::syncNext()
{
    size_t n = counterShards_.approximate_size();
    //  flush no more than 500 counters per second
    long ms = std::max(2L,
        std::min((long)(flushMs_ / (n + 1)),
            (long)FLUSH_INTERVAL_MS_MAX));
    LogSpam << "StatStore::syncNext() every " << ms << " ms";
    syncTimer_.expires_from_now(boost::posix_time::milliseconds(ms));
    syncTimer_.async_wait(boost::bind(&StatStore::onSync, this));
}

void StatStore::onSync()
{
    LogSpam << "StatStore::onSync()";
    flushOne(syncIterator_);
    syncNext();
}

void StatStore::pruneEmptyDirsNext()
{
    LogSpam << "StatStore::pruneEmptyDirsNext() every " << pruneEmptyDirsMs_ << " ms";
    pruneEmptyDirsTimer_.expires_from_now(boost::posix_time::milliseconds(pruneEmptyDirsMs_));
    pruneEmptyDirsTimer_.async_wait(boost::bind(&StatStore::onPruneEmptyDirs, this, factory_->rootPath(), boost::asio::placeholders::error));
}

void StatStore::pruneEmptyDirsNow(const std::string& purgePath)
{
    LogSpam << "StatStore::pruneEmptyDirsNow(" << purgePath << ")";

    pruneEmptyDirs(purgePath);

}

void StatStore::onPruneEmptyDirs(const std::string& purgePath, const boost::system::error_code& e)
{
    LogSpam << "StatStore::onPruneEmptyDirs(" << purgePath << ")";
    if(e != boost::asio::error::operation_aborted)
    {
        pruneEmptyDirs(purgePath);
    }
    else
    {
        LogDebug << "StatStore::onPruneEmptyDirs() operation was cancelled";
    }
    pruneEmptyDirsNext();
}

void StatStore::pruneEmptyDirs(const std::string& purgePath)
{
    LogSpam << "StatStore::pruneEmptyDirs(" << purgePath << ")";
    grab aholdof(pruneEmptyDirsMutex_);
    boost::filesystem::directory_iterator it(purgePath);
    boost::filesystem::directory_iterator end = boost::filesystem::directory_iterator();

    while(it != end)
    {
        const boost::filesystem::path& path = it->path();

        if (boost::filesystem::is_directory(path))
        {
            const boost::filesystem::path del = path;
            ++it;

            pruneEmptyDirs(del.string());
        }
        else
        {
            ++it;
        }
    }
    if (boost::filesystem::is_directory(purgePath) && boost::filesystem::is_empty(purgePath))
    {
        const boost::filesystem::path del = purgePath;

        try
        {
            LogNotice << "Pruning an empty directory: " << del;
            boost::filesystem::remove(del);
        }
        catch (const boost::filesystem::filesystem_error & ex)
        {
            LogWarning << "Couldnt delete empty directory " << del << " because " << ex.what();
        }
    }
}

void StatStore::availCheckNext()
{
    int const CHECK_INTERVAL = 30;

    LogSpam << "StatStore::availCheckNext() every " << CHECK_INTERVAL << " seconds.";
    availCheckTimer_.expires_from_now(boost::posix_time::seconds(CHECK_INTERVAL));
    availCheckTimer_.async_wait(boost::bind(&StatStore::onAvailCheck, this));
}

void StatStore::onAvailCheck()
{
    LogSpam << "StatStore::onAvailCheck()";

    char const *dir = (path_.size() ? path_.c_str() : ".");
    if (!hasAvailableSpace())
    {
        char cwd[1024];
        LogError << "availableSpace" << dir << ": cwd " << getcwd(cwd, 1024) << ": not enough free space on device";
    }

    availCheckNext();
}

void StatStore::scanDir(std::string const& dir)
{
    std::string dirpath(path_);
    dirpath += "/";
    dirpath += dir;
    DIR *d = opendir(dirpath.c_str());
    LogSpam << "StatStore::scanDir(" << dirpath.c_str() << ")";
    if (!d)
    {
        LogError << "Could not scan directory: " << dir << " (out of file desccriptors?)";
    }
    else
    {
        struct dirent *dent;
        struct stat stbuf;
        std::string path, apath;
        while ((dent = readdir(d)) != NULL)
        {
            if (dent->d_name[0] == '.')
            {
                continue;
            }
            path = dir + dent->d_name;
            apath = path_ + "/" + path;
            if (::stat(apath.c_str(), &stbuf) < 0)
            {
                LogWarning << "Could not stat:" << apath;
            }
            else
            {
                if (S_ISREG(stbuf.st_mode))
                {
                    // do nothing
                }
                else if (S_ISDIR(stbuf.st_mode))
                {
                    scanDir(path + "/");
                    loadCtr(path);
                }
                else
                {
                    LogWarning << "Not a file or directory:" << path;
                }
            }
        }
        closedir(d);
    }
    int64_t now = counterShards_.exact_size();
    //  give progressive load feedback
    if (now > numLoaded_ * 1.2 + 9 || now > numLoaded_ + 1000)
    {
        LogWarning << "Loaded" << counterShards_.exact_size() << "counters.";
        numLoaded_ = now;
    }
}

void StatStore::loadCtr(std::string const &file)
{
    LogSpam << "StatStore::loadCtr(" << file << ")";
    std::string cname(file);
    std::replace(cname.begin(), cname.end(), '/', '.');
    try
    {
        openCounter(cname, true, !recursivelyCreateCounters_);
    }
    catch (std::exception const &x)
    {
        LogError << "Exception loading" << file << ":" << x.what();
    }
}


void StatStore::listMatchingCounters(std::string const &pat, std::list<std::pair<std::string, CounterResponse> > &oList)
{
    LogSpam << "StatStore::listMatchingCounters(" << pat << ")";
    keys_.match(pat, oList);
}

class DeletedCounter : public IStatCounter
{
public:
    DeletedCounter() {}
    virtual bool isCollated() const { return false; }
    virtual void record(time_t time, double value, double valueSq, double min, double max, size_t cnt) {}
    virtual void flush(boost::shared_ptr<IStatStore> const &store) {}
    virtual void forceFlush(boost::shared_ptr<IStatStore> const &store) {}
    virtual void maybeShiftCollated(time_t t) {}
    virtual void select(time_t start, time_t end, bool trailing, std::vector<istat::Bucket> &oBuckets, time_t &normalized_start, time_t &normalized_end, time_t &interval, size_t max_samples) {}
    virtual void purge(std::string rootPath) {}

};
static boost::shared_ptr<DeletedCounter> theDeletedCounter = boost::make_shared<DeletedCounter>();

class Deleter
{
public:
    Deleter(boost::shared_ptr<IStatStore> const &ss, std::string rootPath) : ss_(ss), rootPath_(rootPath), rootCtrPath_("") {}

    struct Work
    {
        boost::shared_ptr<StatStore::AsyncCounter> aCount;
        boost::shared_ptr<IStatCounter> counter;
        std::string ctr;
    };

    void go()
    {
        if (!toDelete.size())
        {
            LogSpam << "Deletion batch done";
            ss_->pruneEmptyDirsNow(rootPath_ + "/" + istat::counter_filename(rootCtrPath_));
            ss_->refreshKeys();
            delete this;
            return;
        }
        toDelete.front().aCount->strand_.get_io_service().post(toDelete.front().aCount->strand_.wrap(
            boost::bind(&Deleter::delete_stuff, this)));
    }
    void delete_stuff()
    {
        Deleter::Work work(toDelete.front());
        toDelete.pop_front();
        //counter purging needs to know the stores root path to properly relocate counters for backup
        work.counter->purge(rootPath_);
        go();
    }

    void addWork(Work &w)
    {
        LogSpam << "Scheduling a piece of work for deleting counter " << w.ctr;
        toDelete.push_back(w);
        //Prune will need to know the 'closest' to rootPath_ counter in order to prune only the changed paths
        if(w.ctr.length() < rootCtrPath_.length() && rootCtrPath_.length() != 0)
        {
            rootCtrPath_ = w.ctr;
        }
    }

private:
    boost::shared_ptr<IStatStore> ss_;
    std::string rootPath_;
    std::list<Deleter::Work> toDelete;
    std::string rootCtrPath_;
};
void StatStore::deleteCounter(std::string const &ctr, IComplete *complete)
{
    Deleter *deleter = new Deleter(IStatStore::shared_from_this(), factory_->rootPath());
    deleteCounter(ctr, deleter, complete);
    deleter->go();
}

void StatStore::deleteCounter(std::string const &ctr, Deleter* deleter, IComplete *complete)
{
    boost::shared_ptr<StatStore::AsyncCounter> aCount = openCounter(ctr, false, false, 0);
    if (!!aCount)
    {
        boost::shared_ptr<IStatCounter> oldPtr = aCount->statCounter_;
        aCount->statCounter_ = theDeletedCounter;
        Deleter::Work w;
        w.aCount = aCount;
        w.counter = oldPtr;
        w.ctr = ctr;

        deleter->addWork(w);

    }
    if(complete)
    {
        svc_.post(CallComplete(complete));
    }
}

void StatStore::deletePattern(std::string const &pattern, IComplete *complete)
{

    std::list<std::pair<std::string, CounterResponse> > results;
    listMatchingCounters(pattern, results);

    Deleter *deleter = new Deleter(IStatStore::shared_from_this(), factory_->rootPath());
    for (std::list<std::pair<std::string, CounterResponse> >::iterator ptr(results.begin()), end(results.end()); ptr != end; ptr++)
    {
        deleteCounter((*ptr).first, deleter, (IComplete*)0);
    }
    deleter->go();
    if(complete)
    {
        svc_.post(CallComplete(complete));
    }
}

void StatStore::refreshKeys()
{
    LogSpam << "StatStore::refreshKeys()";
    scheduleRefresh();
}

void StatStore::refreshAllKeys()
{
    std::string key;
    boost::shared_ptr<AsyncCounter> value;

    LogSpam << "StatStore::refreshAllKeys()";
    for (int i = 0; i < counterShards_.dimension(); ++i)
    {
        std::pair<CounterMap&, ::lock&> mapAndLock = counterShards_.get_from_index(i);
        ::lock& lock = mapAndLock.second;
        CounterMap& map = mapAndLock.first;
        grab aholdof(lock);


        CounterMap::iterator it = map.begin();

        while (it != map.end())
        {
            key = (*it).first;
            value = (*it).second;
            if(!(value->statCounter_ == theDeletedCounter))
            {
                keysWA_.add(key, value->statCounter_->isCollated());
                ++it;
            }
            else
            {
                LogNotice << "StatStore::refreshAllKeys(" << key << ") ... removing a theDeleted counter from the map";
                map.erase(it++);
            }
        }
    }
    keys_.exchange(keysWA_);
    keysWA_.delete_all();
    __sync_fetch_and_sub(&queuedRefreshes_, 1);
}

void StatStore::scheduleRefresh()
{
    LogSpam << "StatStore::scheduleRefresh()";

    if(__sync_fetch_and_add(&queuedRefreshes_, 1) <= MAX_QUEUED_KEY_REFRESHES)
    {
        LogDebug << "StatStore::scheduleRefresh() - queueing a refresh. (" << queuedRefreshes_ << "/" << MAX_QUEUED_KEY_REFRESHES << ") scheduled.";
        svc_.post(refreshStrand_.wrap(boost::bind(&StatStore::refreshAllKeys, this)));
    }
    else
    {
        LogNotice << "StatStore::scheduleRefresh() - Did not schedule a refresh. There are already " << queuedRefreshes_ << "/" << MAX_QUEUED_KEY_REFRESHES << " scheduled.";
    }
}

class Flusher
{
public:
    Flusher(boost::shared_ptr<IStatStore> const &ss, IComplete *cmp) : ss_(ss), cmp_(cmp) {}
    void go()
    {
        if (!ptrs.size())
        {
            LogDebug << "all flush complete";
            cmp_->on_complete();
            delete this;
            return;
        }
        ptrs.front()->strand_.get_io_service().post(ptrs.front()->strand_.wrap(
            boost::bind(&Flusher::flush_stuff, this)));
    }
    void flush_stuff()
    {
        boost::shared_ptr<StatStore::AsyncCounter> ptr(ptrs.front());
        ptrs.pop_front();
        // Force, so collations are fully shifted.
        ptr->statCounter_->forceFlush(ss_);
        go();
    }
    boost::shared_ptr<IStatStore> ss_;
    IComplete *cmp_;
    std::list<boost::shared_ptr<StatStore::AsyncCounter> > ptrs;
};

void StatStore::flushAll(IComplete *cmp)
{
    LogSpam << "StatStore::flushAll()";
    Shards::iterator ptr(counterShards_.begin());
    CounterMap::key_type key;
    CounterMap::mapped_type data;
    //  Each one needs to be flushed on its own strand.
    //  Do this serially, and then return to the caller.
    Flusher *f = new Flusher(IStatStore::shared_from_this(), cmp);
    while (ptr.move_next(key, data))
    {
        LogSpam << "Scheduling flush for" << key;
        f->ptrs.push_back(data);
    }
    f->go();
}


void StatStore::getUniqueId(UniqueId &uid)
{
    uid = myId_;
}



std::string UniqueId::str() const
{
    std::stringstream ss;
    ss.width(2);
    ss.fill('0');
    ss << std::hex;
    for (int i = 0; i != 16; ++i)
    {
        if (i && !(i & 3))
        {
            ss << ":";
        }
        ss << (int)id[i];
    }
    return ss.str();
}

UniqueId UniqueId::make()
{
    UniqueId ret;
    int fd = open("/dev/urandom", O_RDONLY, 0444);
    if (fd >= 0) {
        if (::read(fd, ret.id, sizeof(ret.id)) < 0) {
            LogError << "Out of random numbers";
        }
        ::close(fd);
    }
    pid_t pid = ::getpid();
    memcpy(ret.id, &pid, sizeof(pid_t));
    return ret;
}

