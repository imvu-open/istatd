
#if !defined(daemon_AllKeys_h)
#define daemon_AllKeys_h

//  We never remove keys once added!
//  This allows us to do lockless linked lists.

#include "threadfunc.h"
#include <boost/noncopyable.hpp>
#include <string>
#include <list>
#include <tr1/unordered_map>

class AllKeys;

class KeyMatch
{
public:
    KeyMatch();
    void operator()(std::string const &str);
    void extract(std::string const &pat, std::list<std::pair<std::string, bool> > &oList);
public:
    typedef std::tr1::unordered_map<std::string, bool> HashMap;
    HashMap ctrs;
};

class AllKeys : public boost::noncopyable
{
public:
    AllKeys();
    ~AllKeys();
    //  add() does *not* check for duplicates! It happily adds them.
    void add(std::string const &str);

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
    void match(std::string const &path, std::list<std::pair<std::string, bool> > &oList);
private:
    //  Once created, one of these records never go away, unless 
    //  ALL records go away. This makes iteration thread safe (at 
    //  the risk of missing any new record created while iterating).
    struct Rec
    {
        inline Rec(std::string const &init) : next(0), data(init) {}
        struct Rec *next;
        std::string data;
    };
    Rec *head_;
    bool dirty_;
    KeyMatch toMatch_;
    lock lock_;
};

#endif  //  daemon_AllKeys_h

