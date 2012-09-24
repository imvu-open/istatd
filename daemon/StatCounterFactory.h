
#if !defined(daemon_StatCounterFactory_h)
#define daemon_StatCounterFactory_h

#include <string>
#include <istat/Mmap.h>
#include "IStatCounterFactory.h"
#include "Retention.h"


class StatCounterFactory : public IStatCounterFactory
{
public:
    StatCounterFactory(std::string const &root_path, istat::Mmap *mm, RetentionPolicy &rp, RetentionPolicy &xmap);

    virtual boost::shared_ptr<IStatCounter> create(std::string const &name, bool isCollated, time_t zeroTime);

private:
    std::string rootPath_;
    istat::Mmap *mm_;
    RetentionPolicy &policy_;
    RetentionPolicy &xmaPolicy_;

};

#endif  //  daemon_StatCounterFactory_h
