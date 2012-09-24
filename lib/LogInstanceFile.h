

#if !defined(lib_LogInstanceFile_h)
#define lib_LogInstanceFile_h

#include <istat/Log.h>
#include <boost/thread/locks.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <string>

namespace istat
{
    typedef boost::recursive_mutex lock;
    typedef boost::lock_guard<lock> grab;

    class LogInstanceFile  : public LogInstance
    {
    public:
        LogInstanceFile(char const *fn);
        ~LogInstanceFile();
        virtual void output(char const *data, size_t size);
        void rollOver();
    private:
        void reopen();
        std::string path_;
        lock lock_;
        int fd_;
    };

}

#endif  //  lib_LogInstanceFile_h

