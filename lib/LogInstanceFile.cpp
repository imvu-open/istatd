
#include "LogInstanceFile.h"
#include <stdexcept>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <boost/system/system_error.hpp>
#include <boost/filesystem.hpp>
#include <boost/version.hpp>

namespace istat
{
    LogInstanceFile::LogInstanceFile(char const *fn) :
        path_(fn),
        fd_(-1)
    {
        reopen();
    }

    LogInstanceFile::~LogInstanceFile()
    {
        if (fd_ != -1)
        {
            close(fd_);
            fd_ = -1;
        }
    }

    void LogInstanceFile::output(char const *txt, size_t sz)
    {
        grab aholdof(lock_);
        ssize_t result;
        result = write(fd_, txt, sz);
        if (result<0 || (size_t)result!=sz)
        {
            //  error logging of last resort
            std::cerr << "Short write: " << path_ << std::endl;
        }
    }

    void LogInstanceFile::rollOver()
    {
        reopen();
    }

    void LogInstanceFile::reopen()
    {
        grab aholdof(lock_);
        if (fd_ != -1)
        {
            close(fd_);
            fd_ = -1;
        }
        fd_ = open(path_.c_str(), O_CREAT | O_APPEND | O_RDWR, 0666);
        if (fd_ < 0)
        {
            std::ostringstream os;

            char filename[1024];
            os << "Could not open log file '" << path_ << "': "
#if BOOST_VERSION >= 106700
               << boost::system::error_code(errno, boost::system::system_category()).message()
#else
               << boost::system::error_code(errno, boost::system::get_system_category()).message()
#endif
               << std::endl
               << "cwd: ";
               
            char* p = getcwd(filename, 1024);
            if(!p)
            {
#if BOOST_VERSION >= 106700
                os << boost::system::error_code(errno, boost::system::system_category()).message()
#else
                os << boost::system::error_code(errno, boost::system::get_system_category()).message()
#endif
                    << std::endl;
            }
            else
            {
                os << p << std::endl;
            }


            throw std::runtime_error(os.str());
        }
    }
}

