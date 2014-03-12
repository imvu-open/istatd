#include <istat/StatFile.h>
#include <istat/Log.h>
#include <istat/istattime.h>
#include <istat/Atomic.h>
#include <istat/Env.h>

#include <boost/asio.hpp>
#include <boost/foreach.hpp>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <boost/thread.hpp>
#include <sys/time.h>
#include <sys/resource.h>
#include <pwd.h>
#include <signal.h>
#include <cstdlib>
#include <ctime>
#include <boost/filesystem.hpp>

#include "HttpServer.h"
#include "StatServer.h"
#include "StatStore.h"
#include "StatCounterFactory.h"
#include "IStatCounter.h"
#include "Argument.h"
#include "istat/strfunc.h"
#include "Logs.h"
#include "RequestInFlight.h"
#include "AdminServer.h"
#include "ReplicaServer.h"
#include "ReplicaOf.h"
#include "LoopbackCounter.h"
#include "Debug.h"
#include "Settings.h"
#include "Blacklist.h"



//  The maximum number of threads allowed. During start-up,
//  and during high load, writing threads may start blocking
//  on I/O. That's not a great situation to be in, but a large
//  number of threads will help unblock the situation and
//  get going. Note that, if you ever block for longer than
//  it takes to go through a page (8192 bytes) of bucket data
//  you are in a death spiral and will not recover.
#define MAX_THREAD_COUNT 384

using namespace istat;

boost::asio::io_service g_service;
std::vector<boost::shared_ptr<boost::thread> > threads;
extern DebugOption debugHttp;
DebugOption debugSegv("segv");

LoopbackCounter countExceptions("exceptions", TypeEvent);


#include <statgrab.h>
boost::asio::deadline_timer statsTimer_(g_service);
time_t nextStatsTime_;
enum { STATS_INTERVAL = 5 };
boost::asio::strand statsStrand_(g_service);
std::string statSuffix_;
std::string initialDir_;
Mmap *mm(istat::NewMmap());

char  pidFileName_[PATH_MAX];

void join_all_threads();


int get_files_cur()
{
    struct rlimit rl;
    memset(&rl, 0, sizeof(rl));
    getrlimit(RLIMIT_NOFILE, &rl);
    return rl.rlim_cur;
}

void set_mmap_max(int mm)
{
    try
    {
        std::ifstream is;
        is.open("/proc/sys/vm/max_map_count", std::ios::in | std::ios::binary);
        int curCnt = 0;
        is >> curCnt;
        if (curCnt < mm)
        {
            LogNotice << "Setting max mmap count to" << curCnt;
            is.close();
            std::ofstream os;
            os.open("/proc/sys/vm/max_map_count", std::ios::out | std::ios::binary);
            os << mm;
            os.close();
        }
    }
    catch (std::exception const &x)
    {
        LogWarning << "Could not update mmap count:" << x.what();
    }
}

static void set_files_max(rlim_t fm)
{
    struct rlimit rl;
    memset(&rl, 0, sizeof(rl));
    getrlimit(RLIMIT_NOFILE, &rl);
    if (rl.rlim_cur < fm)
    {
        rlim_t hardlimit = rl.rlim_max;
        rl.rlim_cur = fm;
        if (hardlimit < fm)
        {
            rl.rlim_max = fm;
        }
        int err = setrlimit(RLIMIT_NOFILE, &rl);
        if (err < 0)
        {
            LogNotice << "Hard file rlimit is " << hardlimit << ".";
            throw std::runtime_error(std::string("Could not set file limit to ") +
                boost::lexical_cast<std::string>(fm));
        }
    }
    //  make sure we can mmap!
    set_mmap_max(fm * 4);
}

int uid_by_name(std::string const &str)
{
    setpwent();
    struct passwd *pw;
    while ((pw = getpwent()) != NULL)
    {
        if (str == pw->pw_name)
        {
            return pw->pw_uid;
        }
    }
    endpwent();
    return -1;
}

