#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <set>
#include <tr1/unordered_map>
#include <istat/test.h>
#include <istat/istattime.h>
#include <boost/shared_ptr.hpp>
#include <boost/assign.hpp>
#include <boost/make_shared.hpp>
#include <istat/Log.h>

#include "../daemon/RequestInFlight.h"
#include "../daemon/HttpServer.h"
#include "../daemon/FakeHttpRequest.h"
#include "../daemon/FakeEagerConnection.h"

#define BUFSIZE 256

using namespace istat;

void sets_helper(std::multimap<int, std::string> &weights, std::set<std::string> &collect, int weight)
{
    std::pair< std::multimap<int, std::string>::iterator, std::multimap<int, std::string>::iterator> ret = weights.equal_range(weight);
    for( std::multimap<int, std::string>::iterator it = ret.first; it != ret.second; ++it)
    {
        collect.insert((*it).second);
    }
}

void get_heavy_helper(std::multimap<int, std::string> &weights, std::set<std::string> &collect)
{
    sets_helper(weights, collect, (*weights.rbegin()).first );
}


void assert_in_set(std::multimap<int, std::string> &weights, std::set<std::string> &set, int weight, std::string expect)
{
    sets_helper(weights, set, weight);
    assert_true(set.find(expect) != set.end());
}

void assert_in_heavy_set(std::multimap<int, std::string> &weights, std::set<std::string> &set, std::string expect)
{
    get_heavy_helper(weights, set);
    assert_true(set.find(expect) != set.end());
}

boost::shared_ptr<StatServer> makeServerWithPromExporter(boost::asio::io_service & svc)
{
    Blacklist::Configuration blacklistCfg = {};
    boost::shared_ptr<IPromExporter> promExporter = boost::make_shared<PromExporter>(boost::ref(svc), "");
    boost::shared_ptr<IStatStore> statStore = boost::make_shared<NullStatStore>();
    return boost::make_shared<StatServer>(0, "", "", 1, 1, boost::ref(blacklistCfg), boost::ref(svc), boost::ref(statStore), boost::ref(promExporter), 256, 256);
}

void test_parse_encoding() {
    std::set<std::string> mm = boost::assign::list_of("gzip")("deflate");

    std::set<std::string> set;

    std::string header = "gzip,deflate";
    AcceptEncodingHeader aeh(mm, &header);
    assert_equal(2, aeh.weights_.size());
    assert_in_set(aeh.weights_, set, 100, "gzip");
    assert_in_heavy_set(aeh.weights_, set, "gzip");

    header = "gzip;q=.9,deflate;q=1";
    aeh = AcceptEncodingHeader(mm, &header);
    assert_equal(2, aeh.weights_.size());
    assert_in_set(aeh.weights_, set, 100, "deflate");
    assert_in_heavy_set(aeh.weights_, set, "deflate");

    header = "gzip;q=0,deflate";
    aeh = AcceptEncodingHeader(mm, &header);
    assert_equal(2, aeh.weights_.size());
    assert_in_set(aeh.weights_, set, 100, "deflate");
    assert_in_set(aeh.weights_, set, 0, "gzip");
    assert_in_heavy_set(aeh.weights_, set, "deflate");

    header = "gzip;q=0,deflate;q=0.2";
    aeh = AcceptEncodingHeader(mm, &header);
    assert_equal(2, aeh.weights_.size());
    assert_in_set(aeh.weights_, set, 20, "deflate");
    assert_in_set(aeh.weights_, set, 0, "gzip");
    assert_in_heavy_set(aeh.weights_, set, "deflate");

    header = "gzip;q=0,deflate;q=0.2,sdhc,identity;q=.8";
    aeh = AcceptEncodingHeader(mm, &header);
    assert_equal(4, aeh.weights_.size());
    assert_in_set(aeh.weights_, set, 0, "gzip");
    assert_in_set(aeh.weights_, set, 20, "deflate");
    assert_in_set(aeh.weights_, set, 100, "sdhc");
    assert_in_set(aeh.weights_, set, 80, "identity");
    assert_in_heavy_set(aeh.weights_, set, "sdhc");

    header = "gzip;q=0,deflate;q=0.2,sdhc,identity;q=.8,compress;q=1";
    aeh = AcceptEncodingHeader(mm, &header);
    assert_equal(5, aeh.weights_.size());
    assert_in_set(aeh.weights_, set, 0, "gzip");
    assert_in_set(aeh.weights_, set, 20, "deflate");
    assert_in_set(aeh.weights_, set, 100, "sdhc");
    assert_in_set(aeh.weights_, set, 100, "compress");
    assert_in_set(aeh.weights_, set, 80, "identity");
    assert_in_heavy_set(aeh.weights_, set, "sdhc");

    header = "gzip;q=0,deflate;q=0.2,sdhc,identity;q=.8";
    aeh = AcceptEncodingHeader(mm, &header);
    assert_equal(4, aeh.weights_.size());
    assert_in_set(aeh.weights_, set, 0, "gzip");
    assert_in_set(aeh.weights_, set, 20, "deflate");
    assert_in_set(aeh.weights_, set, 100, "sdhc");
    assert_in_set(aeh.weights_, set, 80, "identity");
    assert_in_heavy_set(aeh.weights_, set, "sdhc");
    assert_in_heavy_set(aeh.weights_, set, "compress");

    header = "*,deflate";
    aeh = AcceptEncodingHeader(mm, &header);
    assert_equal(2, aeh.weights_.size());
    assert_in_set(aeh.weights_, set, 100, "deflate");
    assert_in_set(aeh.weights_, set, 100, "gzip");
    assert_in_heavy_set(aeh.weights_, set, "deflate");
    assert_in_heavy_set(aeh.weights_, set, "gzip");

    header = "*:q=0,deflate";
    aeh = AcceptEncodingHeader(mm, &header);
    assert_equal(2, aeh.weights_.size());
    assert_in_set(aeh.weights_, set, 100, "deflate");
    assert_in_set(aeh.weights_, set, 0, "gzip");
    assert_in_heavy_set(aeh.weights_, set, "deflate");

    header = "*:q=0,gzip,deflate";
    aeh = AcceptEncodingHeader(mm, &header);
    assert_equal(3, aeh.weights_.size());
    assert_in_set(aeh.weights_, set, 100, "deflate");
    assert_in_set(aeh.weights_, set, 100, "gzip");
    assert_in_set(aeh.weights_, set, 0, "gzip");
    assert_in_heavy_set(aeh.weights_, set, "deflate");
    assert_in_heavy_set(aeh.weights_, set, "gzip");
}

