#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <tr1/unordered_map>
#include <istat/test.h>
#include <istat/istattime.h>
#include <boost/shared_ptr.hpp>
#include <istat/Log.h>

#include "../daemon/StatServer.h"
#include "../include/istat/Mmap.h"
#include "../daemon/StatCounterFactory.h"
#include "../daemon/StatStore.h"
#include "../daemon/Retention.h"
#include "../daemon/FakeEagerConnection.h"
#include "../daemon/Blacklist.h"

#define BUFSIZE 256

using namespace istat;


RetentionPolicy rp("10s:1d,5m:2d,1h:1y");
RetentionPolicy xrp("");

boost::shared_ptr<StatServer> makeServer(Mmap *mm, boost::asio::io_service &svc) {
    int port = 0;
    std::string agent("");

    Blacklist::Configuration blacklistCfg = {};
    blacklistCfg.path = std::string("");
    blacklistCfg.period = 0;
    std::string listenAddress("");
    std::string storePath("/tmp/test/testdir");
    //Mmap *mm(NewMmap());
    boost::shared_ptr<IStatCounterFactory> statCounterFactory(new StatCounterFactory(storePath, mm, rp));
    boost::shared_ptr<IStatStore> statStore(new StatStore(storePath, getuid(), svc, statCounterFactory, mm));
    statStore->setAggregateCount(2);
    return boost::shared_ptr<StatServer>(new StatServer(port, listenAddress, agent, 1, blacklistCfg, svc, statStore));
}

boost::shared_ptr<StatServer> makeServerWithBlacklist(Mmap *mm, boost::asio::io_service &svc) {
    int port = 0;
    std::string agent("");
    std::string blacklist();
    Blacklist::Configuration blacklistCfg = {};
    blacklistCfg.path = std::string("/tmp/test/blacklist/ss_blacklist.set");
    blacklistCfg.period = 10;
    std::string listenAddress("");
    std::string storePath("/tmp/test/testdir");
    //Mmap *mm(NewMmap());
    boost::shared_ptr<IStatCounterFactory> statCounterFactory(new StatCounterFactory(storePath, mm, rp));
    boost::shared_ptr<IStatStore> statStore(new StatStore(storePath, getuid(), svc, statCounterFactory, mm));
    statStore->setAggregateCount(2);
    return boost::shared_ptr<StatServer>(new StatServer(port, listenAddress, agent, 1, blacklistCfg, svc, statStore));
}

void test_counter() {
    istat::FakeTime ft(1329345880);
    boost::asio::io_service svc;
    boost::shared_ptr<StatServer> server;
    Mmap *mm(NewMmap());
    server = makeServer(mm, svc);
    boost::shared_ptr<ConnectionInfo> ec(new FakeEagerConnection(svc));

    boost::shared_ptr<IStatCounter> statCounter;
    boost::asio::strand* strand = 0;
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
    boost::shared_ptr<ConnectionInfo> ec(new FakeEagerConnection(svc));

    boost::shared_ptr<IStatCounter> statCounter;
    boost::asio::strand* strand;
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
    boost::shared_ptr<ConnectionInfo> ec(new FakeEagerConnection(svc));

    boost::shared_ptr<IStatCounter> statCounter;
    boost::asio::strand* strand;
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
    server = makeServerWithBlacklist(mm, svc);
    boost::shared_ptr<ConnectionInfo> ec(new FakeEagerConnection(svc));

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
    boost::asio::strand* strand = 0;
    server->handleCmd("i.should.be.blacklisted 4242", ec);
    svc.poll();

    // Advance time so we include the data we just recorded in the returned results.
    ft.set(istattime(0) + 10);
    server->store()->find("i.should.be.blacklisted", statCounter, strand);

    assert_equal(boost::shared_ptr<IStatCounter>((IStatCounter*)0), statCounter);
    assert_equal(false, ec->opened());

    boost::shared_ptr<ConnectionInfo> ecg(new FakeEagerConnection(svc));

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
    boost::asio::strand* strandG = 0;
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
    server = makeServerWithBlacklist(mm, svc);
    boost::shared_ptr<ConnectionInfo> ec(new FakeEagerConnection(svc));

    boost::shared_ptr<IStatCounter> statCounter;
    boost::asio::strand* strand = 0;
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

void func() {
    test_collated_counters();
    test_multiple_counters();
    test_counter();
    test_handleInputData();
    test_to_double();
    test_blacklist();
    test_start_with_missing_blacklist();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