//TODO: Implement agent as list-of-agents comma separated or multiple entries
Argument<std::string> listenAddress("listen-address", "", "IP address on which to start listeners. empty string to accept on all interfaces.");
Argument<int> httpPort("http-port", 8000, "Port to listen to for HTTP status service. 0 to disable.");
Argument<int> statPort("stat-port", 8111, "Port to listen to for incoming statistics. 0 to disable.");
Argument<std::string> agent("agent", "", "Agent to forward to, rather than save data locally.");
Argument<std::string> replicaOf("replica-of", "", "Source to pull-replicate from. Incompatible with stat-port.");
Argument<int> replicaPort("replica-port", 0, "Port that others can pull-replicate from. Requires store.");
Argument<int> flush("flush", 300, "How often (in seconds) to flush stats files to disk.");
Argument<std::string> store("store", "", "Where to store statistics files.");
Argument<std::string> config("config", "istatd.cfg", "Config file to read, if present.");
Argument<int> threadCount("thread-count", sysconf(_SC_NPROCESSORS_ONLN), "Number of threads to create.");
Argument<std::string> user("user", boost::lexical_cast<std::string>(getuid()), "User-id to set after adjusting rlimit.");
Argument<int> numFiles("num-files", get_files_cur(), "Number of files to allocate (rlimit).");
Argument<std::string> filesDir("files-dir", "files", "Where to serve built-in web files from.");
Argument<std::string> localStats("local-stats", "", "Gather local stats, counter names suffixed by the given string.");
Argument<std::string> logFile("log-file", "", "When specified, errors and debugging information are logged to the specified file.  Otherwise, this information is output on stdout/stderr.");
Argument<bool> daemonize("daemonize", false, "Unattach stdin; detach to the background.");
Argument<std::string> pidfile("pid-file", "", "Write the PID of the daemon to the given file.");
Argument<int> loglevel("log-level", 2, "How chatty to be on stdout/stderr. Higher is more. (0-4)");
Argument<bool> testMode("test", false, "When specified, run unit tests built into executable; don't start program.");
Argument<int> adminPort("admin-port", 0, "When specified, port that exposes an admin telnet interface");
Argument<int> minimumRequiredSpace("min-space", -1, "When specified, minimum free space required (in bytes). When below this amount, error messages will be generated periodically.");
Argument<std::string> retention("retention", "10s:10d,5m:1y9d,1h:6y12d,5m:7d:ret-7d:0.9", "When specified, adjusts number and size of retention times.");
Argument<std::string> debug("debug", "", "When specified, turns on specific debugging options (as comma separated list).");
Argument<std::string> settings("settings", "/tmp", "Where to store settings (mostly used for HTTP app).");
Argument<int> fakeTime("fake-time", 0, "Fake current time to UNIX epoch (for testing).");
Argument<int> agentInterval("agent-interval", 10, "Interval for batching values when forwarding.");
Argument<int> rollup("rollup", 0, "How many levels to roll up counter values.");
Argument<std::string> blacklist_path("blacklist-path", "", "Where to look for blacklisted hosts");
Argument<int> blacklist_period("blacklist-period", 60, "How often to check the file");
Argument<bool> disallow_compressed_responses("disallow-compressed-responses", false, "Should istatd-server disallow compression of http responses by accept-encoding");


void usage()
{
    std::cerr << "istatd built " __DATE__ << " " << __TIME__ << std::endl;
    for (ArgumentBase::iterator ptr(ArgumentBase::begin()), end(ArgumentBase::end());
        ptr != end; ++ptr)
    {
        std::cerr << std::left << "--" << std::setw(12) << (*ptr).first << std::setw(0) << " (" <<
            std::setw(12) << (*ptr).second->str() << std::setw(0) << ")   " << (*ptr).second->help() << std::endl;
    }
    std::cerr << "debug options:" << std::endl;
    std::string comma = "";
    for (DebugOption const *opt = DebugOption::first(); opt != NULL; opt = DebugOption::next(opt))
    {
        std::cerr << comma << opt->name();
        comma = ", ";
    }
    std::cerr << std::endl;
    exit(1);
}

