#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <tr1/unordered_map>
#include <sys/stat.h>
#include <istat/test.h>
#include <istat/istattime.h>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/tokenizer.hpp>
#include <boost/version.hpp>
#include <istat/Log.h>

#include "../daemon/StatServer.h"
#include "../include/istat/Mmap.h"
#include "../daemon/StatCounterFactory.h"
#include "../daemon/StatStore.h"
#include "../daemon/Retention.h"
#include "../daemon/FakeEagerConnection.h"
#include "../daemon/Blacklist.h"
#include "../daemon/PromExporter.h"

#include "TestComplete.h"

#define BUFSIZE 256

using namespace istat;


RetentionPolicy rp("10s:1d,5m:2d,1h:1y");
RetentionPolicy xrp("");

boost::shared_ptr<StatServer> makeServer(Mmap *mm, boost::asio::io_service &svc, int port = 0, std::string agent = "", size_t agentCount = 1) {
    Blacklist::Configuration blacklistCfg = {};
    blacklistCfg.path = std::string("");
    blacklistCfg.period = 0;
    std::string listenAddress("");
    std::string storePath("/tmp/test/testdir");
    //Mmap *mm(NewMmap());
    boost::shared_ptr<IStatCounterFactory> statCounterFactory = boost::make_shared<StatCounterFactory>(storePath, mm, boost::ref(rp));
    boost::shared_ptr<IStatStore> statStore = boost::make_shared<StatStore>(storePath, getuid(), boost::ref(svc), statCounterFactory, mm);
    statStore->setAggregateCount(2);
    boost::shared_ptr<IPromExporter> promExporter = boost::make_shared<NullPromExporter>();
    return boost::make_shared<StatServer>(port, listenAddress, agent, agentCount, 1, boost::ref(blacklistCfg), boost::ref(svc), boost::ref(statStore), boost::ref(promExporter), 256, 256);
}

boost::shared_ptr<StatServer> makeServerWithBlacklist(Mmap *mm, boost::asio::io_service &svc, int period) {
    int port = 0;
    std::string agent("");
    std::string blacklist;
    Blacklist::Configuration blacklistCfg = {};
    blacklistCfg.path = std::string("/tmp/test/blacklist/ss_blacklist.set");
    blacklistCfg.period = period;
    std::string listenAddress("");
    std::string storePath("/tmp/test/testdir");
    //Mmap *mm(NewMmap());
    boost::shared_ptr<IStatCounterFactory> statCounterFactory = boost::make_shared<StatCounterFactory>(storePath, mm, boost::ref(rp));
    boost::shared_ptr<IStatStore> statStore = boost::make_shared<StatStore>(storePath, getuid(), boost::ref(svc), statCounterFactory, mm);
    statStore->setAggregateCount(2);
    boost::shared_ptr<IPromExporter> promExporter = boost::make_shared<NullPromExporter>();
    return boost::make_shared<StatServer>(port, listenAddress, agent, 1, 1, boost::ref(blacklistCfg), boost::ref(svc), boost::ref(statStore), boost::ref(promExporter), 256, 256);
}

boost::shared_ptr<StatServer> makeServerWithPromExporter(boost::asio::io_service &svc, int port = 0, std::string agent = "", size_t agentCount = 1) {
    Blacklist::Configuration blacklistCfg = {};
    std::string listenAddress("");
    boost::shared_ptr<IPromExporter> promExporter = boost::make_shared<PromExporter>(boost::ref(svc), "");
    boost::shared_ptr<IStatStore> statStore = boost::make_shared<NullStatStore>();
    return boost::make_shared<StatServer>(port, listenAddress, agent, agentCount, 1, boost::ref(blacklistCfg), boost::ref(svc), boost::ref(statStore), boost::ref(promExporter), 256, 256);
}

void test_counter() {
    istat::FakeTime ft(1329345880);
    boost::asio::io_service svc;
    boost::shared_ptr<StatServer> server;
    Mmap *mm(NewMmap());
    server = makeServer(mm, svc);
    boost::shared_ptr<ConnectionInfo> ec = boost::make_shared<FakeEagerConnection>(boost::ref(svc));

    boost::shared_ptr<IStatCounter> statCounter;
#if BOOST_VERSION >= 106700
    boost::asio::strand<boost::asio::io_service::executor_type>* strand = 0;
#else
    boost::asio::strand* strand = 0;
#endif
    server->handleCmd("something.different 4242", ec);
    svc.poll();

    // Advance time so we include the data we just recorded in the returned results.
    ft.set(istattime(0) + 10);
    server->store()->find("something.different", statCounter, strand);

    std::vector<istat::Bucket> buckets;
    time_t normalized_start, normalized_end, interval;
    statCounter->select(0, 0, false, buckets, normalized_start, normalized_end, interval, 0);
    assert_equal(1, buckets.size());
    assert_equal(4242, buckets[0].sum());
}

