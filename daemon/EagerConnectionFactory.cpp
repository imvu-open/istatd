
#include "EagerConnectionFactory.h"
#include "EagerConnection.h"
#include "IStatStore.h"
#include "Logs.h"

#include <boost/bind.hpp>
#include <iostream>



using namespace istat;
using boost::asio::ip::tcp;

lock mutex_;

EagerConnectionFactory::EagerConnectionFactory(boost::asio::io_service &svc) :
    svc_(svc),
    acceptor_(svc),
    timer_(svc)
{
}

EagerConnectionFactory::~EagerConnectionFactory()
{
}

void EagerConnectionFactory::listen(int port, std::string listen_address, int listenOverflowBacklog)
{
    LogSpam << "EagerConnectionFactory::listen(" << port << ", " << listen_address << ", " << listenOverflowBacklog << ")";
    try
    {
        acceptor_.open(tcp::v4());
        acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
        if (listen_address.length() > 0) {
            acceptor_.bind(tcp::endpoint(boost::asio::ip::address::from_string(listen_address.c_str()), port));
        } else {
            acceptor_.bind(tcp::endpoint(tcp::v4(), port));
        }
        acceptor_.listen(listenOverflowBacklog);
        startAccept();
    }
    catch (...)
    {
        LogError << "EagerConnectionFactory::listen(" << port << ", " << listen_address << ", " << listenOverflowBacklog << "): unknown exception";
    }
}

boost::shared_ptr<ConnectionInfo> EagerConnectionFactory::nextConn()
{
    LogSpam << "EagerConnectionFactory::nextConn()";
    boost::shared_ptr<ConnectionInfo> ret;
    {
        grab aholdof(mutex_);
        if (!!pending_ && pending_->opened())
        {
            ret.swap(pending_);
            startAccept();
        }
    }
    return ret;
}

int EagerConnectionFactory::localPort() const {
    return acceptor_.local_endpoint().port();
}

void EagerConnectionFactory::startAccept()
{
    LogSpam << "EagerConnectionFactory::startAccept()";
    try
    {
        if (pending_ != 0)
        {
            throw std::runtime_error("Attempt to startAccept() when already accepting.");
        }
        {
            grab aholdof(mutex_);
            pending_ = EagerConnection::create(svc_);
        }
        acceptor_.async_accept(pending_->asEagerConnection()->socket_,
            pending_->asEagerConnection()->endpoint_,
            boost::bind(&EagerConnectionFactory::on_accept, this, boost::asio::placeholders::error));
    }
    catch (...)
    {
        LogError << "EagerConnectionFactory::startAccept(): unknown exception";
        throw;
    }
}

void EagerConnectionFactory::on_accept(boost::system::error_code const &err)
{
    LogSpam << "EagerConnectionFactory::on_accept()";
    try
    {
        if (!!err)
        {
            LogWarning << "EagerConnectionFactory got an error accepting a connection: " << err;
            pending_ = boost::shared_ptr<ConnectionInfo>();
            timer_.expires_from_now(boost::posix_time::seconds(1));
            timer_.async_wait(boost::bind(&EagerConnectionFactory::startAccept, this));
            return;
        }
        pending_->open();
        onConnection_();
    }
    catch (...)
    {
        LogError << "EagerConnectionFactory::on_accept(" << err << "): unknown exception";
        throw;
    }
}

