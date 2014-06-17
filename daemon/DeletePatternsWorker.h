#if !defined(daemon_DeletePatternsWorker_h)
#define daemon_DeletePatternsWorker_h

#include "EagerConnection.h"
#include "IComplete.h"
#include <boost/shared_ptr.hpp>
#include <list>
#include <string>

class StatServer;

class DeletePatternsWorker : public IComplete
{
public:
    DeletePatternsWorker(
        std::string const *ptr,
        std::string const *end,
        StatServer *ssp,
        boost::shared_ptr<ConnectionInfo> const &ecp) :
        patterns_(ptr, end),
        statServer_(ssp),
        conn_(ecp)
    {
    }
    std::list<std::string> patterns_;
    StatServer *statServer_;
    boost::shared_ptr<ConnectionInfo> conn_;
    void go();
    void delete_one_pattern(std::string const &pattern);

    void on_complete();
};

#endif  //  daemon_DeletePatternsWorker_h
