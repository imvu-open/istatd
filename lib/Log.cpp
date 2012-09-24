
#include "LogInstanceFile.h"

#include <istat/Log.h>
#include <istat/Atomic.h>

#include <iostream>
#include <string.h>
#include <stdio.h>


namespace istat
{

    int64_t stderrDisableCount_;

    DisableStderr::DisableStderr()
    {
        istat::atomic_add(&stderrDisableCount_, 1);
    }

    DisableStderr::~DisableStderr()
    {
        istat::atomic_add(&stderrDisableCount_, -1);
    }

    class StderrLog : public LogInstance
    {
    public:
        virtual void output(char const *text, size_t size)
        {
            if (stderrDisableCount_ == 0)
            {
                std::cerr << std::string(text, size);
            }
        }
        virtual void rollOver()
        {
        }
    };

    static lock logFileLock_;
    static boost::shared_ptr<LogInstance> cerrLog_(
        boost::shared_ptr<LogInstance>(new StderrLog()));
    static boost::shared_ptr<LogInstance> logFile_;
    static std::string g_logFileName_ = "./istat.log";
    static LogLevel g_logLevel_ = LL_Warning;
    static LogLevel g_stderrLogLevel_ = LL_Error;
    static int logN_;
    static int sameCount_;
    static size_t sameSize_;
    static char sameBuf_[1024];


    static boost::shared_ptr<LogInstance> getOrMakeLogFile()
    {
        //  must be called with lock held
        if (!logFile_)
        {
            logFile_ = boost::shared_ptr<LogInstance>(new LogInstanceFile(g_logFileName_.c_str()));
        }
        return logFile_;
    }


    static void setLogFileName(char const *name)
    {
        grab aholdof(logFileLock_);
        logFile_.reset();
        g_logFileName_ = name;
    }

    Log::Log(LogLevel level, char const *facility) :
        level_(level),
        tag_(facility)
    {
    }

    Log::~Log()
    {
    }



    void LogConfig::setLogLevel(LogLevel ll)
    {
        g_logLevel_ = ll;
    }

    void LogConfig::setStderrLogLevel(LogLevel ll)
    {
        g_stderrLogLevel_ = ll;
    }

    void LogConfig::getLogLevels(LogLevel &a, LogLevel &b)
    {
        a = g_logLevel_;
        b = g_stderrLogLevel_;
    }

    bool LogConfig::levelEnabled(LogLevel ll)
    {
        return g_logLevel_ >= ll || g_stderrLogLevel_ >= ll;
    }

    void LogConfig::setOutputFile(char const *path)
    {
        setLogFileName(path);
    }

    void LogConfig::setOutputInstance(boost::shared_ptr<LogInstance> inst)
    {
        grab aholdof(logFileLock_);
        logFile_ = inst;
    }

    static bool msg_is_duplicate(char const *data, size_t size)
    {
        //  super hack!
        //  The date/time is the first 20 characters.
        //  Don't count that as part of the equality
        if (size == sameSize_ && size > 20 && !strncmp(data, "2011", 4))
        {
            if (!memcmp(&sameBuf_[20], data + 20, size - 20))
            {
                return true;
            }
        }
        return false;
    }

    static void remember_for_duplicate(char const *data, size_t size)
    {
        if (sameCount_ > 0)
        {
            snprintf(sameBuf_, sizeof(sameBuf_), "... repeated %d more time%s ...\n",
                sameCount_, (sameCount_ > 1) ? "s" : "");
            getOrMakeLogFile()->output(sameBuf_, strlen(sameBuf_));
            sameCount_ = 0;
        }
        if (size > 0 && size < 1024)
        {
            memcpy(sameBuf_, data, size);
            sameSize_ = size;
        }
        else
        {
            sameSize_ = 0;
        }
    }

    static void flush_duplicate()
    {
        remember_for_duplicate(0, 0);
    }

    void LogConfig::outputToFile(LogLevel ll, char const *data, size_t size)
    {
        grab aholdof(logFileLock_);
        if (ll <= g_stderrLogLevel_)
        {
            cerrLog_->output(data, size);
        }
        ++logN_;
        if (!(logN_ & 1023))
        {
            rollOver();
        }
        if (msg_is_duplicate(data, size))
        {
            ++sameCount_;
            return;
        }
        remember_for_duplicate(data, size);
        getOrMakeLogFile()->output(data, size);
    }

    void LogConfig::rollOver()
    {
        grab aholdof(logFileLock_);
        if (!!logFile_)
        {
            flush_duplicate();
            logFile_->rollOver();
        }
    }
}