void test_multiple_counters() {
    istat::FakeTime ft(1329345880);
    boost::asio::io_service svc;
    boost::shared_ptr<StatServer> server;
    Mmap *mm(NewMmap());
    server = makeServer(mm, svc);
    boost::shared_ptr<ConnectionInfo> ec = boost::make_shared<FakeEagerConnection>(boost::ref(svc));

    boost::shared_ptr<IStatCounter> statCounter;
#if BOOST_VERSION >= 106700
    boost::asio::strand<boost::asio::io_service::executor_type>* strand;
#else
    boost::asio::strand* strand;
#endif
    server->handleCmd("test.counter^a^b^c 4242", ec);
    svc.poll();

    // Advance time so we include the data we just recorded in the returned results.
    ft.set(istattime(0) + 10);
    server->store()->find("test.counter.b", statCounter, strand);

    std::vector<istat::Bucket> buckets;
    time_t normalized_start, normalized_end, interval;
    statCounter->select(0, 0, false, buckets, normalized_start, normalized_end, interval, 0);
    assert_equal(1, buckets.size());
    assert_equal(4242, buckets[0].sum());
}

void test_collated_counters() {
    boost::asio::io_service svc;
    boost::shared_ptr<StatServer> server;
    Mmap *mm(NewMmap());
    server = makeServer(mm, svc);
    boost::shared_ptr<ConnectionInfo> ec = boost::make_shared<FakeEagerConnection>(boost::ref(svc));

    boost::shared_ptr<IStatCounter> statCounter;
#if BOOST_VERSION >= 106700
    boost::asio::strand<boost::asio::io_service::executor_type>* strand;
#else
    boost::asio::strand* strand;
#endif
    time_t now = 1000000000 - 1000;
    istat::FakeTime ft(now);
    char buffer[BUFSIZE];
    snprintf(buffer, BUFSIZE, "*collated.counter %ld 4242", now);
    server->handleCmd(buffer, ec);
    server->handleCmd(buffer, ec);
    server->handleCmd(buffer, ec);

    //  running something that's a lot further into the future will flush
    //  the previous bucket
    ft.set(now+100);
    snprintf(buffer, BUFSIZE, "*collated.counter %ld 4242", now+100);
    server->handleCmd(buffer, ec);
    svc.poll();

    server->store()->find("collated.counter", statCounter, strand);
    std::vector<istat::Bucket> buckets;
    time_t normalized_start, normalized_end, interval=0;

    statCounter->select(now-900000,now+10,false, buckets, normalized_start, normalized_end, interval, 0);
    assert_equal(interval, 60 * 60);
    assert_equal(251, buckets.size());
    size_t bucketSum = 0;
    for (size_t i = 0; i != buckets.size(); ++i)
    {
        bucketSum += buckets[i].sum();
    }
    assert_equal(4242*3/10, bucketSum);

    statCounter->select(now-10,now+9,false, buckets, normalized_start, normalized_end, interval, 0);
    assert_equal(interval, 10);
    assert_equal(2, buckets.size());
    assert_equal(4242*3/10, (int)buckets[1].sum());

    statCounter->select(now-90000,now+9,false, buckets, normalized_start, normalized_end, interval, 0);
    assert_equal(interval, 5*60);
    assert_equal(301, buckets.size());
    bucketSum = 0;
    for (size_t i = 0; i != buckets.size(); ++i)
    {
        bucketSum += buckets[i].sum();
    }
    assert_equal(4242*3/10, bucketSum);
}


struct condition_match : public std::binary_function<MetaInfo, std::string, bool> {
    bool operator()(const MetaInfo &m, const std::string &match) const
    {
        std::tr1::unordered_map<std::string, std::string>::const_iterator it = m.info_.find("hostname");
        return ((*it).second == match);
    }
};