bool loadConfigFile()
{
    std::string path(config.get());
    struct stat st;
    if (::stat(path.c_str(), &st) < 0 || !S_ISREG(st.st_mode))
    {
        std::cerr << "Not a file: " << path << std::endl;
        return false;
    }
    std::ifstream is(path.c_str(), std::ios::binary);
    if (!is.is_open())
    {
        std::cerr << "File not readable: " << path << std::endl;
        return false;
    }
    is.seekg(0, std::ios_base::end);
    std::streampos len = is.tellg();
    is.seekg(0, std::ios::beg);
    std::string data;
    if (len > 0)
    {
        data.resize(len);
        is.read(&data[0], len);
        std::string error;
        if (!ArgumentBase::parseData(data, error))
        {
            std::cerr << error << std::endl;
            return false;
        }
    }
    return true;
}


DebugOption debugAudit("audit");

LoopbackCounter mmOpens("mm.opens", TypeGauge);
LoopbackCounter mmCloses("mm.closes", TypeGauge);
LoopbackCounter mmMaps("mm.maps", TypeGauge);
LoopbackCounter mmUnmaps("mm.unmaps", TypeGauge);

struct RetentionCounters
{
    RetentionCounters(RetentionInterval *ri) :
        ri_(ri),
        pHits_((std::string("pagecache.hits.time_") + ri->name).c_str(), TypeEvent),
        pMisses_((std::string("pagecache.misses.time_") + ri->name).c_str(), TypeEvent)
    {
    }
    RetentionInterval *ri_;
    LoopbackCounter pHits_;
    LoopbackCounter pMisses_;
};

std::vector<RetentionCounters *> retentionCounters;

class AuditTimer
{
public:
    AuditTimer(boost::asio::io_service &svc, Mmap *mm = 0, StatServer *ss = 0) :
        timer_(svc),
        svc_(svc),
        mm_(mm),
        ss_(ss)
    {
        if (debugAudit.enabled())
        {
            LogDebug << "AuditTimer() thread" << boost::this_thread::get_id();
        }
        srand((int)((size_t)this & 0xffff));
        scheduleNext();
    }
    ~AuditTimer()
    {
        if (debugAudit.enabled())
        {
            LogDebug << "~AuditTimer() thread" << boost::this_thread::get_id();
        }
        timer_.cancel();
    }
    void cancel()
    {
        if (debugAudit.enabled())
        {
            LogDebug << "AuditTimer::cancel() thread" << boost::this_thread::get_id();
        }
        timer_.cancel();
    }
private:
    boost::asio::deadline_timer timer_;
    boost::asio::io_service &svc_;
    Mmap *mm_;
    StatServer *ss_;

    void auditFunc(boost::system::system_error const &err)
    {
        if (debugAudit.enabled())
        {
            LogDebug << "AuditTimer::auditFunc() thread" << boost::this_thread::get_id();
        }
        else
        {
            LogSpam << "auditFunc() " << getpid();
        }
        if (mm_)
        {
            int64_t nMap = 0, nUnmap = 0, nOpen = 0, nClose = 0;
            mm_->counters(&nMap, &nUnmap, &nOpen, &nClose);
            mmOpens.value(nOpen);
            mmCloses.value(nClose);
            mmMaps.value(nMap);
            mmUnmaps.value(nUnmap);
        }
        for (size_t i = 0, n = retentionCounters.size(); i != n; ++i)
        {
            RetentionCounters *rc = retentionCounters[i];
            int64_t v = rc->ri_->stats.nHits.stat_;
            istat::atomic_add(&rc->ri_->stats.nHits.stat_, -v);
            rc->pHits_.value(v);
            v = rc->ri_->stats.nMisses.stat_;
            istat::atomic_add(&rc->ri_->stats.nMisses.stat_, -v);
            rc->pMisses_.value(v);
        }
        scheduleNext();
    }

    void scheduleNext()
    {
        timer_.expires_from_now(boost::posix_time::seconds(10));
        timer_.async_wait(
            boost::bind(&AuditTimer::auditFunc, this, boost::asio::placeholders::error));
    }
};

