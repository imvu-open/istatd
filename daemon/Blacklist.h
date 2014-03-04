#if !defined(daemon_Blacklist_h)
#define daemon_Blacklist_h

#include <string>
#include <tr1/unordered_set>

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>


#include "threadfunc.h"


class Blacklist : public boost::noncopyable
{
public:
    struct Configuration
    {
        std::string path;
        int16_t period;

        bool use() { return !path.empty() && period > 0; }
    };
    Blacklist(boost::asio::io_service &svc, Configuration &cfg);
    ~Blacklist();

    bool contains(std::string &host_name);

private:
    typedef std::tr1::unordered_set<std::string> BlacklistSet;

    void startRead();
    void onRead(boost::system::error_code const &err);

    void load();

    boost::asio::io_service &svc_;
    boost::asio::deadline_timer tryReadBlacklistTimer_;

    BlacklistSet blacklistSet_;
    lock lock_;
    std::string blacklistPath_;
    std::time_t lastModifiedTime_;
    int16_t period_;

};

#endif  //  daemon_Blacklist_h
