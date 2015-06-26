
#include <istat/Log.h>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <time.h>
#include <unistd.h>
#include <stdio.h>

namespace istat
{
    static char pidstr[20];

    void LogFormatterLog::BufDeleter::operator() (std::stringstream *ssbuf)
    {
        if (ssbuf)
        {
            *ssbuf << std::endl;
            std::string s(boost::posix_time::to_iso_extended_string(boost::posix_time::second_clock::local_time()));
            s += pidstr;
            s += ssbuf->str();
            LogConfig::outputToFile(level_, s.c_str(), s.size());
            delete ssbuf;
            ssbuf = 0;
         }
    }

    LogFormatterLog::LogFormatterLog(bool enabled, LogLevel level) :
        buf_(boost::shared_ptr<std::stringstream>(enabled ? new std::stringstream() : NULL, LogFormatterLog::BufDeleter(level)))
    {
        snprintf(pidstr, 20, " (%d)", getpid());
    }
}
