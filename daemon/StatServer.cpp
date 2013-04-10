
#include <istat/StatFile.h>
#include <istat/Atomic.h>
#include <istat/strfunc.h>
#include <istat/istattime.h>
#include "StatServer.h"
#include "StatStore.h"
#include "IComplete.h"
#include "Logs.h"
#include "Debug.h"

#include <iostream>
#include <sstream>

#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/bind/placeholders.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio/ip/host_name.hpp>


using boost::asio::ip::udp;
using namespace istat;


DebugOption debugUdp("statUdp");
DebugOption debugTcp("statTcp");


template<typename Consume, typename Handle>
void handle_inputData(std::string const &s, Consume const &c, Handle const &h)
{
    size_t sz = s.size();
    size_t start = 0;
    for (size_t pos = 0; pos != sz; ++pos)
    {
        //  protocol says: terminate at char(10), ignore any padding space
        //  (like char(13) from crnl)
        if (s[pos] == 10)
        {
            std::string cmd(s.substr(start, pos-start));
            start = pos + 1;
            c(cmd.size()+1);
            h(cmd);
        }
    }
}

double to_double(std::string const &str)
{
    if (str == "nan" || str == "NaN" || str == "-nan" || str == "-NaN" || str == "")
    {
        return 0.0;
    }
    return boost::lexical_cast<double>(str);
}

time_t to_time_t(std::string const &str)
{
    return boost::lexical_cast<time_t>(str);
}

size_t to_size_t(std::string const &str)
{
    return boost::lexical_cast<size_t>(str);
}



#if !defined(STAT_SERVER_TEST_ONLY)

StatServer::StatServer(int statPort, std::string listenAddress,
    std::string const &agentFw,
    time_t agentInterval,
    boost::asio::io_service &svc,
    boost::shared_ptr<IStatStore> &statStore) :
    port_(statPort),
    forwardInterval_(agentInterval),
    agent_(agentFw),
    statStore_(statStore),
    forward_(EagerConnection::create(svc)),
    input_(svc),
    svc_(svc),
    udpSocket_(svc),
    udpEndpoint_(new UdpConnectionInfo()),
    udpTimer_(svc),
    reportTimer_(svc),
    udpBackoffMs_(50),
    nConnects_("statserver.connects", TypeEvent),
    nDrops_("statserver.drops", TypeEvent),
    nConnected_("statserver.connected", TypeGauge),
    reservedCommands_("statserver.commands.reserved", TypeEvent),
    badCommands_("statserver.commands.bad", TypeEvent),
    numConnected_(0),
    metaInterval_(0),
    forwardTimer_(svc)
{
    if (statPort < 0 || statPort > 65535)
    {
        throw std::runtime_error("Bad stat port specified: " + boost::lexical_cast<std::string>(statPort));
    }

    hasStatStore_ = true;
    bool gotSomething = false;
    if (hasAgent())
    {
        startResolveAgent();
        gotSomething = true;
    }
    if (!statStore_)
    {
        hasStatStore_ = false;
        statStore_ = boost::shared_ptr<IStatStore>(new NullStatStore());
    }
    if (hasStore())
    {
        LogNotice << "Store location is:" << store()->getLocation();
        gotSomething = true;
    }
    if (!gotSomething)
    {
        throw std::runtime_error("StatServer must have at least one of a forward address or a local store.");
    }
    if (statPort > 0)
    {
        input_.onConnection_.connect(boost::bind(&StatServer::on_connection, this));
        input_.listen(statPort, listenAddress);
        //  Large receive buffer, if a zillion servers send a state dump at a synchronized time.
        //  Generally, though, the server must be capable of receiving counters at line speed,
        //  or no buffer size will be sufficient in the long run.
        udpSocket_.open(udp::v4());
        udpSocket_.set_option(udp::socket::receive_buffer_size(1024 * 256));
        if (listenAddress.length() > 0) {
            udpSocket_.bind(udp::endpoint(boost::asio::ip::address::from_string(listenAddress.c_str()), statPort));
        } else {
            udpSocket_.bind(udp::endpoint(udp::v4(), statPort));
        }
        recvOneUdp();
    }
    startReport();
}

void StatServer::startReport()
{
    reportTimer_.expires_from_now(boost::posix_time::seconds(5));
    reportTimer_.async_wait(boost::bind(&StatServer::onReport, this, boost::asio::placeholders::error));
}

