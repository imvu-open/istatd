
#include "ReplicaServer.h"
#include "ReplicaPdus.h"
#include "IStatStore.h"
#include "Logs.h"
#include "EagerConnection.h"
#include "PduProtocol.h"

#include <istat/Atomic.h>

#include <boost/bind.hpp>
#include <boost/signals.hpp>
#include <iostream>


class ReplicaConnection
{
public:

    ReplicaConnection(boost::shared_ptr<ConnectionInfo> &conn, ReplicaServer *rs) :
        conn_(conn),
        pduSrc_(new PduReaderActor(conn)),
        rs_(rs),
        proto_("start")
    {
        rs_->numConnectionsGauge_.value(istat::atomic_add(&rs_->numConnections_, 1));
        setupProtocol();
    }

    void go()
    {
        pduSrc_->onPdu_.connect(boost::bind(&ReplicaConnection::on_pdu, this));
        conn_->onDisconnect_.connect(boost::bind(&ReplicaConnection::on_disconnect, this));
        conn_->asEagerConnection()->startRead();
    }

    ~ReplicaConnection()
    {
        rs_->numConnectionsGauge_.value(istat::atomic_add(&rs_->numConnections_, -1));
        delete pduSrc_;
    }

    void on_pdu()
    {
        try
        {
            PduHeader pdu;
            while (pduSrc_->nextPdu(pdu))
            {
                proto_.onPdu(pdu.type, pdu.size, pdu.size ? &pdu.payload[0] : NULL);
            }
        }
        catch (PduProtocolException const &x)
        {
            LogError << "Protocol Exception from" << conn_->endpointName() << ":" << x.what();
            conn_->close();
        }
    }

    void emit(void const *data, size_t sz)
    {
        conn_->writeOut(data, sz);
    }

    void on_iterate(istat::Header const &hdr)
    {
    std::cerr << hdr.name << std::endl;
        LogSpam << "ReplicaConnection::on_iterate(" << hdr.name << ")";
    }

    void on_connect(PduConnect const &conn)
    {
        if (conn.Version != ReplicateProtocolVersion1)
        {
            LogWarning << "Unsupported protocol version" << conn.Version << "from" << conn_->endpointName();
            char buf[1024];
            size_t sz = 0;
            PduError::make(buf, 1024, sz, "Unsupported protocol version");
            emit(buf, sz);
            return;
        }
        LogNotice << "New replica connection from" << conn.ServerId.str() << "at" << conn_->endpointName();
        UniqueId id;
        rs_->ss_->getUniqueId(id);
        if (id == conn.ServerId)
        {
            LogWarning << "Ignoring connection from myself at" << conn_->endpointName();
            char buf[1024];
            size_t sz = 0;
            PduError::make(buf, 1024, sz, "I refuse to talk to myself");
            emit(buf, sz);
            return;
        }
        iterate_ = rs_->ss_->iterateSignal.connect(boost::bind(&ReplicaConnection::on_iterate, this, _1));
    }

    void on_request(PduRequest const &req)
    {
        LogDebug << "Replication request from" << conn_->endpointName() << ":" << req.fromTime << req.numBuckets << req.getName();
        //  well, that's nice :-)
    }

    void setupProtocol()
    {
        PduProtocolState *start = proto_.state("start");
        PduProtocolState *connected = proto_.state("connected");
        start->bindPdu<PduConnect>(boost::bind(&ReplicaConnection::on_connect, this, _1));
        connected->bindPdu<PduRequest>(boost::bind(&ReplicaConnection::on_request, this, _1));
    }

    void on_disconnect()
    {
        self_ = boost::shared_ptr<ReplicaConnection>((ReplicaConnection *)0);
    }

    boost::shared_ptr<ReplicaConnection> self_;
    boost::shared_ptr<ConnectionInfo> conn_;
    PduReaderActor *pduSrc_;
    ReplicaServer *rs_;
    PduProtocol proto_;
    boost::signals::connection iterate_;
};


ReplicaServer::ReplicaServer(int port, std::string listen_address, boost::asio::io_service &svc, boost::shared_ptr<IStatStore> &ss) :
    port_(port),
    listen_(svc),
    ss_(ss),
    numConnectionsGauge_("replica.connections", TypeGauge),
    numConnections_(0),
    replicaRequestEvents_("replica.events", TypeEvent)
{
    listen_.onConnection_.connect(boost::bind(&ReplicaServer::on_connection, this));
    listen_.listen(port, listen_address);
}

ReplicaServer::~ReplicaServer()
{
}

void ReplicaServer::getInfo(ReplicaServerInfo &oInfo)
{
    oInfo.port = (unsigned short)port_;
    oInfo.numConnections = numConnections_;
}

void ReplicaServer::on_connection()
{
    boost::shared_ptr<ConnectionInfo> conn;
    while (!!(conn = listen_.nextConn()))
    {
        LogWarning << "Connection from" << conn->endpointName() << "in ReplicaServer";
        boost::shared_ptr<ReplicaConnection> other(new ReplicaConnection(conn, this));
        other->self_ = other;
        other->go();
    }
}

