
#if !defined(daemon_PromServer_h)
#define daemon_PromServer_h

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/detail/atomic_count.hpp>
#include <set>
#include "LoopbackCounter.h"
#include "Signal.h"
#include "HttpServer.h"


class PromServer : public HttpServer
{
public:
    PromServer(int port, boost::asio::io_service &svc, std::string listen_addr, int listenOverflowBacklog);
    virtual ~PromServer();
    //inline boost::asio::io_service &svc() { return svc_; }
    //  You will see the request *before* headers are parsed. 
    //  Generally, sign up for onHeader_ and perhaps onError_ if you keep a reference.
    //boost::signals2::signal<void (HttpRequestHolder const &)> onRequest_;
    //void getInfo(HttpServerInfo &oInfo);

private:
    friend class HttpRequest;
    void acceptOne();
    void handleAccept(boost::system::error_code const &e, HttpRequestHolder const &req);
/*    HttpServerInfo sInfo_;
    int port_;
    int listenOverflowBacklog_;
    boost::asio::io_service &svc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::deadline_timer timer_;
*/
};

#endif  //  daemon_PromServer_h
