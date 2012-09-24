#include "StatCounterFactory.h"
#include "StatCounter.h"
#include "Logs.h"
#include "istat/strfunc.h"

#include <string>


StatCounterFactory::StatCounterFactory(std::string const &root_path, istat::Mmap *mm, RetentionPolicy &rp, RetentionPolicy &xmap) :
    rootPath_(root_path),
    mm_(mm),
    policy_(rp),
    xmaPolicy_(xmap)
{
}

boost::shared_ptr<IStatCounter> StatCounterFactory::create(std::string const &name, bool isCollated, time_t zeroTime)
{
    std::string fullpath = rootPath_ + "/" + istat::counter_filename(name);
    return boost::shared_ptr<IStatCounter>(new StatCounter(fullpath, isCollated, zeroTime, mm_, policy_, xmaPolicy_));
}
