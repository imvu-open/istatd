
#if !defined(daemon_EagerConnection_h)
#define daemon_EagerConnection_h

#include "threadfunc.h"

#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/detail/atomic_count.hpp>
#include <boost/version.hpp>

#include "Signal.h"

class EagerConnection;
class UdpConnectionInfo;

class ConnectionInfo : public boost::noncopyable, public boost::enable_shared_from_this<ConnectionInfo>
{
public:
    virtual std::string const &endpointName() = 0;
    virtual EagerConnection *asEagerConnection() = 0;
    virtual UdpConnectionInfo *asUdpConnection() = 0;
    virtual void open() = 0;
    virtual void open(std::string const &dest, std::string const &initialData) = 0;
    virtual void close() = 0;
    virtual bool opened() = 0;
    virtual bool connected() = 0;
    virtual size_t pendingOut() = 0;
    virtual size_t pendingIn() = 0;
    virtual size_t readIn(void *ptr, size_t maxSize) = 0;
    virtual size_t peekIn(void *ptr, size_t maxSize) = 0;
    virtual void consume(size_t n) = 0;
    virtual size_t writeOut(void const *data, size_t size) = 0;
    virtual size_t writeOut(std::string const &str) = 0;

    virtual ~ConnectionInfo() {}
    boost::signals2::signal<void ()> onConnect_;
    boost::signals2::signal<void ()> onDisconnect_;
    boost::signals2::signal<void ()> onData_;
    // onWrite_ will be called with the connection mutex held!
    boost::signals2::signal<void (size_t)> onWrite_;
};

//  EagerConnection will attempt to establish a connection to a remote side.
//  
class EagerConnection : public ConnectionInfo
{
public:
    typedef std::vector<char> buffer;
    ~EagerConnection();
    void open();
    void open(std::string const &dest, std::string const &initialData);
    void close();
    bool opened();
    bool connected();
    size_t pendingOut();
    size_t pendingIn();
    size_t readIn(void *ptr, size_t maxSize);
    size_t peekIn(void *ptr, size_t maxSize);
    void consume(size_t n);
    size_t writeOut(void const *data, size_t size);
    size_t writeOut(std::string const &str);
    void abort();
    boost::asio::ip::tcp::endpoint endpoint() const;
    //  if there's some data, call onData_
    void startRead();

    static boost::shared_ptr<ConnectionInfo> create(boost::asio::io_service &svc);

    virtual std::string const &endpointName();
    virtual EagerConnection *asEagerConnection();
    virtual UdpConnectionInfo *asUdpConnection();
private:
    friend class EagerConnectionFactory;
    friend class FakeEagerConnection;
    EagerConnection(boost::asio::io_service &svc);
    void tryLater();
    void startResolve();
    void on_resolve(boost::system::error_code const &err, boost::asio::ip::tcp::resolver::iterator endpoint);
    void startConnect(boost::asio::ip::tcp::endpoint endpoint);
    void on_connect(boost::system::error_code const &err, boost::asio::ip::tcp::endpoint endpoint);
    void startWrite();
    void on_write(boost::system::error_code const &err, size_t xfer);
    void on_read(boost::system::error_code const &err, size_t xfer);

    void init();

    void resetBackoffOnSuccessfulWrites();

    bool opened_;
    bool writePending_;
    bool readPending_;
    bool tryingLater_;
    time_t tryLaterTime_;
    int backoff_;
    int successfulWritesDuringBackoff_;
    boost::detail::atomic_count interlock_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::deadline_timer timer_;
#if BOOST_VERSION >= 106700
    boost::asio::strand<boost::asio::io_service::executor_type> strand_;
#else
    boost::asio::strand strand_;
#endif
    lock mutex_;
    buffer writeBuf_;
    buffer outgoing_;
    buffer inBuf_;
    buffer incoming_;
    std::string resolveHost_;
    std::string resolvePort_;
    std::string endpointName_;
    boost::asio::ip::tcp::endpoint endpoint_;
    std::string initialData_;

    class Tramp
    {
        boost::shared_ptr<ConnectionInfo> const ci_;
    public:
        Tramp(boost::shared_ptr<ConnectionInfo> const &ci) : ci_(ci) {}
        EagerConnection *operator->() const { return ci_->asEagerConnection(); }
        EagerConnection &operator*() const { return *ci_->asEagerConnection(); }
    };
    friend EagerConnection *get_pointer(Tramp const &t);

    friend struct EagerConnectionEnabler;
};

inline EagerConnection *get_pointer(EagerConnection::Tramp const &t) { return t.operator->(); }

#endif // daemon_EagerConnection_h
