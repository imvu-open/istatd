
#include "AllKeys.h"
#include "istat/strfunc.h"
#include "Debug.h"
#include "Logs.h"

#include <istat/Atomic.h>
#include <boost/assign/list_of.hpp>


using boost::assign::map_list_of;


DebugOption allKeys("allKeys");

CounterResponse::CounterDisplayTypeToStringMap CounterResponse::DisplayTypeToString = map_list_of(DisplayTypeGauge, "gauge")(DisplayTypeEvent, "counter")(DisplayTypeAggregate, "aggregate");
AllKeys::AllKeys() :
    head_(0)
{
}

AllKeys::~AllKeys()
{
    Rec *r = head_;
    head_ = 0;
    while (r)
    {
        Rec *d = r;
        r = r->next;
        delete d;
    }
}

void AllKeys::exchange(AllKeys &ak)
{
    Rec* old_head = head_;
    dirty_ = true;
    while (true)
    {
        if(istat::atomic_compare_exchange((void * volatile *)&head_, (void *)old_head, (void *)ak.head_))
        {
            break;
        }

    }
    ak.head_ = old_head;
    toMatch_.clear();

}

void AllKeys::add(std::string const &str, bool isCollated)
{
    dirty_ = true;
    if (allKeys.enabled())
    {
        LogSpam << "AllKeys::add(" << str << ")";
    }
    Rec *r = new Rec(str, isCollated);
    //  Spin, in case we race with some other winner.
    //  Note that it will never livelock, because someone
    //  will write to the head_ each cycle and thus make
    //  progress.
    while (true)
    {
        r->next = head_;
        if (istat::atomic_compare_exchange((void * volatile *)&head_, (void *)r->next, (void *)r))
        {
            return;
        }
    }
}

void AllKeys::delete_all()
{
    AllKeys ak;
    exchange(ak);
}

void AllKeys::match(std::string const &path, std::list<std::pair<std::string, CounterResponse> > &oList)
{
    //  Only one match() can run at once. That should not be a real problem.
    grab aholdof(lock_);
    if (dirty_)
    {
        dirty_ = false;
        foreach(toMatch_);
    }
    toMatch_.extract(path, oList);
}


KeyMatch::KeyMatch()
{
}

void KeyMatch::operator()(CounterData const &cd)
{
    std::string const &str = cd.name;
    //  add knowledge about a particular counter
    HashMap::iterator ptr(ctrs.find(str)), end(ctrs.end());
    if (ptr != end)
    {
        return;
    }

    CounterResponse::CounterDisplayType lt = cd.isCounter ? CounterResponse::DisplayTypeEvent : CounterResponse::DisplayTypeGauge;
    CounterResponse cr(true, lt);

    ctrs[str] = cr;
    std::string prev(str);
    while (true)
    {
        size_t pos = prev.rfind('.');
        if (pos == std::string::npos)
        {
            //  no more pieces to allocate
            break;
        }
        prev.resize(pos);
        ptr = ctrs.find(prev);
        if (ptr == end)
        {
            ctrs[prev] = CounterResponse(false, CounterResponse::DisplayTypeAggregate);
        }
        else
        {
            (*ptr).second = CounterResponse(false, CounterResponse::DisplayTypeAggregate);
            //  I know its parents already exist
            break;
        }
    }
}

void KeyMatch::extract(std::string const &pat, std::list<std::pair<std::string, CounterResponse> > &oList)
{
    for (HashMap::iterator ptr(ctrs.begin()), end(ctrs.end());
        ptr != end;
        ++ptr)
    {
        if (istat::str_pat_match((*ptr).first, pat))
        {
            if (allKeys.enabled())
            {
                LogDebug << "allKeys match " << (*ptr).first << "as" << ((*ptr).second.isLeaf ? "leaf" : "branch");
            }
            oList.push_back(*ptr);
        }
        else
        {
            if (allKeys.enabled())
            {
                LogDebug << "allKeys no match " << (*ptr).first;
            }
        }
    }
}

