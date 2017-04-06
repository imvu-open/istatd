#include "StatCounterFactory.h"
#include "StatCounter.h"
#include "Logs.h"
#include "istat/strfunc.h"

#include <boost/make_shared.hpp>
#include <string>


StatCounterFactory::StatCounterFactory(std::string const &root_path, istat::Mmap *mm, RetentionPolicy &rp) :
    rootPath_(root_path),
    mm_(mm),
    policy_(rp)
{
}

bool StatCounterFactory::statCounterFilesExist(std::string const &pathName)
{
    for(size_t i = 0; i != policy_.countIntervals(); ++i)
    {
        RetentionInterval const &ri = policy_.getInterval(i);
        if (!boost::filesystem::exists(pathName + "/" + ri.name))
        {
            return false;
        }
    }
    return true;
}


boost::shared_ptr<IStatCounter> StatCounterFactory::create(std::string const &name, bool isCollated, time_t zeroTime, bool onlyExisting)
{
    std::string fullpath = rootPath_ + "/" + istat::counter_filename(name);
    if (onlyExisting && !statCounterFilesExist(fullpath))
    {
        return boost::shared_ptr<IStatCounter>((IStatCounter *)0);
    }
    else
    {
        return boost::make_shared<StatCounter>(fullpath, isCollated, zeroTime, mm_, policy_);
    }
}
