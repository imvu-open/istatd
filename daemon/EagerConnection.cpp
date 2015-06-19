
#include "EagerConnection.h"
#include "IStatStore.h"
#include "Logs.h"
#include "istat/strfunc.h"
#include "threadfunc.h"
#include <istat/istattime.h>

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <iostream>


using namespace istat;

using boost::asio::ip::tcp;
using namespace boost::asio;
//  Allow this to be manipulated later.
size_t maxBufferSize = 64*1024;

struct EagerConnectionEnabler : public EagerConnection {
       EagerConnectionEnabler(boost::asio::io_service &svc) : EagerConnection(svc) {}
};

boost::shared_ptr<ConnectionInfo> EagerConnection::create(boost::asio::io_service &svc)
{
    return boost::make_shared<EagerConnectionEnabler>(boost::ref(svc));
}

EagerConnection::EagerConnection(boost::asio::io_service &svc) :
    tryingLater_(false),
    tryLaterTime_(0),
    backoff_(1),
    successfulWritesDuringBackoff_(1),
    interlock_(0),
    socket_(svc),
    resolver_(svc),
    timer_(svc),
    strand_(svc)
{
    init();
    ++IStatCounter::eagerconns_;
}

void EagerConnection::init()
{
    opened_ = false;
    backoff_ = 1;
    successfulWritesDuringBackoff_ = 1;
    writePending_ = false;
    readPending_ = false;
}

EagerConnection::~EagerConnection()
{
    close();
    --IStatCounter::eagerconns_;
}

void EagerConnection::open()    //  called when accepted
{
    LogSpam << "EagerConnection::open()";
    std::stringstream ss;
    ss << endpoint_;
    endpointName_ = ss.str();
    opened_ = true;
}

void EagerConnection::open(std::string const &dest, std::string const &initialData)
{
    LogSpam << "EagerConnection::open(" << dest << ")";
    close();
    std::string host;
    std::string port;
    if (split(dest, ':', host, port) != 2)
    {
        throw std::runtime_error("open() destination must be host:port");
    }
    backoff_ = 1;
    successfulWritesDuringBackoff_ = 1;
    opened_ = true;
    if (initialData.size())
    {
        initialData_ = initialData;
    }
    resolveHost_ = host;
    resolvePort_ = port;
    startResolve();
}

void EagerConnection::close()
{
    LogSpam << "EagerConnection::close()";
    if (!opened_)
    {
        return;
    }
    opened_ = false;
    socket_.close();
    timer_.cancel();
    grab aholdof(mutex_);
    writeBuf_.clear();
    inBuf_.clear();
    outgoing_.clear();
    incoming_.clear();

    disconnect_all_slots(onConnect_);
    disconnect_all_slots(onDisconnect_);
    disconnect_all_slots(onData_);
    disconnect_all_slots(onWrite_);
}

bool EagerConnection::opened()
{
    return opened_;
}

bool EagerConnection::connected()
{
    return socket_.is_open();
}

size_t EagerConnection::pendingOut()
{
    //  vector may be implemented as "start" and "end" pointers
    //  which are not synchronously updated, and thus we could get
    //  a wrong size if reading while someone else is mutating.
    //  There's a separate race where the size may change after
    //  this function returns -- the lock doesn't solve that.
    grab aholdof(mutex_);
    return outgoing_.size();
}

size_t EagerConnection::pendingIn()
{
    //  vector may be implemented as "start" and "end" pointers
    //  which are not synchronously updated, and thus we could get
    //  a wrong size if reading while someone else is mutating.
    //  There's a separate race where the size may change after
    //  this function returns -- the lock doesn't solve that.
    grab aholdof(mutex_);
    return incoming_.size();
}

size_t EagerConnection::readIn(void *ptr, size_t maxSize)
{
    LogSpam << "EagerConnection::readIn(" << maxSize << ")";
    grab aholdof(mutex_);
    size_t n = peekIn(ptr, maxSize);
    consume(n);
    return n;
}

size_t EagerConnection::peekIn(void *ptr, size_t maxSize)
{
    LogSpam << "EagerConnection::peekIn(" << maxSize << ")";
    grab aholdof(mutex_);
    size_t n = maxSize;
    if (n > incoming_.size())
    {
        n = incoming_.size();
    }
    if (n > 0 && ptr != 0)
    {
        memcpy(ptr, &incoming_[0], n);
    }
    return n;
}

