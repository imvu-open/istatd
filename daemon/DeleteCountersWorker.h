#if !defined(daemon_DeleteCountersWorker_h)
#define daemon_DeleteCountersWorker_h

#include "EagerConnection.h"
#include "IComplete.h"
#include <boost/shared_ptr.hpp>
#include <list>
#include <string>

class StatServer;

class DeleteCountersWorker : public IComplete
{
public:
    DeleteCountersWorker(
        std::string const *ptr,
        std::string const *end,
        StatServer *ssp,
        boost::shared_ptr<ConnectionInfo> const &ecp) :
        counters_(ptr, end),
        statServer_(ssp),
        conn_(ecp)
    {
    }
    std::list<std::string> counters_;
    StatServer *statServer_;
    boost::shared_ptr<ConnectionInfo> conn_;
    void go();
    void delete_one_counter(std::string const &name);

    void on_complete();
};

#endif  //  daemon_DeleteCountersWorker_h
