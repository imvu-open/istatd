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
    boost::asio::io_service svc;
    boost::shared_ptr<PromExporter> pe = boost::make_shared<PromExporter>(boost::ref(svc), true);
    std::vector<PromMetric> res;
    std::vector<PromMetric> new_metrics;

    //test storeMetrics
    pe->storeMetrics("*foo.bar", 1329345850, 1);
    pe->storeMetrics("x_0y", 1329345820, 0.5);
    pe->storeMetrics("foo.bar.baz", 1329345880, 2.5);
    pe->storeMetrics("foo.bar.baz", 1329345820, 1.5);
    assert_equal(4, pe->data_.size());
    PromExporter::PromDataMap::iterator it = pe->data_.begin();
    assert_equal(1329345820, it->first);
    assert_equal(PromTypeGauge, it->second.getType());
    assert_equal(1329345820, (++it)->first);
    assert_equal(1329345850, (++it)->first);
    assert_equal(PromTypeCounter, it->second.getType());
    assert_equal(1329345880, (++it)->first);
    assert_equal(0, pe->metric_type_map_.size());

    //test dump
    pe->dumpMetrics(res, new_metrics);
    assert_equal(3, new_metrics.size());
    //foo_bar, x_0y, foo_bar_baz
    assert_equal(3, pe->metric_type_map_.size());
    res.clear();
    new_metrics.clear();
    pe->storeMetrics("*foo.bar", 1329345850, 1);
    pe->storeMetrics("x_y", 1329345820, 0.5);
    assert_equal(3, pe->metric_type_map_.size());
    pe->dumpMetrics(res, new_metrics);
    assert_equal(2, res.size());
    assert_equal(1, new_metrics.size());
    assert_equal("x_y", (new_metrics.begin())->getName());
    assert_equal("x_y 0.5 1329345820000\n", res[0].toString());
    assert_equal("# TYPE x_y gauge\n", res[0].typeString());
    assert_equal("foo_bar 1 1329345850000\n", res[1].toString());
    assert_equal("# TYPE foo_bar counter\n", res[1].typeString());
    res.clear();
    new_metrics.clear();
    pe->dumpMetrics(res, new_metrics);
    assert_equal(0, res.size());
    assert_equal(0, new_metrics.size());
    //foo_bar, x_0y, foo_bar_baz, x_y
    assert_equal(4, pe->metric_type_map_.size());
    
    //test onCleanup
    pe->storeMetrics("*foo.bar", 1329345550, 5);
    pe->storeMetrics("x_0y", 1329345850, 0.5);
    pe->storeMetrics("*foo.bar", 1329345819, 2);
    pe->storeMetrics("*foo.bar", 1329345821, 1);
    pe->storeMetrics("foo.bar.baz", 1329345819, 2.0);
    pe->storeMetrics("foo.bar.baz", 1329345880, 2.5);
    pe->onCleanup();
    assert_equal(3, pe->data_.size());
    it = pe->data_.begin();
    assert_equal(1, it->second.getValue());
    assert_equal(0.5, (++it)->second.getValue());
    assert_equal(2.5, (++it)->second.getValue());
    assert_equal(4, pe->metric_type_map_.size());
}

void func() {
    test_prom_exporter();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
