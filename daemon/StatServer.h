
#if !defined(daemon_StatServer_h)
#define daemon_StatServer_h

#include <istat/Mmap.h>
#include <istat/Bucket.h>

#include <string>
#include <tr1/unordered_map>

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/detail/atomic_count.hpp>

#include "EagerConnection.h"
#include "EagerConnectionFactory.h"
#include "LoopbackCounter.h"
#include "MetaInfo.h"
#include "IStatStore.h"


class IStatStore;
class IComplete;
using boost::asio::ip::tcp;

void test_handleInputData();
void test_to_double();


class IStatServer : public boost::noncopyable
{
    virtual void syncAgent(IComplete *complete) = 0;
    virtual void getConnected(std::vector<MetaInfo> &oConnected) = 0;
};


class AgentFlushRequest : public boost::noncopyable
{
public:
    AgentFlushRequest(size_t n, IComplete *comp);
    ~AgentFlushRequest();
    bool completed(size_t n);
private:
    size_t n_;
    IComplete *comp_;
};

class UdpConnectionInfo : public ConnectionInfo
{
public:
    UdpConnectionInfo() {}
    std::string const &endpointName()
    {
        if (!endpointName_.size())
        {
            std::stringstream ss;
            ss << endpoint_;
            endpointName_ = ss.str();
        }
        return endpointName_;
    }
    EagerConnection *asEagerConnection()
    {
        return 0;
    }
    UdpConnectionInfo *asUdpConnection()
    {
        return this;
    }
    boost::asio::ip::udp::endpoint endpoint_;
    std::string endpointName_;
    virtual void open() {}
    virtual void open(std::string const &dest, std::string const &initialData) {}
    virtual void close() {}
    virtual bool opened() { return false; }
    virtual bool connected() { return false; }
    virtual size_t pendingOut() { return 0; }
    virtual size_t pendingIn() { return 0; }
    virtual size_t readIn(void *ptr, size_t maxSize) { return 0; }
    virtual size_t peekIn(void *ptr, size_t maxSize) { return 0; }
    virtual void consume(size_t n) {}
    virtual void writeOut(void const *data, size_t size) {}
    virtual void writeOut(std::string const &str) {}
};

class StatServer : IStatServer
{
public:
    StatServer(int statPort, std::string listenAddr, std::string const &agentFw,
        time_t agentInterval,
        boost::asio::io_service &svc,
        boost::shared_ptr<IStatStore> &statStore);
    ~StatServer();
    inline bool hasStore() const { return hasStatStore_; }
    inline bool hasAgent() const { return !agent_.empty(); }
    inline boost::asio::io_service &service() { return svc_; }
    inline boost::shared_ptr<IStatStore> store() const
        {
            return hasStatStore_ ? statStore_ : boost::shared_ptr<IStatStore>((IStatStore *)NULL);
        }
    void handleCmd(std::string const &cmd, boost::shared_ptr<ConnectionInfo> const &ec);
    void counters(int64_t *oConnects, int64_t *oDrops);
    void syncAgent(IComplete *complete);
    //  Copy data while the lock is held
    void getConnected(std::vector<MetaInfo> &oConnected);
    void purgeOldMetaRecords(size_t maxOld, time_t maxAge);
	
	void ignore(std::string const& name);
private:
    typedef std::tr1::unordered_map<void *, boost::shared_ptr<MetaInfo> > InfoHashMap;

    void startResolveAgent();
    void on_forwardData();
    void on_connection();
    void on_forwardWrite(size_t n);
    void on_inputData(boost::shared_ptr<ConnectionInfo> ec);
    void on_inputLost(boost::shared_ptr<ConnectionInfo> ec);
    void recvOneUdp();
    void on_udp_recv(boost::system::error_code const &err, size_t bytes);
    void forwardCmd(std::string cmd);
    void reschedule_udp_recv(bool success, const std::string &reason);
    size_t agentQueueSize();
    void startReport();
    void onReport(boost::system::error_code const &err);

    void handle_counter_cmd(std::string const &cmd);
    bool handle_event_cmd(std::string const &cmd);
    bool handle_meta_info(std::string const &cmd, boost::shared_ptr<ConnectionInfo> const &ec);
    bool handle_reserved_cmd(std::string const &cmd, boost::shared_ptr<ConnectionInfo> const &ec);
    //  separate 3 handle_record cases because StatStore wants to insert time 
    //  and calculate counters, but shouldn't know about forwarding.
    void handle_record(std::string const &ctr, double val);
    void handle_record(std::string const &ctr, time_t time, double val);
    void handle_record(std::string const &ctr, time_t time, double val, double sumSq, double min, double max, size_t n);
    void handle_forward(std::string const &ctr, time_t time, double val, double sumSq, double min, double max, size_t n);
    void on_forwardTimer();
    void clearForward();

    boost::shared_ptr<MetaInfo> metaInfo(boost::shared_ptr<ConnectionInfo> const &ec);

    int port_;
    int backoffSeconds_;
    time_t forwardInterval_;
    std::string agent_;

    bool hasStatStore_;
    boost::shared_ptr<IStatStore> statStore_;

    boost::shared_ptr<ConnectionInfo> forward_;
    EagerConnectionFactory input_;
    boost::asio::io_service &svc_;
    boost::asio::ip::udp::socket udpSocket_;
    boost::shared_ptr<UdpConnectionInfo> udpEndpoint_;
    boost::asio::deadline_timer udpTimer_;
    boost::asio::deadline_timer reportTimer_;
    char udpBuffer_[4096];
    int udpBackoffMs_;
    LoopbackCounter nConnects_;
    LoopbackCounter nDrops_;
    LoopbackCounter nConnected_;
    LoopbackCounter reservedCommands_;
    LoopbackCounter badCommands_;
    int64_t numConnected_;
    ::lock agentMutex_;
    std::list<AgentFlushRequest *> agentFlushRequests_;
    ::lock metaMutex_;
    InfoHashMap metaInfo_;
    time_t metaInterval_;
    ::lock forwardMutex_;
    std::tr1::unordered_map<std::string, istat::Bucket> forwardCounters_;
    boost::asio::deadline_timer forwardTimer_;
};

#endif  //  daemon_StatServer_h

