
#if !defined(daemon_AdminServer_h)
#define daemon_AdminServer_h

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <istat/Env.h>
#include "LoopbackCounter.h"

class IHttpServerInfo;
class StatServer;
class EagerConnectionFactory;
class ReplicaServer;
class ReplicaOf;

class AdminServer
{
public:
    AdminServer(unsigned int port, std::string listen_address, boost::asio::io_service &svc, IHttpServerInfo *hsp, StatServer *ssp,
        ReplicaServer *rs, ReplicaOf *ro, int listenOverflowBacklog);
    ~AdminServer();

    void on_connection();

    boost::asio::io_service &svc_;
    IHttpServerInfo *hsp_;
    StatServer *ssp_;
    ReplicaServer *rs_;
    ReplicaOf *ro_;
    LoopbackCounter numAdminConnections_;
    LoopbackCounter numAdminCommands_;

    boost::shared_ptr<EagerConnectionFactory> fac_;
    int listenOverflowBacklog_;
};


#endif // daemom_AdminServer_h
