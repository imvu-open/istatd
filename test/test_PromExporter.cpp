#include <boost/asio.hpp>
#include <boost/ref.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <istat/test.h>
#include <istat/istattime.h>
#include <istat/Log.h>

#include "../daemon/PromExporter.h"

using namespace istat;

boost::shared_ptr<PromExporter> makePromExporter()
{
    boost::asio::io_service svc;
    return boost::make_shared<PromExporter>(boost::ref(svc), true);
}

void test_prom_exporter()
{
    istat::FakeTime ft(1329345880);
    boost::shared_ptr<PromExporter> pe = makePromExporter();
    pe->storeMetrics("*foo.bar", 1329345850, 1);
    pe->storeMetrics("x_0y", 1329345820, 0.5);
    std::vector<PromMetric> res;
    pe->dumpMetrics(res);
    assert_equal(2, res.size());
    assert_equal("x_0y 0.5 1329345820000\n", res[0].toString());
    assert_equal("foo_bar 1 1329345850000\n", res[1].toString());
    assert_equal(PromTypeGauge, res[0].getType());
    assert_equal(PromTypeCounter, res[1].getType());
    res.clear();
    pe->dumpMetrics(res);
    assert_equal(0, res.size());
}

void func() {
    test_prom_exporter();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
