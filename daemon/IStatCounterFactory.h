
#if !defined(daemon_IStatCounterFactory_h)
#define daemon_IStatCounterFactory_h

#include "IStatCounter.h"
#include <boost/shared_ptr.hpp>


class IStatCounterFactory
{
public:
    virtual ~IStatCounterFactory() {};

    virtual boost::shared_ptr<IStatCounter> create(std::string const &name, bool isCollated, time_t zeroTime, bool onlyExisting) = 0;
    virtual bool statCounterFilesExist(std::string const &pathName) = 0;
    virtual std::string &rootPath() = 0;
};

#endif  //  daemon_IStatCounterFactory_h