void test_choose_encoding() {
    boost::asio::io_service svc;
    HttpRequestHolder htr(boost::shared_ptr<IHttpRequest>((HttpRequest *)0));
    boost::shared_ptr<RequestInFlight> rif = boost::make_shared<RequestInFlight>(htr.p_, boost::ref(svc), "files");

    std::set<std::string> mm = boost::assign::list_of("gzip")("deflate");
    std::set<std::string> set;

    std::string header = "gzip,deflate";
    AcceptEncodingHeader aeh(mm, &header);
    rif->chooseEncoding(aeh);
    assert_equal("gzip", rif->hdrs_["Content-encoding"]);

    rif->hdrs_.clear();
    header = "deflate";
    aeh = AcceptEncodingHeader(mm, &header);
    rif->chooseEncoding(aeh);
    assert_equal("deflate", rif->hdrs_["Content-encoding"]);

    rif->hdrs_.clear();
    header = "deflate;q=0";
    aeh = AcceptEncodingHeader(mm, &header);
    rif->chooseEncoding(aeh);
    assert_equal("", rif->hdrs_["Content-encoding"]);

    rif->hdrs_.clear();
    header = "";
    aeh = AcceptEncodingHeader(mm, &header);
    rif->chooseEncoding(aeh);
    assert_equal("", rif->hdrs_["Content-encoding"]);

    rif->hdrs_.clear();
    aeh = AcceptEncodingHeader(mm, (std::string*)0);
    rif->chooseEncoding(aeh);
    assert_equal("", rif->hdrs_["Content-encoding"]);

    rif->hdrs_.clear();
    header = "*,deflate";
    aeh = AcceptEncodingHeader(mm, &header);
    rif->chooseEncoding(aeh);
    assert_equal("gzip", rif->hdrs_["Content-encoding"]);

    rif->hdrs_.clear();
    header = "*";
    aeh = AcceptEncodingHeader(mm, &header);
    rif->chooseEncoding(aeh);
    assert_equal("gzip", rif->hdrs_["Content-encoding"]);

    rif->hdrs_.clear();
    header = "*;q=0,deflate";
    aeh = AcceptEncodingHeader(mm, &header);
    rif->chooseEncoding(aeh);
    assert_equal("deflate", rif->hdrs_["Content-encoding"]);

    rif->hdrs_.clear();
    header = "gzip,deflate,sdhc";
    AcceptEncodingHeader::disallow_compressed_responses = true;
    aeh = AcceptEncodingHeader(mm, &header);
    rif->chooseEncoding(aeh);
    assert_equal("", rif->hdrs_["Content-encoding"]);
}

void test_get_method_with_prom_exporter_200_reply()
{
    istat::FakeTime ft(1329345880);
    boost::asio::io_service svc;
    boost::shared_ptr<StatServer> server = makeServerWithPromExporter(svc);
    std::string url("/metrics");
    boost::shared_ptr<IHttpRequest> req = boost::make_shared<FakeHttpRequest>(url, "GET");
    boost::shared_ptr<RequestInFlight> rif = boost::make_shared<RequestInFlight>(req, server.get());
    boost::shared_ptr<ConnectionInfo> ec = boost::make_shared<FakeEagerConnection>(boost::ref(svc));
    server->handleCmd("something.different 4242", ec);
    svc.poll();
    rif->goProm();
    assert_equal("# TYPE something_different gauge\nsomething_different 4242 1329345880000\n", ((FakeHttpRequest*)req.get())->theReply_);
    assert_equal(200, ((FakeHttpRequest*)req.get())->code());
}

void test_get_method_with_prom_exporter_400_reply()
{
    boost::asio::io_service svc;
    boost::shared_ptr<StatServer> server = makeServerWithPromExporter(svc);
    std::string url("/files");
    boost::shared_ptr<IHttpRequest> req = boost::make_shared<FakeHttpRequest>(url, "GET");
    boost::shared_ptr<RequestInFlight> rif = boost::make_shared<RequestInFlight>(req, server.get());
    rif->goProm();
    assert_equal(400, ((FakeHttpRequest*)req.get())->code());
}

void test_unknown_method_with_prom_exporter()
{
    boost::asio::io_service svc;
    boost::shared_ptr<StatServer> server = makeServerWithPromExporter(svc);
    std::string url("/metrics");
    boost::shared_ptr<IHttpRequest> req = boost::make_shared<FakeHttpRequest>(url, "POST");
    boost::shared_ptr<RequestInFlight> rif = boost::make_shared<RequestInFlight>(req, server.get());
    rif->goProm();
    assert_equal(405, ((FakeHttpRequest*)req.get())->code());
}

void func() {
    test_parse_encoding();
    test_choose_encoding();
    test_get_method_with_prom_exporter_200_reply();
    test_get_method_with_prom_exporter_400_reply();
    test_unknown_method_with_prom_exporter();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
