
#include "ReplicaOf.h"
#include "Logs.h"
#include "IStatStore.h"
#include "Debug.h"

#include <boost/bind/bind.hpp>



/* The replication protocol is simple.
 *
 * PDUs are enclosed in framing of a "type" word (4 bytes) and a "payload length" word (4 bytes).
 * These are little-endian, because that's what the data is
 *
 * The replication master will send a "discover" PDU for a counter after that file is opened 
 * (and potentially more than once). This lets the slave build up the appropriate set of files 
 * to replicate.
 * The slave will send a "commit file time" message to the master. This asks the master to 
 * send any data for the given file that's after the given time. This may not be all the data, 
 * but that's OK as the replica slave will ask for data again later.
 * The master will send a "file data" PDU with data for a time range for a file, which is just 
 * a chunk of buckets, preceded by the file name. This is only sent in response to a request 
 * for file data from the slave. The data *may* overlap previously sent data, and *may* have gaps.
 *
 * The master should typically send the "discover" message when it gets around to flushing each 
 * file for the first time, to spread the traffic around a little bit. Similarly, the slave 
 * should probably request counter data with some amount of pacing, to avoid totally flooding 
 * the connection during start-up.
 *
 * A PID and a random number (from /dev/urandom) is used to prevent a process from communicating 
 * with itself over this protocol.
 */
 

DebugOption debugReplicaOf("replica_of");
 
ReplicaOf::ReplicaOf(std::string const &name, boost::asio::io_service &svc, boost::shared_ptr<IStatStore> const &ss) :
    name_(name),
    conn_(EagerConnection::create(svc)),
    ss_(ss),
    timer_(svc),
    pduSrc_(0),
    proto_("start")
{
}

ReplicaOf::~ReplicaOf()
{
}

void ReplicaOf::go()
{
    LogSpam << "ReplicaOf::go()";
    conn_->onConnect_.connect(boost::bind(&ReplicaOf::on_connection, this));
    pduSrc_ = new PduReaderActor(conn_);
    pduSrc_->onPdu_.connect(boost::bind(&ReplicaOf::on_pdu, this));
    conn_->open(name_, "");
}

void ReplicaOf::getInfo(ReplicaOfInfo &oInfo)
{
    oInfo.source = name_;
    oInfo.connected = conn_->connected();
    oInfo.queueLength = 0;
}

void ReplicaOf::on_connection()
{
    if (!debugReplicaOf.enabled())
    {
        LogDebug << "ReplicaOf::on_connection() from" << conn_->endpointName();
    }
    char buf[1024];
    size_t sz = 0;
    UniqueId uid;
    ss_->getUniqueId(uid);
    if (debugReplicaOf.enabled())
    {
        LogNotice << "replica connection from" << conn_->endpointName() << "my uid" << uid.str();
    }
    PduConnect::make(buf, 1024, sz, ReplicateProtocolVersion1, uid);
    emit(buf, sz);
}

void ReplicaOf::emit(void const *data, size_t sz)
{
    conn_->writeOut(data, sz);
}

void ReplicaOf::on_pdu()
{
    if (debugReplicaOf.enabled())
    {
        LogDebug << "replica pdu from" << conn_->endpointName();
    }
    else
    {
        LogSpam << "ReplicaOf::on_pdu() from" << conn_->endpointName();
    }
}