class LogRolloverTimer
{
public:
    LogRolloverTimer(boost::asio::io_service &svc, int interval) :
        svc_(svc),
        timer_(svc),
        interval_(interval)
    {
        scheduleNext();
    }
private:
    boost::asio::io_service &svc_;
    boost::asio::deadline_timer timer_;
    int interval_;
    void timerFunc(boost::system::system_error const &err)
    {
        if (debugAudit.enabled())
        {
            LogDebug << "LogRolloverTimer::threadFunc() thread" << boost::this_thread::get_id();
        }
        else
        {
            LogSpam << "threadFunc() calling rollOver()" << getpid();
        }
        istat::LogConfig::rollOver();
        scheduleNext();
    }

    void scheduleNext()
    {
        timer_.expires_from_now(boost::posix_time::seconds(interval_));
        timer_.async_wait(
            boost::bind(&LogRolloverTimer::timerFunc, this, boost::asio::placeholders::error));
    }
};

void thread_fn()
{
    volatile bool do_retry = true;
    while (do_retry)
    {
        try
        {
            g_service.run();
            do_retry = false;
        }
        catch (boost::thread_interrupted const &ti)
        {
            do_retry = false;
        }
        catch (std::exception const &x)
        {
            LogError << "Uncaught exception in thread:" << x.what();
            ++countExceptions;
        }
    }
}

void mainThread_fn(StatServer *ss)
{
    boost::shared_ptr<AuditTimer> aTimer(new AuditTimer(g_service, mm, ss));
    boost::shared_ptr<LogRolloverTimer> lTimer(new LogRolloverTimer(g_service, 300));
    volatile bool do_retry = true;
    while (do_retry)
    {
        try
        {
            g_service.run();
            do_retry = false;
            aTimer->cancel();
        }
        catch (boost::thread_interrupted const &ti)
        {
            do_retry = false;
        }
        catch (std::exception const &x)
        {
            LogError << "Uncaught exception in main thread:" << boost::this_thread::get_id() << ":" << x.what();
            aTimer->cancel();
            ++countExceptions;
        }
    }
    LogError << "Main thread exits:" << boost::this_thread::get_id();
    join_all_threads();
}


static void printRequest(HttpRequestHolder const &req, StatServer *ss)
{
    if (debugHttp.enabled())
    {
        LogDebug << "http serving" << req->url();
    }
    else
    {
        LogSpam << "Serving:" << req->method() << req->url();
    }
    boost::shared_ptr<RequestInFlight> rif(new RequestInFlight(req.p_, ss, filesDir.get()));
    ss->service().post(boost::bind(&RequestInFlight::go, rif));
}

static void onHttpRequest(StatServer *ss, HttpRequestHolder const &req)
{
    LogSpam << "onHttpRequest()";
    if (debugHttp.enabled())
    {
        LogDebug << "http onHttpRequest binding handlers";
    }
    req->onHeader_.connect(boost::bind(printRequest, req, ss));
    req->onError_.connect(boost::bind(printRequest, req, ss));
}

static uid_t dropped_uid()
{
    uid_t uid = -1;
    try
    {
        uid = boost::lexical_cast<int>(user.get());
    }
    catch (boost::bad_lexical_cast const &)
    {
        uid = uid_by_name(user.get());
    }
    return uid;
}

void drop_privileges()
{
    LogSpam << "drop_privileges()";
    uid_t uid = dropped_uid();
    if (uid < 0)
    {
        throw std::runtime_error("User " + user.get() + " is not known.");
    }
    if ((geteuid() != uid) && (seteuid(uid) < 0))
    {
        throw std::runtime_error("Could not seteuid(" + boost::lexical_cast<std::string>(uid) + ").");
    }
}

static std::stringstream localFlush_;
static time_t localTime_;
static std::list<std::string> toHandle_;

void localBegin(time_t t)
{
    LogSpam << "localBegin(" << t << ")";
    localTime_ = t;
}

void localStat(float v, char const *n)
{
    localFlush_ << n << statSuffix_ << " " << localTime_ << " " << v << "\n";
    toHandle_.push_back(localFlush_.str());
    localFlush_.str("");
}

