
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
#include <iostream>
#include <dirent.h>
#include <algorithm>

using namespace istat;

//  Flush all counters in round-robin fashion during this time
#define FLUSH_INTERVAL_MS 300000
//  Don't wait more than this to flush at least one counter (if there are few counters)
#define FLUSH_INTERVAL_MS_MAX 3000

long const DEFAULT_MIN_REQUIRED_SPACE = 100 * 1024 * 1024;

StatStore::StatStore(std::string const &path, int uid,
    boost::asio::io_service &svc,
    boost::shared_ptr<IStatCounterFactory> factory,
    istat::Mmap *mm, long flushMs,
    long minimumRequiredSpace) :
    path_(path),
    svc_(svc),
    syncTimer_(svc_),
    availCheckTimer_(svc_),
    factory_(factory),
    mm_(mm),
    flushMs_(flushMs),
    minimumRequiredSpace_(minimumRequiredSpace),
    myId_(UniqueId::make()),
    numLoaded_(0),
    aggregateCount_(0)
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

        boost::shared_ptr<StatStore::AsyncCounter> asyncCounter = openCounter(cname, true, time);
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
boost::shared_ptr<StatStore::AsyncCounter> StatStore::openCounter(std::string const &name, bool create, time_t zeroTime)
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
        asyncCounter = boost::shared_ptr<StatStore::AsyncCounter>(new StatStore::AsyncCounter(svc_, factory_->create(xform, isCollated, zeroTime)));
        if (!asyncCounter)
        {
            return asyncCounter;
        }
        counters[xform] = asyncCounter;
        keys_.add(xform, isCollated);
    }
    //  strip the leaf name "extension"
    std::string sex(name);
    stripext(sex);
    if (sex != name)
    {
        //  recursively create counters up the chain
        openCounter(sex, create, zeroTime);
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
        openCounter(cname, true);
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

};
static boost::shared_ptr<DeletedCounter> theDeletedCounter(new DeletedCounter);

void StatStore::deleteCounter(std::string const &ctr, IComplete *complete)
{
    boost::shared_ptr<StatStore::AsyncCounter> aCount = openCounter(ctr, false, 0);
    if (!!aCount)
    {
        //  don't keep a reference to this counter anymore
        aCount->statCounter_ = theDeletedCounter;
    }
    svc_.post(CallComplete(complete));
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

