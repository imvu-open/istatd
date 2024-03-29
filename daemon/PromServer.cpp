#include "PromServer.h"
#include "StatServer.h"
#include "istat/strfunc.h"
#include "Logs.h"
#include "Debug.h"


#include <boost/lexical_cast.hpp>
#include <boost/bind/bind.hpp>
#include <boost/make_shared.hpp>
#include <ctype.h>
#include <iostream>


using namespace istat;
using boost::asio::ip::tcp;
using namespace boost::asio;


DebugOption debugPromServer("promServer");

PromServer::PromServer(int port, boost::asio::io_service &svc, std::string listen_addr, int listenOverflowBacklog) :
    HttpServer(port, svc, listen_addr, listenOverflowBacklog)
{
}


PromServer::~PromServer()
{
}

void PromServer::acceptOne()
{
    LogSpam << "PromService::acceptOne()";
    boost::shared_ptr<IHttpRequest> sptr = boost::make_shared<HttpRequest>(boost::ref(svc()), this);
    HttpRequestHolder request(sptr);
    acceptor_.async_accept(request->socket(),
        boost::bind(&PromServer::handleAccept, this, placeholders::error, request));
}

void PromServer::handleAccept(boost::system::error_code const &e, HttpRequestHolder const &req)
{
    LogSpam << "PromService::handleAccept()";
    ++sInfo_.numRequests;
    if (!e)
    {
        if (debugPromServer.enabled())
        {
            LogDebug << "Prometheus http request";
        }
        ++sInfo_.current;
        sInfo_.currentGauge.value((int32_t)sInfo_.current);
        onRequest_(req);
        req->readHeaders();
        acceptOne();
        return;
    }
    ++sInfo_.numErrors;
    LogError << "Error accepting a Prometheus HTTP request: " << e;
    timer_.expires_from_now(boost::posix_time::seconds(1));
    timer_.async_wait(boost::bind(&PromServer::acceptOne, this));
}

