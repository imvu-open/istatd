
#if !defined(daemon_AllKeys_h)
#define daemon_AllKeys_h

//  We never remove keys once added!
//  This allows us to do lockless linked lists.

#include "threadfunc.h"
#include <boost/noncopyable.hpp>
#include <map>
#include <string>
#include <list>
#include <tr1/unordered_map>
#include "LoopbackCounter.h"

class AllKeys;
struct CounterResponse
{
    enum CounterDisplayType
    {
        DisplayTypeGauge = 0,
        DisplayTypeEvent = 1,
        DisplayTypeAggregate = 2
    };
    typedef std::map<CounterDisplayType, const char*> CounterDisplayTypeToStringMap;
    static CounterDisplayTypeToStringMap DisplayTypeToString;

    bool isLeaf;
    CounterDisplayType counterType;
    CounterResponse(bool isLeaf, CounterDisplayType counterType) : isLeaf(isLeaf), counterType(counterType) {}
    CounterResponse() : isLeaf(true), counterType(DisplayTypeAggregate) {}
};

class KeyMatch
{
public:
    struct CounterData
    {
        std::string name;
        bool isCounter;
        CounterData(std::string const &str, bool isCounter) : name(str), isCounter(isCounter) {}
    };
public:
    KeyMatch();
    void operator()(CounterData const &cd);
    void extract(std::string const &pat, std::list<std::pair<std::string, CounterResponse> > &oList);

    void clear() { ctrs.clear(); }
public:
    typedef std::tr1::unordered_map<std::string, CounterResponse> HashMap;
    HashMap ctrs;
};

class AllKeys : public boost::noncopyable
{
public:
    AllKeys();
    ~AllKeys();
    //  add() does *not* check for duplicates! It happily adds them.
    void add(std::string const &str, bool isCollated);
    void delete_all();
    void exchange(AllKeys &ak);

    //  T is callable(std::string const &str)
    template<typename T> void foreach(T &t) const
    {
        Rec const *r = head_;
        while (r)
        {
            t(r->data);
            r = r->next;
        }
    }
    void match(std::string const &path, std::list<std::pair<std::string, CounterResponse> > &oList);
private:
    //  Once created, one of these records never go away, unless 
    //  ALL records go away. This makes iteration thread safe (at 
    //  the risk of missing any new record created while iterating).
    struct Rec
    {
        inline Rec(std::string const &init, bool isCollated) : next(0), data(init, isCollated) {}
        struct Rec *next;
        KeyMatch::CounterData data;
    };
    Rec *head_;
    bool dirty_;
    KeyMatch toMatch_;
    lock lock_;
};

#endif  //  daemon_AllKeys_h

