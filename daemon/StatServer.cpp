
#include <istat/StatFile.h>
#include <istat/Atomic.h>
#include <istat/strfunc.h>
#include <istat/istattime.h>
#include "StatServer.h"
#include "StatStore.h"
#include "PromExporter.h"
#include "IComplete.h"
#include "Logs.h"
#include "Debug.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include <boost/lexical_cast.hpp>
#include <boost/bind/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/make_shared.hpp>


using boost::asio::ip::udp;
using namespace istat;


DebugOption debugUdp("statUdp");
DebugOption debugTcp("statTcp");

template<typename Consume, typename Handle>
void handle_inputData(std::string const &s, Consume const &c, Handle const &h)
{
    size_t sz = s.size();
    size_t start = 0;
    StatServer::HandleStatus handle_status = StatServer::HandleSuccess;
    for (size_t pos = 0; pos != sz; ++pos)
    {
        //  protocol says: terminate at char(10), ignore any padding space
        //  (like char(13) from crnl)
        if (s[pos] == 10)
        {
            std::string cmd(s.substr(start, pos-start));
            start = pos + 1;
            c(cmd.size()+1);
            if (handle_status != StatServer::HandleBlacklisted)
            {
                handle_status = h(cmd);
            }
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
    size_t agentCount,
    time_t agentInterval,
    Blacklist::Configuration &blacklistCfg,
    boost::asio::io_service &svc,
    boost::shared_ptr<IStatStore> &statStore,
    boost::shared_ptr<IPromExporter> &promExporter,
    int udpBufferSize,
    int listenOverflowBacklog ) :
    port_(statPort),
    forwardInterval_(agentInterval),
    agent_(agentFw),
    agentCount_(agentCount),
    statStore_(statStore),
    promExporter_(promExporter),
    input_(svc),
    svc_(svc),
    udpSocket_(svc),
    udpEndpoint_(new UdpConnectionInfo()),
    udpTimer_(svc),
    udpBufferSize_(udpBufferSize),
    listenOverflowBacklog_(listenOverflowBacklog),
    reportTimer_(svc),
    udpBackoffMs_(50),
    nConnects_("statserver.connects", TypeEvent),
    nDrops_("statserver.drops", TypeEvent),
    nConnected_("statserver.connected", TypeGauge),
    reservedCommands_("statserver.commands.reserved", TypeEvent),
    badCommands_("statserver.commands.bad", TypeEvent),
    totalMemoryGauge_("app.memory.vmsize", TypeGauge),
    residentMemoryGauge_("app.memory.vmrss", TypeGauge),
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

    if (blacklistCfg.use())
    {
        blacklist_ = boost::make_shared<Blacklist>(boost::ref(svc), boost::ref(blacklistCfg));
    }

    if (!agent_.empty())
    {
        for (size_t i = 0; i < agentCount_; ++i) {
            forward_.push_back(EagerConnection::create(svc));
        }

        startResolveAgents();
        gotSomething = true;
    }
    if (!statStore_)
    {
        hasStatStore_ = false;
        statStore_ = boost::make_shared<NullStatStore>();
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
        input_.listen(statPort, listenAddress, listenOverflowBacklog_);
        udpSocket_.open(udp::v4());
        udpSocket_.set_option(udp::socket::receive_buffer_size(udpBufferSize_ * 1024));
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

    reportMemory();

    startReport();
}

void StatServer::reportMemory()
{
    try
    {
        // In Pages: vmsize, vmrss, shared_pages, text/code, library, data+stack, dirty_pages
        unsigned long vmsize, vmrss;
        {
            std::string ignore;
            std::ifstream ifs("/proc/self/statm", std::ios_base::in);
            ifs >> vmsize >> vmrss >> ignore >> ignore >> ignore >> ignore >> ignore;
        }
        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size == -1)
        {
            return;
        }

        double vmsize_bytes = vmsize * page_size;
        double vmrss_bytes = vmrss * page_size;

        totalMemoryGauge_.value(vmsize_bytes);
        residentMemoryGauge_.value(vmrss_bytes);

    }
    catch( std::exception const &x )
    {

    }
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
        boost::shared_ptr<MetaInfo> mi = boost::make_shared<MetaInfo>();
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

void StatServer::deleteCounter(std::string const &ctr, IComplete *complete)
{
    statStore_->deleteCounter(ctr, complete);
}

void StatServer::deletePattern(std::string const &pattern, IComplete *complete)
{
    statStore_->deletePattern(pattern, complete);
}

void StatServer::refreshKeys()
{
    statStore_->refreshKeys();
}

void StatServer::startResolveAgents()
{
    std::stringstream info;
    //  integers:
    //  - non-digit to confuse very old agents
    //  - current version
    //  - minimum version needed to read the data
    info << "#proto x 1 1\r\n";
    info << "#version " << __DATE__ << " " << __TIME__ << "\r\n";
    info << "#hostname " << boost::asio::ip::host_name() << "\r\n";

    forwardTimer_.expires_from_now(boost::posix_time::seconds(forwardInterval_));
    forwardTimer_.async_wait(boost::bind(&StatServer::on_forwardTimer, this));

    for (size_t i = 0; i < agentCount_; ++i) {
        forward_[i]->onData_.connect(boost::bind(&StatServer::on_forwardData, this, i));
        forward_[i]->onWrite_.connect(boost::bind(&StatServer::on_forwardWrite, this, i, boost::placeholders::_1));
        forward_[i]->open(agent_, info.str());
        forward_[i]->asEagerConnection()->startRead();
    }
}

void StatServer::on_forwardWrite(size_t forwardIndex, size_t n)
{
    FlushRequestList afrs;
    {
        grab aholdof(agentMutex_);
        if (agentFlushRequests_.empty())
        {
            return;
        }
        for (FlushRequestList::iterator ptr(agentFlushRequests_.begin()), end(agentFlushRequests_.end());
            ptr != end;)
        {
            FlushRequestList::iterator x = ptr;
            ++ptr;
            if ((*x)->completed(forwardIndex, n))
            {
                afrs.push_back(*x);
                agentFlushRequests_.erase(x);
            }
        }
    }
}

void StatServer::on_forwardData(size_t forwardIndex)
{
    LogSpam << "StatServer::on_forwardData()";
    //  This really should never happen, because the server on the other end
    //  will never send anything back!
    size_t sz = forward_[forwardIndex]->pendingIn();
    if (sz == 0)
    {
        return;
    }
    std::string s;
    s.resize(forward_[forwardIndex]->pendingIn());
    forward_[forwardIndex]->readIn(&s[0], sz);
    LogError << "Unexpected data back from agent forward server: " << s;
}

void StatServer::on_connection()
{
    LogSpam << "StatServer::on_connection()";
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

bool StatServer::checkBlacklistAndMaybeClose(boost::shared_ptr<ConnectionInfo> const &ec)
{
    LogSpam << "StatServer::checkBlacklistAndMaybeClose()";
    grab aholdof(metaMutex_);
    void *key = ec->asEagerConnection();
    InfoHashMap::iterator mit(metaInfo_.find(key));
    if (mit != metaInfo_.end())
    {
        boost::shared_ptr<MetaInfo> mi = (*mit).second;
        MetaInfo::MetaInfoDataMap::iterator ptr(mi->info_.find("hostname"));
        if (ptr != mi->info_.end())
        {
            if (blacklist_->contains((*ptr).second))
            {
                if (ec->opened())
                {
                    close_connection(ec);
                    metaInfo(ec)->info_["blacklisted"] = "true";
                    metaInfo(ec)->online_ = false;
                }
                return true;
            }
        }
    }
    return false;
}

void StatServer::on_inputData(boost::shared_ptr<ConnectionInfo> ec)
{
    LogSpam << "StatServer::on_inputData()";

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
        LogDebug << "data from" << ec->endpointName() << sz << "bytes:" << std::string(s, sz);
    }
    ec->peekIn(&s[0], sz);
    handle_inputData(s,
        boost::bind(&ConnectionInfo::consume, ec, boost::placeholders::_1),
        boost::bind(&StatServer::handleCmd, this, boost::placeholders::_1, ec));
}

StatServer::HandleStatus StatServer::handleCmd(std::string const &cmd, boost::shared_ptr<ConnectionInfo> const &ec)
{
    LogSpam << "StatServer::handleCmd(" << cmd << ")";
    if (blacklist_ && checkBlacklistAndMaybeClose(ec))
    {
        return HandleBlacklisted;
    }

    if (!cmd.size())
    {
        return HandleUnhandled;
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
    return HandleSuccess;
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
                    LogWarning << "Unknown field in received stat: " << cmd;
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
            LogWarning << "unknown field at end of received stat:" << cmd;
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
        std::string prom_base;
        extract_ctrs(parts[0], cnames, prom_base);

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
                LogError << "Bad format in received stat (" << n << "fields; expected 2, 3 or 7):" << cmd;
                ++badCommands_;
                break;
            }
        }

        if (hasPromExporter())
        {
            switch (n)
            {
                case 2: //  ctr, value
                    handle_forward_prom(parts[0], prom_base, cnames, 0, to_double(parts[1]));
                    break;
                case 3: //  ctr, time, value
                case 7: //  ctr, time, value, valuesq, min, max, cnt
                    handle_forward_prom(parts[0], prom_base, cnames, to_time_t(parts[1]), to_double(parts[2]));
                    break;
                default:
                    break;
            }
        }
    }
    else
    {
        LogError << "Bad format in received stat (" << n << "fields; expected 2, 3 or 7):" << cmd;
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
    istat::trim(left);
    istat::trim(right);
    metaInfo(ec)->info_[left.substr(1)] = right;

    for (ForwardList::iterator i = forward_.begin(); i !=  forward_.end(); ++i) {
        // we do not bucketize the agent info ... direct forward
        if ((*i)->opened()) {
            (*i)->writeOut(cmd + "\n");
        }
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
    if (hasAgent())
    {
        handle_forward(ctr, 0, val, val*val, val, val, 1);
    }
}

void StatServer::handle_record(std::string const &ctr, time_t time, double val)
{
    statStore_->record(ctr, time, val);
    if (hasAgent())
    {
        handle_forward(ctr, time, val, val*val, val, val, 1);
    }
}

void StatServer::handle_record(std::string const &ctr, time_t time, double val, double sumSq, double min, double max, size_t n)
{
    statStore_->record(ctr, time, val, sumSq, min, max, n);
    if (hasAgent())
    {
        handle_forward(ctr, time, val, sumSq, min, max, n);
    }
}

void StatServer::handle_forward(std::string const &ctr, time_t time, double val, double sumSq, double min, double max, size_t n)
{
    time_t now;
    std::string update_err_msg;
    istat::istattime(&now);

    if (time == 0)
    {
        time = now;
    }
    grab aholdof(forwardMutex_);
    std::tr1::unordered_map<std::string, Bucketizer>::iterator buckets(
        forwardBuckets_.find(ctr));
    if (buckets == forwardBuckets_.end())
    {
        forwardBuckets_[ctr] = Bucketizer(now, istat::Bucket(val, sumSq, min, max, n, time));
        update_err_msg = forwardBuckets_[ctr].getUpdateErrMsg();
    }
    else
    {
        (*buckets).second.update(istat::Bucket(val, sumSq, min, max, n, time));
        update_err_msg = (*buckets).second.getUpdateErrMsg();
    }
    if (update_err_msg != "")
    {
        LogDebug << "StatServer::handle_forwarded for" << ctr << update_err_msg;
    }
}

void StatServer::handle_forward_prom(std::string const & ctr, std::string const & basename, std::vector<std::string> const & cnames, time_t time, double val)
{
    LogDebug << "StatServer::handle_forward_prom()";

    if (time == 0)
    {
        istat::istattime(&time);
    }
    promExporter_->storeMetrics(ctr, basename, cnames, time, val);
}

void StatServer::clearForward(AgentFlushRequest * agentFlushRequest)
{
    std::tr1::unordered_map<std::string, Bucketizer> buckets;
    {
        grab aholdof(forwardMutex_);
        buckets.swap(forwardBuckets_);
    }
    ForwardList conns;
    for (ForwardList::iterator i = forward_.begin(); i < forward_.end(); ++i) {
        if ((*i)->opened()) {
            conns.push_back(*i);
        }
    }
    size_t count = conns.size();
    if ((count > 0) && buckets.size())
    {
        size_t forwardIndex = 0;
        //  if not opened, then we lose this bucket
        std::stringstream ss[count];
        for (std::tr1::unordered_map<std::string, Bucketizer>::iterator
            ptr(buckets.begin()), end (buckets.end());
            ptr != end;
            ++ptr)
        {
            Bucketizer &bizer((*ptr).second);
            if ((*ptr).first[0] == '*')
            {
                for (unsigned int i = 0 ; i < bizer.BUCKET_COUNT; i++) {
                    istat::Bucket b = bizer.get(i);
                    if (b.count() > 0) {
                        ss[forwardIndex] << (*ptr).first << " " << b.time() << " " << b.sum() << "\r\n";
                    }
                }
            }
            else
            {
                for (unsigned int i = 0 ; i < bizer.BUCKET_COUNT; i++) {
                    istat::Bucket b = bizer.get(i);
                    if (b.count() > 0) {
                        ss[forwardIndex] << (*ptr).first << " " << b.time() << " " << b.sum() << " " <<
                            b.sumSq() << " " << b.min() << " " << b.max() << " " << b.count() << "\r\n";
                    }
                }
            }
            ++forwardIndex;
            forwardIndex %= count;
        }
        for (size_t i = 0; i < count; ++i) {
            std::string str(ss[i].str());
            if (!str.empty()) {
                size_t pending = conns[i]->writeOut(str);
                if (agentFlushRequest != NULL) {
                    agentFlushRequest->add(i, pending);
                }
            }
        }
    }
}

void StatServer::on_forwardTimer()
{
    clearForward(NULL);
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
    boost::shared_ptr<AgentFlushRequest> agentFlushRequest(new AgentFlushRequest(complete));
    clearForward(agentFlushRequest.get());
    if (!agentFlushRequest->completed())
    {
        grab aholdof(agentMutex_);
        agentFlushRequests_.push_back(agentFlushRequest);
    }
}

void StatServer::close_connection(boost::shared_ptr<ConnectionInfo> const &ec)
{
    ec->close();
    ++nDrops_;
    istat::atomic_add(&numConnected_, -1);
}

void StatServer::on_inputLost(boost::shared_ptr<ConnectionInfo> ec)
{
    LogWarning << "Lost connection from" << ec->endpointName();
    close_connection(ec);
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
    LogSpam << "StatServer::on_udp_recv()";
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
                LogDebug << "data from" << udpEndpoint_->endpoint_ << bytes << "bytes:" << std::string(udpBuffer_, bytes);
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


AgentFlushRequest::AgentFlushRequest(IComplete *comp) :
    comp_(comp)
{
}

AgentFlushRequest::~AgentFlushRequest()
{
    comp_->on_complete();
}

void AgentFlushRequest::add(size_t index, size_t n)
{
    if ((index + 1) > sizes_.size()) {
        sizes_.resize(index + 1, 0);
    }
    sizes_[index] = n;
}

bool AgentFlushRequest::completed() const
{
    for (SizeList::const_iterator i = sizes_.begin(); i != sizes_.end(); ++i) {
        if (*i > 0) {
            return false;
        }
    }
    return true;
}

bool AgentFlushRequest::completed(size_t index, size_t n)
{
    if (index < sizes_.size()) {
        if (n >= sizes_[index]) {
            sizes_[index] = 0;
        } else {
            sizes_[index] -= n;
        }
    }
    return completed();
}

#endif  //  !STAT_SERVER_TEST_ONLY

#include <istat/test.h>

void test_handleInputData()
{
    istat::CallCounter<void> consume;
    istat::CallCounter<StatServer::HandleStatus> handle(StatServer::HandleSuccess);
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