void EagerConnection::consume(size_t n)
{
    LogSpam << "EagerConnection::consume(" << n << ")";
    if (!opened_)
    {
        return;
    }
    grab aholdof(mutex_);
    if (n > 0)
    {
        assert(n <= incoming_.size());
        if (n > incoming_.size())
        {
            throw std::runtime_error("EagerConnection::consume() of too much data");
        }
        incoming_.erase(incoming_.begin(), incoming_.begin() + n);
    }
}

void EagerConnection::writeOut(std::string const &str)
{
    writeOut(str.c_str(), str.size());
}

void EagerConnection::writeOut(void const *data, size_t size)
{
    LogSpam << "EagerConnection::writeOut(" << size << ")";
    if (!opened_)
    {
        throw std::runtime_error("Attempt to writeOut() to a non-open() EagerConnection.");
    }
    grab aholdof(mutex_);
    if (outgoing_.size() > maxBufferSize)
    {
        bool found = false;
        LogDebug << "notice: pruning pending outgoing buffer";
        //  find a line somewhere in the middle, and cut at that point
        for (std::vector<char>::iterator ptr(outgoing_.begin() + maxBufferSize/2),
            end(outgoing_.end()); ptr != end; ++ptr)
        {
            if (*ptr == 10)
            {
                outgoing_.erase(outgoing_.begin(), ++ptr);
                found = true;
                break;
            }
        }
        if (!found)
        {
            //  toss it all, if there's no newline!
            LogWarning << "error: there's no newline in the existing buffer data!";
            outgoing_.clear();
        }
    }
    outgoing_.insert(outgoing_.end(), (char const *)data, (char const *)data + size);
    startWrite();
}

void EagerConnection::abort()
{
    LogSpam << "EagerConnection::abort()";
    socket_.close();
    {
        grab aholdof(mutex_);
        writeBuf_.clear();
        inBuf_.clear();
        outgoing_.clear();
        incoming_.clear();
    }
    onDisconnect_();
}

tcp::endpoint EagerConnection::endpoint() const
{
    return endpoint_;
}

void EagerConnection::startResolve()
{
    LogSpam << "EagerConnection::startResolve()";
    time_t nowTime;
    istat::istattime(&nowTime);
    if (nowTime - tryLaterTime_ < backoff_ / 2)
    {
        LogError << "tryLater fired early: tryLaterTime" << tryLaterTime_ << "; nowTime" << nowTime << "; backoff" << backoff_ << "; forcing sleep.";
        //  force the thing to not suck down the CPU!
        boost::this_thread::sleep(boost::posix_time::seconds(1));
    }
    LogNotice << "Resolving " << resolveHost_ << ":" << resolvePort_;
    boost::system::error_code error;
    tcp::resolver::iterator iter = resolver_.resolve(tcp::resolver::query(resolveHost_, resolvePort_), error);
    on_resolve(error, iter);
}

void EagerConnection::on_resolve(boost::system::error_code const &err, tcp::resolver::iterator endpoint)
{
    LogSpam << "EagerConnection::on_resolve()";
    if(!err)
    {
        tryingLater_ = false;
  next:
        endpoint_ = *endpoint;
        std::stringstream ss;
        ss << endpoint_;
        endpointName_ = ss.str();
        //  prefer ipv4, because that's what we bind
        if (endpoint_.address().is_v6())
        {
          ++endpoint;
          if (endpoint != tcp::resolver::iterator())
          {
            //  the good kind of goto
            goto next;
          }
        }
        startConnect(endpoint_);
    }
    else
    {
        LogError << "Could not resolve " << resolveHost_ << ":" << resolvePort_;
        grab aholdof(mutex_);
        tryingLater_ = false;
        tryLater();
    }
}

void EagerConnection::tryLater()
{
    LogSpam << "EagerConnection::tryLater()";
    successfulWritesDuringBackoff_ = 1;
    if (resolveHost_.size() && resolvePort_.size())
    {
        if (tryingLater_)
        {
            LogDebug << "already trying later (backoff is " << backoff_ << " seconds)";
        }
        else
        {
            istat::istattime(&tryLaterTime_);
            tryingLater_ = true;
            backoff_ = (int)((backoff_ + 1) * 1.5);
            if (backoff_ > 180)
            {
                //  wait at most 3 minutes, even when backing off
                backoff_ = 180;
            }
            //  randomize the backoff
            if (backoff_ < 30)
            {
                backoff_ = backoff_ + (rand() & 3);
            }
            else
            {
                backoff_ = backoff_ + (rand() & 15);
            }
            LogWarning << "Trying again in " << backoff_ << " seconds.";
            timer_.expires_from_now(boost::posix_time::seconds(backoff_));
            timer_.async_wait(strand_.wrap(boost::bind(&EagerConnection::startResolve,
                Tramp(shared_from_this()))));
        }
    }
}