void test_blacklist() {

    mkdir("/tmp/test/blacklist", 511);
    int i = open("/tmp/test/blacklist/ss_blacklist.set", O_RDWR | O_CREAT, 511);
    assert_not_equal(i, -1);
    close(i);
    i = open("/tmp/test/blacklist/ss_blacklist.set", O_RDWR | O_CREAT, 511);
    char const *str = "hostname1\nhostname2\nhostname3\n";
    assert_not_equal(i, -1);
    assert_equal(write(i, str, strlen(str)), (ssize_t)strlen(str));
    close(i);

    istat::FakeTime ft(1329345880);
    boost::asio::io_service svc;
    boost::shared_ptr<StatServer> server;
    Mmap *mm(NewMmap());
    server = makeServerWithBlacklist(mm, svc, 10);
    boost::shared_ptr<ConnectionInfo> ec = boost::make_shared<FakeEagerConnection>(boost::ref(svc));

    server->handleCmd("#hostname hostname1", ec);
    svc.poll();

    // Advance time so we include the data we just recorded in the returned results.
    ft.set(istattime(0) + 10);
    std::vector<MetaInfo> metaInfo;
    server->getConnected(metaInfo);

    std::vector<MetaInfo>::iterator it = std::find_if(
            metaInfo.begin(), 
            metaInfo.end(), 
            std::bind2nd(condition_match(), "hostname1"));
    assert_true(it != metaInfo.end());

    boost::shared_ptr<IStatCounter> statCounter;
#if BOOST_VERSION >= 106700
    boost::asio::strand<boost::asio::io_service::executor_type>* strand = 0;
#else
    boost::asio::strand* strand = 0;
#endif
    server->handleCmd("i.should.be.blacklisted 4242", ec);
    svc.poll();

    // Advance time so we include the data we just recorded in the returned results.
    ft.set(istattime(0) + 10);
    server->store()->find("i.should.be.blacklisted", statCounter, strand);

    assert_equal(boost::shared_ptr<IStatCounter>((IStatCounter*)0), statCounter);
    assert_equal(false, ec->opened());

    boost::shared_ptr<ConnectionInfo> ecg = boost::make_shared<FakeEagerConnection>(boost::ref(svc));

    server->handleCmd("#hostname hostname7", ecg);
    svc.poll();

    // Advance time so we include the data we just recorded in the returned results.
    ft.set(istattime(0) + 10);
    std::vector<MetaInfo> metaInfoG;
    server->getConnected(metaInfoG);

    it = std::find_if(
            metaInfoG.begin(), 
            metaInfoG.end(), 
            std::bind2nd(condition_match(), "hostname7"));
    assert_true(it != metaInfoG.end());

    boost::shared_ptr<IStatCounter> statCounterG;
#if BOOST_VERSION >= 106700
    boost::asio::strand<boost::asio::io_service::executor_type>* strandG = 0;
#else
    boost::asio::strand* strandG = 0;
#endif
    server->handleCmd("i.should.not.be.blacklisted 4242", ecg);
    svc.poll();

    // Advance time so we include the data we just recorded in the returned results.
    ft.set(istattime(0) + 10);
    server->store()->find("i.should.not.be.blacklisted", statCounterG, strandG);

    std::vector<istat::Bucket> buckets;
    time_t normalized_start, normalized_end, interval;
    statCounterG->select(0, 0, false, buckets, normalized_start, normalized_end, interval, 0);
    assert_equal(1, buckets.size());
    assert_equal(4242, buckets[0].sum());
}

void test_start_with_missing_blacklist() {
    istat::FakeTime ft(1329345880);
    boost::asio::io_service svc;
    boost::shared_ptr<StatServer> server;
    Mmap *mm(NewMmap());
    server = makeServerWithBlacklist(mm, svc, 10);
    boost::shared_ptr<ConnectionInfo> ec = boost::make_shared<FakeEagerConnection>(boost::ref(svc));

    boost::shared_ptr<IStatCounter> statCounter;
#if BOOST_VERSION >= 106700
    boost::asio::strand<boost::asio::io_service::executor_type>* strand = 0;
#else
    boost::asio::strand* strand = 0;
#endif
    server->handleCmd("something.differenter 4242", ec);
    svc.poll();

    // Advance time so we include the data we just recorded in the returned results.
    ft.set(istattime(0) + 10);
    server->store()->find("something.differenter", statCounter, strand);

    std::vector<istat::Bucket> buckets;
    time_t normalized_start, normalized_end, interval;
    statCounter->select(0, 0, false, buckets, normalized_start, normalized_end, interval, 0);
    assert_equal(1, buckets.size());
    assert_equal(4242, buckets[0].sum());
}

