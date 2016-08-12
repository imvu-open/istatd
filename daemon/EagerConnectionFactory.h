
#if !defined(daemon_EagerConnectionFactory_h)
#define daemon_EagerConnectionFactory_h

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>

#include "Signal.h"

class EagerConnection;
class ConnectionInfo;

class EagerConnectionFactory
{
public:
    EagerConnectionFactory(boost::asio::io_service &svc);
    ~EagerConnectionFactory();
    void listen(int port, std::string listen_address);
    boost::shared_ptr<ConnectionInfo> nextConn();
    boost::signals2::signal<void ()> onConnection_;
    int localPort() const;
private:
    void startAccept();
    void on_accept(boost::system::error_code const &err);

    boost::asio::io_service &svc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::deadline_timer timer_;
    boost::shared_ptr<ConnectionInfo> pending_;
};

#endif  //  daemon_EagerConnectionFactory_h
