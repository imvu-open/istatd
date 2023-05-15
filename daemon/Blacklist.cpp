
#include "Blacklist.h"

#include "LoopbackCounter.h"
#include "Logs.h"
#include <algorithm>
#include <string>
#include <sys/stat.h>
#include <boost/filesystem.hpp>
#include <boost/bind/bind.hpp>

LoopbackCounter countBlacklisted("blacklist.count", TypeEvent);

using namespace istat;

Blacklist::Blacklist(boost::asio::io_service &svc, Configuration &cfg) :
    svc_(svc),
    tryReadBlacklistTimer_(svc),
    blacklistPath_(cfg.path),
    lastModifiedTime_(0),
    period_(cfg.period)
{
    LogSpam << "Blacklist::Blacklist being created ( " << cfg.path << " )";
    load();
    startRead();
}

void Blacklist::startRead()
{
    tryReadBlacklistTimer_.expires_from_now(boost::posix_time::seconds(period_));
    tryReadBlacklistTimer_.async_wait(boost::bind(&Blacklist::onRead, this, boost::asio::placeholders::error));
}

void Blacklist::onRead(boost::system::error_code const &err)
{
    LogSpam << "Blacklist::onRead()";
    if (!!err)
    {
        LogWarning << "Blacklist::onRead error:" << err;
        return;
    }

    load();
    startRead();
}

void Blacklist::load()
{
    try
    {
        grab aholdof(lock_);
        struct stat st;
        if (::stat(blacklistPath_.c_str(), &st) < 0)
        {
            throw std::runtime_error("blacklist: could not find file");
        }

        time_t modifiedTime = st.st_mtime;

        if (modifiedTime <= lastModifiedTime_)
        {
            LogDebug << "Blacklist::load modified time is not new enough ( " << modifiedTime << ", " << lastModifiedTime_ << " )";
            return;
        }

        int i = ::open(blacklistPath_.c_str(), O_RDONLY);
        if (i == -1)
        {
            throw std::runtime_error("blacklist: could not open file");
        }
        int64_t size = ::lseek(i, 0, 2);

        ::lseek(i, 0, 0);
        std::vector<char> buf;
        buf.resize(size +1);
        if (size != ::read(i, &buf[0], size))
        {
            ::close(i);
            throw std::runtime_error("blacklist: could not read file");
        }
        ::close(i);
        buf[size] = '\n';

        char *base = &buf[0];
        char *end = &buf[size];

        blacklistSet_.clear();

        // file format: Newling delimited hostname
        // host
        // host
        // host
        for (char *cur = base; cur <= end; ++cur)
        {
            if (*cur == '\n')
            {
                //  only non-empty
                if (cur > base)
                {
                    std::string name(base, cur);
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    blacklistSet_.insert(name);
                    LogNotice << "Blacklist::load blacklist host ( " << name << " ).";
                }
                base = cur + 1;
            }
        }

        lastModifiedTime_ = modifiedTime;
        LogNotice << "Blacklist::load new mod time! ( " << lastModifiedTime_ << " ).";
    }
    catch (std::exception const &x)
    {
        LogError << "Blacklist::load exception ( " << x.what() << " ).";
    }
}

bool Blacklist::contains(std::string &host_name)
{
    LogSpam << "Blacklist::contains( " << host_name << " )";
    grab aholdof(lock_);

    std::transform(host_name.begin(), host_name.end(), host_name.begin(), ::tolower);

    BlacklistSet::iterator fit = blacklistSet_.find(host_name);
    bool blacklisted = fit != blacklistSet_.end();
    if (blacklisted)
    {
        ++countBlacklisted;
        LogNotice << "Blacklist::contains ( " << host_name << " ) BLACKLISTED!";
    }
    return blacklisted;
}

Blacklist::~Blacklist()
{
}