void StatServer::onReport(boost::system::error_code const &err)
{
    LogSpam << "StatServer::onReport()";
    if (!!err)
    {
        LogWarning << "StatServer::onReport error:" << err;
        return;
    }
    time_t now;
    if (istat::istattime(&now) > metaInterval_ + 300)
    {
        metaInterval_ = now;
        //  can be no more than 100 records, and no more than 86400 seconds old
        purgeOldMetaRecords(100, 90000);
    }
    nConnected_.value(numConnected_);
    startReport();
}


StatServer::~StatServer()
{
}

boost::shared_ptr<MetaInfo> StatServer::metaInfo(boost::shared_ptr<ConnectionInfo> const &ec)
{
    grab aholdof(metaMutex_);
    //  Udp info will be 0, so all UDP will merge -- that's probably what we want, anyway!
    void *key = ec->asEagerConnection();
    InfoHashMap::iterator ptr(metaInfo_.find(key));
    if (ptr == metaInfo_.end() || !(*ptr).second->online_)
    {
        boost::shared_ptr<MetaInfo> mi(new MetaInfo());
        mi->online_ = true;
        ::time(&mi->connected_);
        mi->activity_ = mi->connected_;
        mi->info_["_peer"] = ec->endpointName();
        metaInfo_[key] = mi;
        return mi;
    }
    return (*ptr).second;
}

//  Purging only purges offline records
void StatServer::purgeOldMetaRecords(size_t maxOld, time_t maxAge)
{
    time_t now;
    istat::istattime(&now);
    time_t then = now - maxAge;
    grab aholdof(metaMutex_);
    std::vector<std::pair<time_t, InfoHashMap::key_type> > toPurge;
    for (InfoHashMap::iterator ptr(metaInfo_.begin()), end(metaInfo_.end());
        ptr != end; ++ptr)
    {
        if (!(*ptr).second->online_)
        {
            toPurge.push_back(std::pair<time_t, InfoHashMap::key_type>((*ptr).second->activity_, (*ptr).first));
        }
        else if ((*ptr).second->activity_ < then)
        {
            LogWarning << "connection" << (*ptr).second->info_["_peer"] << "is marked as online but older idle than allowed max age";
        }
    }
    std::sort(toPurge.begin(), toPurge.end());
    size_t n = toPurge.size();
    for (size_t i = 0; i != n; ++i)
    {
        if (toPurge[i].first > then && (n - i) >= maxOld)
        {
            break;
        }
        metaInfo_.erase(metaInfo_.find(toPurge[i].second));
    }
}

void StatServer::startResolveAgent()
{
    forward_->onData_.connect(boost::bind(&StatServer::on_forwardData, this));
    forward_->onWrite_.connect(boost::bind(&StatServer::on_forwardWrite, this, _1));
    std::stringstream info;
    //  integers:
    //  - non-digit to confuse very old agents
    //  - current version
    //  - minimum version needed to read the data
    info << "#proto x 1 1\r\n";
    info << "#version " << __DATE__ << " " << __TIME__ << "\r\n";
    info << "#hostname " << boost::asio::ip::host_name() << "\r\n";
    forward_->open(agent_, info.str());
    forwardTimer_.expires_from_now(boost::posix_time::seconds(forwardInterval_));
    forwardTimer_.async_wait(boost::bind(&StatServer::on_forwardTimer, this));
    forward_->asEagerConnection()->startRead();
}

void StatServer::on_forwardWrite(size_t n)
{
    std::list<AgentFlushRequest *> afrs;
    {
        grab aholdof(agentMutex_);
        if (agentFlushRequests_.empty())
        {
            return;
        }
        for (std::list<AgentFlushRequest *>::iterator ptr(agentFlushRequests_.begin()), end(agentFlushRequests_.end());
            ptr != end;)
        {
            std::list<AgentFlushRequest *>::iterator x = ptr;
            ++ptr;
            if ((*x)->completed(n))
            {
                afrs.push_back(*x);
                agentFlushRequests_.erase(x);
            }
        }
    }
    while (!afrs.empty())
    {
        delete afrs.front();
        afrs.pop_front();
    }
}

void StatServer::on_forwardData()
{
    LogDebug << "StatServer::on_forwardData()";
    //  This really should never happen, because the server on the other end
    //  will never send anything back!
    size_t sz = forward_->pendingIn();
    if (sz == 0)
    {
        return;
    }
    std::string s;
    s.resize(forward_->pendingIn());
    forward_->readIn(&s[0], sz);
    LogError << "Unexpected data back from agent forward server: " << s;
}

