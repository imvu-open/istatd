
#if !defined(daemon_ReplicaServer_h)
#define daemon_ReplicaServer_h

#include <boost/noncopyable.hpp>

#include "EagerConnectionFactory.h"
#include "LoopbackCounter.h"


class IStatStore;


struct ReplicaServerInfo
{
    int64_t numConnections;
    unsigned short port;
};

class ReplicaServer : public boost::noncopyable
{
public:
    ReplicaServer(int port, std::string listen_address, boost::asio::io_service &svc, boost::shared_ptr<IStatStore> &ss);
    ~ReplicaServer();
    
    void getInfo(ReplicaServerInfo &oInfo);

private:
    friend class ReplicaConnection;
    int port_;
    EagerConnectionFactory listen_;
    boost::shared_ptr<IStatStore> ss_;
    LoopbackCounter numConnectionsGauge_;
    int64_t numConnections_;
    LoopbackCounter replicaRequestEvents_;

    void on_connection();
};

#endif  //  daemon_ReplicaServer_h

