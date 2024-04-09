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
    assert_equal(PromMetric::PromTypeGauge, it->second.getType());
    assert_equal(1329345820, (++it)->first);
    assert_equal(1329345850, (++it)->first);
    assert_equal(PromMetric::PromTypeCounter, it->second.getType());
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

    // test tags
    res.clear();
    new_metrics.clear();
    pe->dumpMetrics(res, new_metrics);
    pe->storeMetrics("x_y_host_h123", 1329345860, 0.2);
    pe->storeMetrics("x_y_host_h123_", 1329345865, 0.2);
    pe->storeMetrics("*x_y_host_", 1329345875, 1);
    pe->storeMetrics("x_y_role_rr123", 1329345855, 0.4);
    pe->storeMetrics("x_y_class_ccc123", 1329345870, 0.3);
    res.clear();
    new_metrics.clear();
    pe->dumpMetrics(res, new_metrics);
    assert_equal(5, res.size());
    assert_equal(2, new_metrics.size());
    assert_equal("x_y_host_h123_", (new_metrics.begin())->getName());
    assert_equal("x_y_host_", new_metrics[1].getName());
    assert_equal("x_y{\"role\"=\"rr123\"} 0.4 1329345855000\n", res[0].toString());
    assert_equal("x_y{\"host\"=\"h123\"} 0.2 1329345860000\n", res[1].toString());
    assert_equal("x_y_host_h123_ 0.2 1329345865000\n", res[2].toString());
    assert_equal("x_y{\"class\"=\"ccc123\"} 0.3 1329345870000\n", res[3].toString());
    assert_equal("x_y_host_ 1 1329345875000\n", res[4].toString());
}

void func() {
    test_prom_exporter();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
