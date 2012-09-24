
#include "AllKeys.h"
#include "istat/strfunc.h"
#include "Debug.h"
#include "Logs.h"

#include <istat/Atomic.h>



DebugOption allKeys("allKeys");

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

void AllKeys::add(std::string const &str)
{
    dirty_ = true;
    if (allKeys.enabled())
    {
        LogNotice << "AllKeys::add(" << str << ")";
    }
    Rec *r = new Rec(str);
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

void AllKeys::match(std::string const &path, std::list<std::pair<std::string, bool> > &oList)
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

void KeyMatch::operator()(std::string const &str)
{
    //  add knowledge about a particular counter
    HashMap::iterator ptr(ctrs.find(str)), end(ctrs.end());
    if (ptr != end)
    {
        return;
    }
    ctrs[str] = true;
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
            ctrs[prev] = false;
        }
        else
        {
            (*ptr).second = false;
            //  I know its parents already exist
            break;
        }
    }
}

void KeyMatch::extract(std::string const &pat, std::list<std::pair<std::string, bool> > &oList)
{
    for (HashMap::iterator ptr(ctrs.begin()), end(ctrs.end());
        ptr != end;
        ++ptr)
    {
        if (istat::str_pat_match((*ptr).first, pat))
        {
            if (allKeys)
            {
                LogNotice << "allKeys match " << (*ptr).first << "as" << ((*ptr).second ? "leaf" : "branch");
            }
            oList.push_back(*ptr);
        }
        else
        {
            if (allKeys)
            {
                LogNotice << "allKeys no match " << (*ptr).first;
            }
        }
    }
}

