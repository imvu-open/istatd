#if !defined(daemon_IStatWriter_h)
#define daemon_IStatWriter_h

#include <ctime>
#include <string>
#include <cstddef>

class IStatWriter
{
public:
    virtual void record(std::string const &ctr, double value) = 0;
    virtual void record(std::string const &ctr, time_t time, double value) = 0;
    virtual void record(std::string const &ctr, time_t time, double value,
        double valueSquared, double min, double max, size_t count) = 0;
protected:
    virtual ~IStatWriter() {}
};


#endif // daemon_IStatWriter_h