void StatServer::on_connection()
{
    LogDebug << "StatServer::on_connection()";
    while (true)
    {
        boost::shared_ptr<ConnectionInfo> ec(input_.nextConn());
        if (!ec)
        {
            break;
        }
        ++nConnects_;
        istat::atomic_add(&numConnected_, 1);
        LogNotice << "new StatServer connection from" << ec->endpointName();
        ec->onDisconnect_.connect(boost::bind(&StatServer::on_inputLost, this, ec));
        ec->onData_.connect(boost::bind(&StatServer::on_inputData, this, ec));
        ec->asEagerConnection()->startRead();
    }
}

void StatServer::on_inputData(boost::shared_ptr<ConnectionInfo> ec)
{
    LogDebug << "StatServer::on_inputData()";
    size_t sz = ec->pendingIn();
    if (sz == 0)
    {
        return;
    }
    ::time(&metaInfo(ec)->activity_);
    std::string s;
    s.resize(sz);
    if (debugTcp)
    {
        LogNotice << "data from" << ec->endpointName() << sz << "bytes:" << std::string(s, sz);
    }
    ec->peekIn(&s[0], sz);
    handle_inputData(s,
        boost::bind(&ConnectionInfo::consume, ec, _1),
        boost::bind(&StatServer::handleCmd, this, _1, ec));
}

void StatServer::handleCmd(std::string const &cmd, boost::shared_ptr<ConnectionInfo> const &ec)
{
    LogSpam << "StatServer::handleCmd(" << cmd << ")";
    if (!cmd.size())
    {
        return;
    }
    try
    {
        switch (cmd[0])
        {
            case '*':
            default:
                handle_counter_cmd(cmd);
                break;
            case '#':
                handle_meta_info(cmd, ec);
                break;
            case '@':
            case '$':
            case '%':
            case '&':
            case ';':
            case ':':
                handle_reserved_cmd(cmd, ec);
                break;
        }
    }
    catch (std::exception const &x)
    {
        LogError << "Caught exception in handleCmd(): " << x.what() << ": " << cmd;
    }
    catch (...)
    {
        LogError << "Caught unknown exception in handleCmd(): " << cmd;
    }
}

void StatServer::handle_counter_cmd(std::string const &cmd)
{
    //  format of each cmd:
    //  countername {measurement | time measurement [msq min max count]}
    //  separators are one or more whitespace
    size_t start = 0, pos = 0;
    unsigned int n = 0;
    std::string parts[7];

    //  TODO: this could probably be re-forumulated on top of istat::split()
    for (size_t l = cmd.size(); pos != l; ++pos)
    {
        bool spc = !!isspace(cmd[pos]);
        if (spc)
        {
            if (pos == start)
            {
                //  leading space
                ++start;
            }
            else
            {
                //  got part
                if (n >= sizeof(parts)/sizeof(parts[0]))
                {
                    LogNotice << "Unknown field in received stat: " << cmd;
                    break;
                }
                parts[n] = cmd.substr(start, pos-start);
                start = pos + 1;
                ++n;
            }
        }
    }

    if (start < pos)
    {
        if (n >= sizeof(parts)/sizeof(parts[0]))
        {
            LogNotice << "unknown field at end of received stat:" << cmd;
        }
        else
        {
            parts[n] = cmd.substr(start, pos-start);
            ++n;
        }
    }


    if(n > 1)
    {
        std::vector<std::string> cnames;
        extract_ctrs(parts[0], cnames);

        for (std::vector<std::string>::iterator it(cnames.begin()), end(cnames.end()); it != end; ++it)
        {
            switch (n)
            {
            case 2: //  ctr, value
                handle_record(*it, to_double(parts[1]));
                break;
            case 3: //  ctr, time, value
                handle_record(*it, to_time_t(parts[1]), to_double(parts[2]));
                break;
            case 7: //  ctr, time, value, valuesq, min, max, cnt
                handle_record(*it, to_time_t(parts[1]), to_double(parts[2]),
                    to_double(parts[3]), to_double(parts[4]),
                    to_double(parts[5]), to_size_t(parts[6]));
                break;
            default:
                LogNotice << "Bad format in received stat (" << n << "fields; expected 2, 3 or 7):" << cmd;
                ++badCommands_;
                break;
            }
        }
    }
    else
    {
        LogNotice << "Bad format in received stat (" << n << "fields; expected 2, 3 or 7):" << cmd;
        ++badCommands_;
    }
}

