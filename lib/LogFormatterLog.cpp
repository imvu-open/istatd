
#include <istat/Log.h>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <time.h>
#include <unistd.h>
#include <stdio.h>

namespace istat
{

    static char pidstr[20];

    LogFormatterLog::LogFormatterLog(bool enabled, LogLevel level) :
        ptr_(enabled ? new int(1) : 0),
        buf_(enabled ? new std::stringstream() : 0),
        level_(level)
    {
        snprintf(pidstr, 20, " (%d)", getpid());
    }
    
    LogFormatterLog::LogFormatterLog(LogFormatterLog const &lo) :
        ptr_(lo.ptr_),
        buf_(lo.buf_),
        level_(lo.level_)
    {
        if (ptr_)
        {
            ++*ptr_;
        }
    }

    LogFormatterLog::~LogFormatterLog()
    {
        reduce();
    }

    LogFormatterLog &LogFormatterLog::operator=(LogFormatterLog const &lo)
    {
        reduce();
        ptr_ = lo.ptr_;
        buf_ = lo.buf_;
        level_ = lo.level_;
        if (ptr_)
        {
            ++*ptr_;
        }
        return *this;
    }

    void LogFormatterLog::reduce()
    {
        if (ptr_)
        {
            if (!--*ptr_)
            {
                finish();
            }
        }
    }

    void LogFormatterLog::finish()
    {
        *buf_ << std::endl;
        std::string s(boost::posix_time::to_iso_extended_string(boost::posix_time::second_clock::local_time()));
        s += pidstr;
        s += buf_->str();
        LogConfig::outputToFile(level_, s.c_str(), s.size());
        delete ptr_;
        ptr_ = 0;
        delete buf_;
        buf_ = 0;
    }
}
