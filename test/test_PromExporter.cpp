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

void storeMetrics(boost::shared_ptr<PromExporter> pe, std::string const & name, time_t t, double v)
{
    std::vector<std::string> ctrs(1, name);
    pe->storeMetrics(name, ctrs, t, v);
}

void test_prom_exporter()
{
    istat::FakeTime ft(1329345880);
    boost::asio::io_service svc;
    boost::shared_ptr<PromExporter> pe = boost::make_shared<PromExporter>(boost::ref(svc));
    std::vector<PromMetric> res;
    std::vector<PromMetric> new_metrics;

    //test storeMetrics
    storeMetrics(pe, "*foo.bar", 1329345850, 1);
    storeMetrics(pe, "x.0y", 1329345820, 0.5);
    storeMetrics(pe, "foo.bar.baz", 1329345880, 2.5);
    storeMetrics(pe, "foo.bar.baz", 1329345820, 2.0);
    PromExporter::PromGaugeMap::iterator it = pe->data_gauges_.begin();
    PromExporter::CumulativeCountsMap::iterator cit = pe->data_counters_.begin();
    assert_equal(3, pe->data_gauges_.size());
    assert_equal(1329345820, it->first);
    assert_equal(PromMetric::PromTypeGauge, it->second.getType());
    assert_equal(1329345820, (++it)->first);
    assert_equal(PromMetric::PromTypeGauge, it->second.getType());
    assert_equal(1329345880, (++it)->first);
    assert_equal(1, pe->data_counters_.size());
    assert_equal(true, cit->second.getCounterStatus());
    assert_equal(PromMetric::PromTypeCounter, cit->second.getType());
    assert_equal(1, cit->second.getValue());
    //test counter update
    storeMetrics(pe, "*foo.bar", 1329345845, 1);
    assert_equal(1, pe->data_counters_.size());
    assert_equal(2, cit->second.getValue());
    assert_equal(1329345850, cit->second.getTimestamp());

    //test dump
    pe->dumpMetrics(res, new_metrics);
    //foo_bar, x_0y, foo_bar_baz
    assert_equal(3, new_metrics.size());
    res.clear();
    new_metrics.clear();
    storeMetrics(pe, "*foo", 1329345850, 1);
    storeMetrics(pe, "x.y", 1329345820, 0.5);
    storeMetrics(pe, "x.y", 1329345860, 1.5);
    pe->dumpMetrics(res, new_metrics);
    assert_equal(1, res.size());
    assert_equal(2, new_metrics.size());
    assert_equal("foo", (new_metrics.begin())->getName());
    assert_equal("x_y", (++(new_metrics.begin()))->getName());
    assert_equal("x_y 0.5 1329345820000\n", new_metrics[1].toString());
    assert_equal("# TYPE x_y gauge\n", new_metrics[1].typeString());
    assert_equal("foo 1 1329345850000\n", new_metrics[0].toString());
    assert_equal("# TYPE foo counter\n", new_metrics[0].typeString());
    assert_equal(false, cit->second.getCounterStatus());
    new_metrics.clear();
    res.clear();
    pe->dumpMetrics(res, new_metrics);
    assert_equal(0, res.size());
    assert_equal(0, new_metrics.size());
    assert_equal(2, pe->data_counters_.size());

    //test onCleanup
    storeMetrics(pe, "*foo.bar", 1329345855, 5);
    storeMetrics(pe, "x.0y", 1329345850, 0.5);
    storeMetrics(pe, "*foo.bar", 1329345819, 2);
    storeMetrics(pe, "x.0y", 1329345821, 1);
    storeMetrics(pe, "foo.bar.baz", 1329345819, 2.0);
    storeMetrics(pe, "foo.bar.baz", 1329345880, 2.5);
    pe->onCleanup();
    assert_equal(3, pe->data_gauges_.size());
    it = pe->data_gauges_.begin();
    assert_equal(1, it->second.getValue());
    assert_equal(0.5, (++it)->second.getValue());
    assert_equal(2.5, (++it)->second.getValue());
    assert_equal(9, cit->second.getValue());
    assert_equal(1329345855, cit->second.getTimestamp());
    assert_equal(true, cit->second.getCounterStatus());

    // test extract_tags
    pe->dumpMetrics(res, new_metrics);
    std::vector<std::string> ctrs;
    ctrs.push_back("x.y.z.host.h123.");
    ctrs.push_back("x.y.z.role.r123");
    ctrs.push_back("x.y.z.a-b.ab123.x.y.z");
    ctrs.push_back("x.y.z.a-b");
    ctrs.push_back("x.y.z.a-b.");
    ctrs.push_back(".x.y.z");
    ctrs.push_back("x.y.");
    ctrs.push_back("x.y.z_");
    pe->storeMetrics("x.y.z", ctrs, 1329345860, 0.2);
    storeMetrics(pe, "*x.y.host.", 1329345875, 1);
    pe->storeMetrics("", std::vector<std::string>(1,"*x.y.host."), 1329345880, 3);
    pe->storeMetrics("x.y.z", ctrs, 1329345865, 0.4);
    res.clear();
    new_metrics.clear();
    pe->dumpMetrics(res, new_metrics);
    assert_equal(3, pe->data_counters_.size()); //foo.bar, foo, x_y_host_
    assert_equal(3, res.size());
    assert_equal(4, new_metrics.size());
    assert_equal("# TYPE x_y_host_ counter\n", new_metrics[0].typeString());
    assert_equal("x_y_host_ 4 1329345880000\n", new_metrics[0].toString());
    assert_equal("# TYPE x_y_z gauge\n", new_metrics[1].typeString());
    assert_equal("x_y_z{host=\"h123_\",role=\"r123\",a_b=\"ab123_x_y_z\"} 0.2 1329345860000\n", new_metrics[1].toString());
    assert_equal("# TYPE x_y_z_a_b gauge\n", new_metrics[2].typeString());
    assert_equal("x_y_z_a_b 0.2 1329345860000\n", new_metrics[2].toString());
    assert_equal("# TYPE x_y_z_a_b_ gauge\n", new_metrics[3].typeString());
    assert_equal("x_y_z_a_b_ 0.2 1329345860000\n", new_metrics[3].toString());
    assert_equal("x_y_z{host=\"h123_\",role=\"r123\",a_b=\"ab123_x_y_z\"} 0.4 1329345865000\n", res[0].toString());
    assert_equal("x_y_z_a_b 0.4 1329345865000\n", res[1].toString());
    assert_equal("x_y_z_a_b_ 0.4 1329345865000\n", res[2].toString());
}

void func() {
    test_prom_exporter();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