void EagerConnection::startConnect(tcp::endpoint endpoint)
{
    LogNotice << "Connecting to " << endpoint;
    socket_.async_connect(endpoint,
        strand_.wrap(boost::bind(&EagerConnection::on_connect, Tramp(shared_from_this()),
            placeholders::error, endpoint)));
}

void EagerConnection::on_connect(boost::system::error_code const &err, tcp::endpoint endpoint)
{
    if (!err)
    {
        LogWarning << "Connected to " << endpoint;
        if (initialData_.size())
        {
            outgoing_.insert(outgoing_.begin(), initialData_.begin(), initialData_.end());
        }
        onConnect_();
        startWrite();
        startRead();
    }
    else
    {
        LogError << "Error connecting to " << endpoint;
        grab aholdof(mutex_);
        tryLater();
    }
}

void EagerConnection::startWrite()
{
    LogSpam << "EagerConnection::startWrite()";
    if (socket_.is_open())
    {
        grab aholdof(mutex_);
        if (outgoing_.size() && !writePending_ && !tryingLater_)
        {
            writeBuf_.swap(outgoing_);
            outgoing_.clear();
            writePending_ = true;
            char const *ptr = &writeBuf_[0];
            boost::asio::async_write(socket_, boost::asio::buffer(ptr, writeBuf_.size()),
                strand_.wrap(boost::bind(&EagerConnection::on_write,
                    Tramp(shared_from_this()), placeholders::error, placeholders::bytes_transferred)));
        }
    }
}

void EagerConnection::resetBackoffOnSuccessfulWrites()
{
    if (backoff_ != 1)
    {
        ++successfulWritesDuringBackoff_;
        if (successfulWritesDuringBackoff_ > 3)
        {
            backoff_ = 1;
        }
    }
}

void EagerConnection::on_write(boost::system::error_code const &err, size_t xfer)
{
    LogSpam << "EagerConnection::on_write(" << xfer << ")";
    grab aholdof(mutex_);
    writePending_ = false;
    if (!!err || xfer < writeBuf_.size())
    {
        LogWarning << "Short write on socket to " << resolveHost_ << ":" << resolvePort_ << ": " << err;
        socket_.close();
        onDisconnect_();
        tryLater();
        return;
    }
    resetBackoffOnSuccessfulWrites();

    onWrite_(xfer);
    startWrite();
}


//  There is a trade-off between throughput and ability to have many connections open.
//  I may want to have 10,000 connections open (monitoring a large cluster of machines, say)
//  and I don't want to waste more than 160 MB on such a set-up: 10k * 16k buffers == 160M
void EagerConnection::startRead()
{
    LogSpam << "EagerConnect::startRead()";
    grab aholdof(mutex_);
    if (!readPending_)
    {
        readPending_ = true;
        inBuf_.resize(16384);
        char *ptr = &inBuf_[0];
        boost::asio::async_read(socket_, boost::asio::buffer(ptr, 16384), boost::asio::transfer_at_least(1),
            strand_.wrap(boost::bind(&EagerConnection::on_read, Tramp(shared_from_this()),
                placeholders::error, placeholders::bytes_transferred)));
    }
}

void EagerConnection::on_read(boost::system::error_code const &err, size_t xfer)
{
    LogSpam << "EagerConnection::on_read(" << xfer << ")";
    if (!err)
    {
        if (xfer > 0)
        {
            grab aholdof(mutex_);
            incoming_.insert(incoming_.end(), inBuf_.begin(), inBuf_.begin() + xfer);
        }
        readPending_ = false;
        assert(1 == ++interlock_);
        onData_();
        assert(0 == --interlock_);
        startRead();
    }
    else
    {
        if (resolveHost_ != "")
        {
            LogError << "Socket error for host" << resolveHost_ << "; port" << resolvePort_ << ";" << err;
        }
        else
        {
            LogError << "Socket error for endpoint" << endpoint_ << ";" << err;
        }
        socket_.close();
        onDisconnect_();
        readPending_ = false;
        if (opened_)
        {
            grab aholdof(mutex_);
            tryLater();
        }
    }
}

std::string const &EagerConnection::endpointName()
{
    return endpointName_;
}

EagerConnection *EagerConnection::asEagerConnection()
{
    return this;
}

UdpConnectionInfo *EagerConnection::asUdpConnection()
{
    return 0;
}