void localStatFlush(StatServer *ss)
{
    boost::shared_ptr<UdpConnectionInfo> tmp(new UdpConnectionInfo());
    //  it's null during initialization
    if (ss != 0)
    {
        for (std::list<std::string>::iterator ptr(toHandle_.begin()), end(toHandle_.end());
            ptr != end; ++ptr)
        {
            ss->handleCmd(*ptr, tmp);
        }
    }
    std::list<std::string>().swap(toHandle_);
}

void collectLocalStats(StatServer *ss)
{
    time_t now;
    istat::istattime(&now);
    if (ss != 0)
    {
        nextStatsTime_ = (now + STATS_INTERVAL);
        nextStatsTime_ -= (nextStatsTime_ % STATS_INTERVAL);
        statsTimer_.expires_from_now(boost::posix_time::seconds(nextStatsTime_ - now));
        statsTimer_.async_wait(statsStrand_.wrap(boost::bind(&collectLocalStats, ss)));
    }

    localBegin(now);

    sg_cpu_percents *cpu = sg_get_cpu_percents();
    localStat(cpu->user, "localstat.cpu.user");
    localStat(cpu->kernel, "localstat.cpu.kernel");
    localStat(cpu->idle, "localstat.cpu.idle");
    localStat(cpu->iowait, "localstat.cpu.iowait");
    localStat(cpu->swap, "localstat.cpu.swap");
    localStat(cpu->nice, "localstat.cpu.nice");

    sg_mem_stats *mem = sg_get_mem_stats();
    localStat(mem->total, "localstat.mem.total");
    localStat(mem->free, "localstat.mem.free");
    localStat(mem->used, "localstat.mem.used");
    localStat(mem->cache, "localstat.mem.cache");

    sg_load_stats *load = sg_get_load_stats();
    localStat(load->min1, "localstat.load.min1");
    localStat(load->min5, "localstat.load.min5");
    localStat(load->min15, "localstat.load.min15");

    sg_swap_stats *swap = sg_get_swap_stats();
    localStat(swap->total, "localstat.swap.total");
    localStat(swap->used, "localstat.swap.used");
    localStat(swap->free, "localstat.swap.free");

    int j = 0;
    sg_disk_io_stats *disk = sg_get_disk_io_stats_diff(&j);
    for (int i = 0; i != j; ++i)
    {
        std::string n("localstat.disk.");
        n += disk->disk_name;
        localStat(disk[i].read_bytes, (n + ".read").c_str());
        localStat(disk[i].write_bytes, (n + ".write").c_str());
        ++disk;
    }

    j = 0;
    sg_network_io_stats *net = sg_get_network_io_stats_diff(&j);
    for (int i = 0; i != j; ++i)
    {
        std::string n("localstat.net.");
        n += net->interface_name;
        localStat(net[i].tx, (n + ".tx").c_str());
        localStat(net[i].rx, (n + ".rx").c_str());
        localStat(net[i].ipackets, (n + ".ipackets").c_str());
        localStat(net[i].opackets, (n + ".opackets").c_str());
        localStat(net[i].ierrors, (n + ".ierrors").c_str());
        localStat(net[i].oerrors, (n + ".oerrors").c_str());
        localStat(net[i].collisions, (n + ".collisions").c_str());
    }

    sg_process_count *proc = sg_get_process_count();
    localStat(proc->total, "localstat.process.total");
    localStat(proc->running, "localstat.process.running");
    localStat(proc->sleeping, "localstat.process.sleeping");
    localStat(proc->stopped, "localstat.process.stopped");
    localStat(proc->zombie, "localstat.process.zombie");

    localStatFlush(ss);
}

int waitFdPair[2];

