
#if !defined(daemon_FakeEagerConnection_h)
#define daemon_FakeEagerConnection_h

#include <vector>

#include "EagerConnection.h"


class FakeEagerConnection : public EagerConnection
{
public:
    inline FakeEagerConnection(boost::asio::io_service &ios) :
        EagerConnection(ios)
    {
    }
};

#endif  //  daemon_FakeEagerConnection_h