void test_start_with_blacklist_disabled_due_to_0_period() {
    istat::FakeTime ft(1329345880);
    boost::asio::io_service svc;
    boost::shared_ptr<StatServer> server;
    Mmap *mm(NewMmap());
    server = makeServerWithBlacklist(mm, svc, 0);
    assert_equal(boost::shared_ptr<Blacklist>((Blacklist*)0), server->blacklist());
}

struct FakeServer {
    FakeServer(boost::asio::io_service &svc) : acceptor_(svc), nConnects_(0) {
        acceptor_.onConnection_.connect(boost::bind(&FakeServer::on_connection, this));
        acceptor_.listen(0, std::string(), boost::asio::socket_base::max_connections);
    }
    std::string address() {
        std::stringstream ss;
        ss << "localhost:" << acceptor_.localPort();
        return ss.str();
    }
    void on_connection() {
        boost::shared_ptr<ConnectionInfo> ec(acceptor_.nextConn());
        streams_.push_back(std::string());
        ec->onData_.connect(boost::bind(&FakeServer::on_inputData, this, ec, nConnects_));
        ec->asEagerConnection()->startRead();
        ++nConnects_;
    }
    void on_inputData(boost::shared_ptr<ConnectionInfo> ec, size_t streamIndex) {
        size_t len = ec->pendingIn();
        std::string data;
        data.resize(len);
        ec->readIn(&data[0], len);
        streams_[streamIndex] += data;
    }
    std::vector<std::string> lines(size_t streamIndex) {
        std::vector<std::string> result;
        boost::char_separator<char> sep("\r\n");
        boost::tokenizer<boost::char_separator<char> > tokens(streams_[streamIndex], sep);
        for (boost::tokenizer<boost::char_separator<char> >::iterator i = tokens.begin(); i != tokens.end(); ++i) {
            if (!i->empty() && ((*i)[0] != '#')) {
                result.push_back(*i);
            }
        }
        return result;
    }
    EagerConnectionFactory acceptor_;
    std::vector<std::string> streams_;
    size_t nConnects_;
};

void test_multiple_forward_agents() {
    boost::asio::io_service svc;

    FakeServer server(svc);

    const size_t count = 4;
    const size_t multiple = 5;

    Mmap *mm(NewMmap());
    boost::shared_ptr<StatServer> agent = makeServer(mm, svc, 0, server.address(), count);
    svc.poll();

    assert_equal(count, server.nConnects_);

    boost::shared_ptr<ConnectionInfo> ec = boost::make_shared<FakeEagerConnection>(boost::ref(svc));
    for (size_t i = 0; i < (count * multiple); ++i) {
        std::stringstream ss;
        ss << "something.different." << i << " 42";
        agent->handleCmd(ss.str(), ec);
    }
    complete comp;
    agent->syncAgent(&comp);

    svc.poll();
    assert_true(comp.complete_);

    for (size_t i = 0; i < count; ++i ) {
        assert_equal(multiple, server.lines(i).size());
        for (size_t j = 0; j < multiple; ++j) {
            assert_contains(server.lines(i)[j], "something.different");
        }
    }
}

void test_forward_to_prom_exporter()
{
    istat::FakeTime ft(1329345880);
    boost::asio::io_service svc;
    boost::shared_ptr<StatServer> server = makeServerWithPromExporter(svc);
    boost::shared_ptr<ConnectionInfo> ec = boost::make_shared<FakeEagerConnection>(boost::ref(svc));
    server->handleCmd("something.different 4242", ec);
    server->handleCmd("something.different.2 1329345870 42", ec);
    server->handleCmd("something.different.4 1329345872 4200 12345 0 3000 0", ec);
    server->handleCmd("something.different.3 1329345875 4242 12345 0 3000 2", ec);
    svc.poll();
    std::vector<PromMetric> res;
    std::vector<PromMetric> new_metrics;
    server->promExporter()->dumpMetrics(res, new_metrics);
    assert_equal(0, res.size());
    assert_equal(3, new_metrics.size());
    assert_equal(42, new_metrics[0].getValue());
    assert_equal(2121, new_metrics[1].getValue());
    assert_equal(4242, new_metrics[2].getValue());
}

void func() {
    test_collated_counters();
    test_multiple_counters();
    test_counter();
    test_handleInputData();
    test_to_double();
    test_blacklist();
    test_start_with_missing_blacklist();
    test_start_with_blacklist_disabled_due_to_0_period();
    test_multiple_forward_agents();
    test_forward_to_prom_exporter();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
