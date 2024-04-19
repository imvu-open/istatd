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
    return boost::make_shared<PromExporter>(boost::ref(svc));
}

void test_prom_exporter()
{
    istat::FakeTime ft(1329345880);
    boost::asio::io_service svc;
    boost::shared_ptr<PromExporter> pe = boost::make_shared<PromExporter>(boost::ref(svc));
    std::vector<PromMetric> res;
    std::vector<PromMetric> new_metrics;

    //test storeMetrics
    pe->storeMetrics("*foo.bar", 1329345850, 1);
    pe->storeMetrics("x.0y", 1329345820, 0.5);
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
    pe->storeMetrics("x.y", 1329345820, 0.5);
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
    pe->storeMetrics("x.0y", 1329345850, 0.5);
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
    pe->storeMetrics("x.y.host.h123_", 1329345860, 0.2);
    pe->storeMetrics("x.y.host.h123.", 1329345865, 0.2);
    pe->storeMetrics("*x.y.host.", 1329345875, 1);
    pe->storeMetrics("x.y.role.rr123-1", 1329345855, 0.4);
    pe->storeMetrics("x.y.class.ccc123", 1329345870, 0.3);
    pe->storeMetrics("x.y.cluster.cl123", 1329345880, 0.3);
    pe->storeMetrics("x.y.proxy-pool.pp123", 1329345885, 0.3);
    pe->storeMetrics("x.y.pool.p123", 1329345890, 0.3);
    res.clear();
    new_metrics.clear();
    pe->dumpMetrics(res, new_metrics);
    assert_equal(8, res.size());
    assert_equal(2, new_metrics.size());
    assert_equal("x_y_host_h123_", (new_metrics.begin())->getName());
    assert_equal("x_y_host_", new_metrics[1].getName());
    assert_equal("x_y{\"role\"=\"rr123_1\"} 0.4 1329345855000\n", res[0].toString());
    assert_equal("x_y{\"host\"=\"h123_\"} 0.2 1329345860000\n", res[1].toString());
    assert_equal("x_y_host_h123_ 0.2 1329345865000\n", res[2].toString());
    assert_equal("x_y{\"class\"=\"ccc123\"} 0.3 1329345870000\n", res[3].toString());
    assert_equal("x_y_host_ 1 1329345875000\n", res[4].toString());
    assert_equal("x_y{\"cluster\"=\"cl123\"} 0.3 1329345880000\n", res[5].toString());
    assert_equal("x_y{\"proxy-pool\"=\"pp123\"} 0.3 1329345885000\n", res[6].toString());
    assert_equal("x_y{\"pool\"=\"p123\"} 0.3 1329345890000\n", res[7].toString());
}

void func() {
    test_prom_exporter();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