bool StatServer::handle_meta_info(std::string const &cmd, boost::shared_ptr<ConnectionInfo> const &ec)
{
    //  meta info format:
    //  #variable information goes here
    //  This will be listed in a per-connection struct saying:
    //  {"variable":"info", ...}
    std::string left, right;
    if (istat::split(cmd, ' ', left, right) != 2) {
        LogWarning << "Bad format meta info:" << cmd;
        ++badCommands_;
        return false;
    }
    metaInfo(ec)->info_[left.substr(1)] = right;
    if (forward_->opened())
    {
        // we do not bucketize the agent info ... direct forward
        forward_->writeOut(cmd + "\n");
    }

    return true;
}

bool StatServer::handle_reserved_cmd(std::string const &cmd, boost::shared_ptr<ConnectionInfo> const &ec)
{
    LogWarning << "Unknown reserved command:" << cmd;
    ++reservedCommands_;
    return false;
}

void StatServer::handle_record(std::string const &ctr, double val)
{
    statStore_->record(ctr, val);
    if (forward_->opened())
    {
        handle_forward(ctr, 0, val, val*val, val, val, 1);
    }
}

void StatServer::handle_record(std::string const &ctr, time_t time, double val)
{
    statStore_->record(ctr, time, val);
    if (forward_->opened())
    {
        handle_forward(ctr, time, val, val*val, val, val, 1);
    }
}

void StatServer::handle_record(std::string const &ctr, time_t time, double val, double sumSq, double min, double max, size_t n)
{
    statStore_->record(ctr, time, val, sumSq, min, max, n);
    if (forward_->opened())
    {
        handle_forward(ctr, time, val, sumSq, min, max, n);
    }
}

void StatServer::handle_forward(std::string const &ctr, time_t time, double val, double sumSq, double min, double max, size_t n)
{
    if (time == 0)
    {
        istat::istattime(&time);
    }
    grab aholdof(forwardMutex_);
    std::tr1::unordered_map<std::string, istat::Bucket>::iterator buck(
        forwardCounters_.find(ctr));
    if (buck == forwardCounters_.end())
    {
        forwardCounters_[ctr] = istat::Bucket(val, sumSq, min, max, n, time);
    }
    else
    {
        (*buck).second.update(istat::Bucket(val, sumSq, min, max, n, time));
    }
}

void StatServer::clearForward()
{
    std::tr1::unordered_map<std::string, istat::Bucket> counters;
    {
        grab aholdof(forwardMutex_);
        counters.swap(forwardCounters_);
    }
    if (forward_->opened() && counters.size())
    {
        //  if not opened, then we lose this bucket
        std::stringstream ss;
        for (std::tr1::unordered_map<std::string, istat::Bucket>::iterator
            ptr(counters.begin()), end (counters.end());
            ptr != end;
            ++ptr)
        {
            istat::Bucket &b((*ptr).second);
            if ((*ptr).first[0] == '*')
            {
                ss << (*ptr).first << " " << b.time() << " " << b.sum() << "\r\n";
            }
            else
            {
                ss << (*ptr).first << " " << b.time() << " " << b.sum() << " " <<
                    b.sumSq() << " " << b.min() << " " << b.max() << " " << b.count() << "\r\n";
            }
        }
        std::string str(ss.str());
        forward_->writeOut(str);
    }
}

void StatServer::on_forwardTimer()
{
    clearForward();
    forwardTimer_.expires_from_now(boost::posix_time::seconds(forwardInterval_));
    forwardTimer_.async_wait(boost::bind(&StatServer::on_forwardTimer, this));
}


void StatServer::getConnected(std::vector<MetaInfo> &agents)
{
    grab aholdof(metaMutex_);
    for (InfoHashMap::iterator ptr(metaInfo_.begin()), end(metaInfo_.end());
        ptr != end; ++ptr)
    {
        agents.push_back(*(*ptr).second);
    }
}

void StatServer::syncAgent(IComplete *complete)
{
    size_t size = agentQueueSize();
    if (!hasAgent() || size == 0)
    {
        svc_.post(boost::bind(&IComplete::on_complete, complete));
        return;
    }
    grab aholdof(agentMutex_);
    agentFlushRequests_.push_back(new AgentFlushRequest(size, complete));
    clearForward();
}

