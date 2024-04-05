
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

private:
    friend class HttpRequest;
    void acceptOne();
    void handleAccept(boost::system::error_code const &e, HttpRequestHolder const &req);
};

#endif  //  daemon_PromServer_h
