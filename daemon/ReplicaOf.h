
#if !defined(daemon_ReplicaOf_h)
#define daemon_ReplicaOf_h

#include "EagerConnection.h"
#include "PduProtocol.h"
#include "ReplicaPdus.h"

class IStatStore;

struct ReplicaOfInfo
{
    int64_t queueLength;
    std::string source;
    bool connected;
};

class ReplicaOf
{
public:
    ReplicaOf(std::string const &name, boost::asio::io_service &svc, boost::shared_ptr<IStatStore> const &ss);
    ~ReplicaOf();
    void go();
    void getInfo(ReplicaOfInfo &oInfo);
private:
    void on_connection();
    void on_discover(PduDiscover const &disc);
    void on_data(PduData const &data);
    void on_error(PduError const &err);
    void on_disconnect();
    void on_pdu();
    void setupProtocol();
    void emit(void const *data, size_t sz);
    std::string name_;
    boost::shared_ptr<ConnectionInfo> conn_;
    boost::shared_ptr<IStatStore> ss_;
    boost::asio::deadline_timer timer_;
    PduReaderActor *pduSrc_;
    PduProtocol proto_;
};


#endif  //  daemon_ReplicaOf_h