size_t StatServer::agentQueueSize()
{
    return forward_->pendingOut();
}

void StatServer::on_inputLost(boost::shared_ptr<ConnectionInfo> ec)
{
    LogWarning << "Lost connection from" << ec->endpointName();
    ec->close();
    ++nDrops_;
    istat::atomic_add(&numConnected_, -1);
    grab aholdof(metaMutex_);
    metaInfo(ec)->online_ = false;
}

void StatServer::recvOneUdp()
{
    LogSpam << "StatServer::recvOneUdp()";
    udpEndpoint_->endpointName_.clear();
    udpSocket_.async_receive_from(
        boost::asio::buffer(udpBuffer_, sizeof(udpBuffer_)),
        udpEndpoint_->endpoint_,
        boost::bind(&StatServer::on_udp_recv, this,
            boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void StatServer::reschedule_udp_recv(bool success, const std::string &reason)
{
    LogSpam << "StatServer::reschedule_udp_recv()";
    if (success)
    {
        //  Got some good data, so reset the backoff.
        //  Next time we get an error, wait double this time milliseconds.
        udpBackoffMs_ = 5;
    }
    else
    {
        udpBackoffMs_ = udpBackoffMs_ * 2;
        if (udpBackoffMs_ > 2000)
        {
            udpBackoffMs_ = 2000;
        }
        LogError << "UDP Receive error:" << reason << "; waiting" << udpBackoffMs_ << "ms.";
        udpTimer_.expires_from_now(boost::posix_time::milliseconds(udpBackoffMs_));
        udpTimer_.async_wait(boost::bind(&StatServer::recvOneUdp, this));
        return;
    }
    recvOneUdp();
}

void StatServer::on_udp_recv(boost::system::error_code const &err, size_t bytes)
{
    LogDebug << "StatServer::on_udp_recv()";
    bool success = false;
    std::string reason;
    if (!!err)
    {
        reason = "Error on UDP port " + boost::lexical_cast<std::string>(port_) +
                 ": " + boost::lexical_cast<std::string>(err);
    }
    else
    {
            if (debugUdp)
            {
                LogNotice << "data from" << udpEndpoint_->endpoint_ << bytes << "bytes:" << std::string(udpBuffer_, bytes);
            }
        //  packet must end with NL, and contain some data!
        if (bytes < 5 || bytes > sizeof(udpBuffer_) || udpBuffer_[bytes-1] != 10)
        {
            reason = "Bad UDP port " + boost::lexical_cast<std::string>(port_) +
                     " packet received from " + udpEndpoint_->endpointName();
        }
        else
        {
            success = true;
            size_t off = 0, base = 0;
            while (off < bytes)
            {
                if (udpBuffer_[off] == 10)
                {
                    if (off > base + 1)
                    {
                        std::string cmd(&udpBuffer_[base], &udpBuffer_[off]);
                        base = off + 1;
                        handleCmd(cmd, udpEndpoint_);
                    }
                }
                ++off;
            }
        }
    }
    reschedule_udp_recv(success, reason);
}


void StatServer::ignore(std::string const &name)
{
    statStore_->ignore(name);
}


AgentFlushRequest::AgentFlushRequest(size_t n, IComplete *comp) :
    n_(n),
    comp_(comp)
{
}

AgentFlushRequest::~AgentFlushRequest()
{
    comp_->on_complete();
}

bool AgentFlushRequest::completed(size_t n)
{
    if (n >= n_)
    {
        n_ = 0;
        return true;
    }
    n_ -= n;
    return false;
}

#endif  //  !STAT_SERVER_TEST_ONLY

#include <istat/test.h>

void test_handleInputData()
{
    istat::CallCounter consume;
    istat::CallCounter handle;
    handle_inputData("foo bar\r\nbaz blorg\r\n", consume, handle);
    assert_equal(handle.count_, 2);
    assert_equal(handle.str_, "foo bar\rbaz blorg\r");
    assert_equal(consume.count_, 2);
    assert_equal(consume.value_, 20);
}

void test_to_double()
{
    assert_equal(1.0,to_double("1.0"));
    assert_equal(0.0,to_double("nan"));
    assert_equal(0.0,to_double("NaN"));
    assert_equal(0.0,to_double("-nan"));
    assert_equal(0.0,to_double("-NaN"));
}