void handle_pid_daemon(std::string const &pidf, bool daemon)
{
    std::string cpf(combine_paths(initialDir_, pidf));
    // see if it already exists
    if (pidf.size() > 0)
    {
        struct stat stbuf;
        if (0 == ::stat(cpf.c_str(), &stbuf))
        {
            std::ifstream fi;
            fi.open(cpf.c_str(), std::ios_base::in | std::ios_base::binary);
            int pid = -1;
            fi >> pid;
            fi.close();
            if (pid > 0 && pid < (1 << 22)) // maximum PID max
            {
                if (kill(pid, 0) >= 0)
                {
                    std::cerr << pidf << " says process " << pid << " is running; I refuse to start." << std::endl;
                    exit(1);
                }
                else
                {
                    std::cerr << pidf << " was left over from process " << pid << "; will clobber."  << std::endl;
                }
            }
            else
            {
                std::cerr << pidf << " seems corrupt; got pidfile " << pid << "; will clobber." << std::endl;
            }
        }
    }
    //  yes, there's a race here if two invocations happen at the same time!

    int pid = getpid();

    if (daemon)
    {
        if (pipe(waitFdPair) < 0)
        {
            std::cerr << "daemonize: pipe() failed." << std::endl;
            exit(1);
        }
        pid = fork();
        if (pid)
        {
            if (pid < 0)
            {
                std::cerr << "daemonize: fork() failed." << std::endl;
                exit(1);
            }
        }
    }

    if (pidf.size() > 0 && pid != 0)
    {
        std::ofstream pf;
        pf.open(cpf.c_str(), std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
        pf << pid << std::endl;
        pf.close();
        strncpy(pidFileName_, cpf.c_str(), sizeof(pidFileName_));
        pidFileName_[sizeof(pidFileName_)-1] = 0;
    }
    if (daemon && pid != 0)
    {
        char buf[1];
        buf[0] = read(waitFdPair[0], buf, 1);
        exit(0);
    }
}

//  Use the
void unblock_waiter()
{
    //  If I have the "waiting" fd pipe open, then unblock the
    //  parent process waiting for me by writing on the pipe.
    if (waitFdPair[0] != waitFdPair[1])
    {
        char buf[1] = { 'X' };
        buf[0] = write(waitFdPair[1], buf, 1);
        close(waitFdPair[0]);
        close(waitFdPair[1]);
        memset(waitFdPair, 0, sizeof(waitFdPair));
        //  disassociate from stdin/out/err
        int devNull = ::open("/dev/null", O_RDWR);
        if (devNull < 0)
        {
            std::cerr << "istatd child: Could not open /dev/null" << std::endl;
        }
        else
        {
            ::dup2(devNull, 0);
            ::dup2(0, 1);
            ::dup2(0, 2);
            ::close(devNull);
        }
    }
}

void print_cmdline(char const *argv[])
{
    LogFormatterLog lfl(LogNotice << "starting istatd:");
    while (*argv)
    {
        lfl << *argv;
        ++argv;
    }
}

void get_initial_dir()
{
    char path[PATH_MAX];
    char const *d = getcwd(path, PATH_MAX);
    if (d == NULL)
    {
        LogError << "could not get working directory";
    }
    initialDir_ = path;
    LogNotice << "get_initial_dir(): initialDir_ is" << initialDir_;
}


DebugOption debugLogArgs("log_args");

void log_args()
{
    LogNotice << "istatd PID" << getpid() << "arguments:";
    for (ArgumentBase::iterator ptr(ArgumentBase::begin()), end(ArgumentBase::end());
        ptr != end; ++ptr)
    {
        LogNotice << "argument" << (*ptr).first << "=" << (*ptr).second->str();
    }
}

void join_all_threads()
{
    //  this will cause threads to eventually fall out of their function
    g_service.stop();
    BOOST_FOREACH(boost::shared_ptr<boost::thread> thr, threads)
    {
        try
        {
            thr->interrupt();
        }
        catch (...)
        {
            //  ignore it and move on
        }
    }
    BOOST_FOREACH(boost::shared_ptr<boost::thread> thr, threads)
    {
        try
        {
            thr->join();
        }
        catch (...)
        {
            //  ignore it and move on
        }
    }
    threads.clear();
}

void cleanup_exit()
{
    join_all_threads();
    if (pidFileName_[0])
    {
        fprintf(stderr, "removing pid file %s\n", pidFileName_);
        ::unlink(pidFileName_);
    }
}

void setupDebug()
{
    std::set<std::string> allOpts;
    for (DebugOption const *opt = DebugOption::first(); opt != NULL; opt = DebugOption::next(opt))
    {
        allOpts.insert(opt->name());
    }
    std::vector<std::string> opts;
    explode(debug.get(), ',', opts);
    if (opts.size())
    {
        std::set<std::string> sopts;
        BOOST_FOREACH(std::string &str, opts)
        {
            trim(str);
            if (str.length())
            {
                if (allOpts.find(str) == allOpts.end())
                {
                    std::cerr << "No such debug option: " << str << std::endl;
                    exit(1);
                }
                sopts.insert(str);
            }
        }
        DebugOption::setOptions(sopts);
    }
}


#include "test_main.inl"

int main(int argc, char const *argv[])
{
    get_initial_dir();
    print_cmdline(argv);
    bool gotfile = false;
    for (int i = 1; i <= argc-1; ++i)
    {
        if (!strcmp(argv[i], "--config"))
        {
            config.set(combine_paths(initialDir_, argv[i+1]));
            gotfile = true;
        }
    }
    if (!loadConfigFile() && gotfile)
    {
        return 1;
    }
    int ix = 1;
    while (ArgumentBase::parseArg(argc, argv, ix))
    {
        // keep going
    }
    if (argv[ix])
    {
        std::cerr << "Command line error near " << argv[ix] << std::endl;
        usage();
    }

    setupDebug();
    LogConfig::setLogLevel((LogLevel)loglevel.get());

    // test convert the address to ensure it's convertable without exceptions before entering main code
    if (listenAddress.get().length() > 0) {
        try
        {
            boost::asio::ip::address::from_string(listenAddress.get().c_str());
        }
        catch (std::exception const &x)
        {
            std::cerr << "Error in main() parsing listen-address [" << x.what() << "]" << std::endl;
            return 2;
        }
    }

    //  set the working directory to the parent of the store file, if specified
    if (store.get().size())
    {

        std::string cwd(boost::filesystem::path(combine_paths(initialDir_, store.get())).parent_path().string());
        if (cwd.size())
        {
            boost::filesystem::create_directory(cwd);
            if (chdir(cwd.c_str()) < 0)
            {
                LogError << "Error: could not chdir to" << cwd;
                exit(1);
            }
        }
    }

    std::string log_file_path = combine_paths(initialDir_, logFile.get());
    if (logFile.get().size() != 0)
    {
        LogConfig::setOutputFile(log_file_path.c_str());
    }

    if (fakeTime.get())
    {
        istat::FakeTime *ft = new istat::FakeTime(fakeTime.get());
        istat::Env::set<istat::FakeTime>(*ft);
        LogDebug << "Started with fake time value =" << istattime(0);
    }


    if (testMode.get())
    {
        if (!boost::filesystem::create_directories("/tmp/test") && !boost::filesystem::exists("/tmp/test"))
        {
            std::cerr << "Could not mkdir /tmp/test" << std::endl;
            return 1;
        }
        if (0 > chdir("/tmp/test"))
        {
            std::cerr << "Could not chdir to /tmp/test" << std::endl;
            return 1;
        }
        unlink("/tmp/test/testdata.tst");
        test_main();
        exit(0);
    }

    if (debugLogArgs.enabled())
    {
        log_args();
    }
    bool daemonizeBool = daemonize.get();
    handle_pid_daemon(pidfile.get(), daemonizeBool);
    atexit(cleanup_exit);

    set_files_max(numFiles.get());

    RetentionPolicy retentionPolicy(retention.get().c_str());

    //  audit the hit/miss counters for the retention intervals
    for (size_t i = 0, n = retentionPolicy.countIntervals(); i != n; ++i)
    {
        RetentionInterval const &ri = retentionPolicy.getInterval(i);
        retentionCounters.push_back(new RetentionCounters(const_cast<RetentionInterval *>(&ri)));
    }
    HttpServer *hsp = 0;
    AdminServer *asp = 0;
    ReplicaServer *reps = 0;
    ReplicaOf *rof = 0;
    try
    {
        istat::Env::set<ISettingsFactory>(*NewSettingsFactory(g_service, settings.get()));

        boost::shared_ptr<IStatStore> statStore;

        std::string storepath(store.get());
        if (storepath.size())
        {
            storepath = combine_paths(initialDir_, storepath);
            boost::shared_ptr<IStatCounterFactory> statCounterFactory(new StatCounterFactory(storepath, mm, retentionPolicy));

            StatStore * ss = new StatStore(storepath, dropped_uid(), g_service, statCounterFactory, mm, flush.get()*1000, minimumRequiredSpace.get());
            LoopbackCounter::setup(ss, g_service);
            statStore = boost::shared_ptr<IStatStore>(ss);
        }

        Blacklist::Configuration cfg = {};
        cfg.path = blacklist_path.get();
        cfg.period = blacklist_period.get();
        StatServer ss(statPort.get(), listenAddress.get(), agent.get(), std::max(agentInterval.get(), 1), cfg, g_service, statStore);
        if (replicaPort.get())
        {
            reps = new ReplicaServer(replicaPort.get(), listenAddress.get(), g_service, statStore);
        }
        if (replicaOf.get() != "")
        {
            rof = new ReplicaOf(replicaOf.get(), g_service, statStore);
        }

        if (disallow_compressed_responses.get())
        {
            AcceptEncodingHeader::disallow_compressed_responses = disallow_compressed_responses.get();
        }

        if (httpPort.get())
        {
            hsp = new HttpServer(httpPort.get(), g_service, listenAddress.get());
            hsp->onRequest_.connect(boost::bind(&onHttpRequest, &ss, _1));
        }
        if (adminPort.get())
        {
            asp = new AdminServer(adminPort.get(), listenAddress.get(), g_service, hsp, &ss, reps, rof);
        }

        statSuffix_ = localStats.get();
        if (statSuffix_.size() > 0)
        {
            if (statSuffix_[0] != '.' && statSuffix_[0] != '^')
            {
                statSuffix_ = "." + statSuffix_;
            }
            if (sg_init())
            {
                std::cerr << "Error: cannot open libstatgrab: " << sg_str_error(sg_get_error()) << ": " << sg_get_error_arg() << std::endl;
                exit(1);
            }
            //  initialize, without recording
            collectLocalStats(0);
            istat::istattime(&nextStatsTime_);
            nextStatsTime_ += STATS_INTERVAL;
            nextStatsTime_ -= (nextStatsTime_ % STATS_INTERVAL);
            statsTimer_.expires_at(boost::posix_time::from_time_t(nextStatsTime_));
            statsTimer_.async_wait(statsStrand_.wrap(boost::bind(&collectLocalStats, &ss)));
        }

        drop_privileges();
        unblock_waiter();

        int nThreads = threadCount.get();
        if (nThreads < 1)
        {
            LogWarning << "thread-count" << nThreads << "adjusted to 1.";
            nThreads = 1;
        }
        //  Hard-code the number of threads I can use at most.
        if (nThreads > MAX_THREAD_COUNT)
        {
            LogWarning << "thread-count" << nThreads << "adjusted to " << MAX_THREAD_COUNT << ".";
            nThreads = MAX_THREAD_COUNT;
        }
        if (nThreads > 1)
        {
            LogNotice << "using" << nThreads << "threads.";
        }
        {
            for (int i = 0; i != nThreads - 1; ++i)
            {
                threads.push_back(boost::shared_ptr<boost::thread>(new boost::thread(thread_fn)));
            }
        }
        if (rof != NULL)
        {
            rof->go();
        }
        //  main thread counts as one!
        mainThread_fn(&ss);
    }
    catch (std::exception const &x)
    {
        std::cerr << "Error in main() [" << x.what() << "]" << std::endl;
        delete hsp;
        delete asp;
        return 2;
    }
    catch (...)
    {
        std::cerr << "Error in main() [unknown]" << std::endl;
        delete hsp;
        delete asp;
        return 1;
    }

    return 0;
}

#if defined(main)
#undef main
#endif

