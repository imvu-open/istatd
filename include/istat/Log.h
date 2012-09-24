
#if !defined(istat_Log_h)
#define istat_Log_h

#include <boost/shared_ptr.hpp>
#include <sstream>


namespace istat
{
    //  If you want custom logging, implement LogInstance.
    //  Yor implementation must be thread safe such that 
    //  multiple parallel invocations to output() do not 
    //  mingle the data (but ordering is arbitrary).
    //  rollOver() must close and re-open log files for 
    //  rotation if necessary. It will not be invoked from 
    //  more than one thread at a time.
    class LogInstance
    {
    public:
        virtual void output(char const *data, size_t size) = 0;
        virtual void rollOver() = 0;
        virtual ~LogInstance() {}
    };

    //  LogLevel tells how severe a particular problem is.
    enum LogLevel
    {
        LL_Error,
        LL_Warning,
        LL_Notice,
        LL_Debug,
        LL_Spam
    };

    //  LogFormatterLog class is not directly instantiated by the user, 
    //  but instead used by Log::operator<<()
    //  LogFormatterLog is *not* thread safe for a single instance,
    //  but multiple instances can be used from different threads. THe 
    //  lifetime is expected to be just for the duration of a statement,
    //  so that's OK.
    class LogFormatterLog
    {
    public:
        LogFormatterLog(bool enabled, LogLevel level);
        LogFormatterLog(LogFormatterLog const &o);
        ~LogFormatterLog();
        LogFormatterLog &operator=(LogFormatterLog const &o);
        template<typename T> inline LogFormatterLog &operator<<(T const &t);
    private:
        void reduce();
        void finish();
        //  TODO: look up how to do action-on-delete with boost::shared_ptr<>
        int *ptr_;
        std::stringstream *buf_;
        LogLevel level_;
    };

    //  LogConfig contains logging configuration parameters 
    //  that are applied globally.
    class LogConfig
    {
    public:
        //  Set the highest log level that will be logged.
        static void setLogLevel(LogLevel ll);
        //  Set the highest log level that will be logged to stderr.
        static void setStderrLogLevel(LogLevel ll);
        //  Create a log file instance and make it active.
        static void setOutputFile(char const *path);
        //  Set the output instance in effect.
        static void setOutputInstance(boost::shared_ptr<LogInstance> inst);
        //  Output text to the active log file.
        static void outputToFile(LogLevel ll, char const *data, size_t size);
        //  Close and re-open the output log file(s).
        static void rollOver();
        //  Is the given level enabled?
        static bool levelEnabled(LogLevel l);
        //  What are the current log levels?
        static void getLogLevels(LogLevel &oll, LogLevel &ollStderr);
    };

    // During tests, you may not want stderr to be spammed.
    //  Create an instance of this; it will prevent stderr from 
    //  being printed while it's alive.
    class DisableStderr
    {
    public:
        DisableStderr();
        ~DisableStderr();
    private:
        DisableStderr(DisableStderr const &);
        DisableStderr &operator=(DisableStderr const &);
    };

    //  Log is the main class used by end users. Use operator<<() to 
    //  start writing to the log; the temporary object returned will 
    //  write the data to the log output when it destructs. You can 
    //  create a Log instance per log site, or a Log instance at the 
    //  top of your file, or globally -- whatever you want.
    //  Example usage:
    //  Log(LL_Warning, "stuff") << "my thing" << value;
    class Log
    {
    public:
        Log(LogLevel level, char const *facility = "log");
        ~Log();
        template<typename T>
        LogFormatterLog operator<<(T const &t) const {
            return LogFormatterLog(LogConfig::levelEnabled(level_), level_) << tag_ << ": " << t;
        }
    private:
        LogLevel level_;
        std::string tag_;
    };

    //  Implementation inline of LogFormatterLog::operator<<().
    template<typename T>
    inline LogFormatterLog &LogFormatterLog::operator<<(T const &t) {
        if (buf_) {
            //  each thing is delimited -- this is different from 
            //  ofstream!
            *buf_ << " " << t;
        }
        return *this;
    }
}

#endif  //  istat_Log_h
