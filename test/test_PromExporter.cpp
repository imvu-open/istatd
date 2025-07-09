#include <algorithm>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/ref.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <istat/test.h>
#include <istat/istattime.h>
#include <istat/Log.h>
#include <istat/strfunc.h>

#include "../daemon/PromExporter.h"

using namespace istat;

void storeMetrics(boost::shared_ptr<PromExporter> pe, std::string const & name, time_t t, double v)
{
    std::vector<std::string> ctrs(1, name);
    pe->storeMetrics(name, name, ctrs, t, v);
}

bool comparePromMetric(PromMetric & a, PromMetric & b)
{
    return a.getName() < b.getName();
}

void compareMetricString(std::string const & a, std::string const & b)
{
    std::vector<std::string> as, bs;
    istat::explode(a, '\n', as);
    istat::explode(b, '\n', bs);
    assert_equal(as.size(), bs.size());
    for (size_t i = 0; i < as.size(); i++)
    {
        std::vector<std::string> afields, bfields;
        istat::explode(as[i], ' ', afields);
        assert_equal(afields.size(), 3);
        istat::explode(bs[i], ' ', bfields);
        assert_equal(bfields.size(), 3);
        std::sort(afields[0].begin(), afields[0].end());
        std::sort(bfields[0].begin(), bfields[0].end());
        assert_equal(afields[0], bfields[0]);
        assert_equal(afields[1], bfields[1]);
        assert_equal(afields[2], bfields[2]);
    }
}

void test_prom_exporter()
{
    istat::FakeTime ft(1329345881);
    boost::asio::io_service svc;
    boost::shared_ptr<PromExporter> pe = boost::make_shared<PromExporter>(boost::ref(svc), "", true, -1, 30);
    std::vector<PromMetric> res;
    std::vector<PromMetric> new_metrics;

    //test storeMetrics
    storeMetrics(pe, "*foo.bar", 1329345850, 1);
    storeMetrics(pe, "x.0y", 1329345819, 0.5);
    storeMetrics(pe, "foo.bar.baz", 1329345880, 2.0);
    storeMetrics(pe, "foo.bar.baz", 1329345820, 2.5);
    PromExporter::PromGaugeMap::iterator it = pe->data_gauges_.begin();
    PromExporter::CumulativeCountsMap::iterator cit = pe->data_counters_.begin();
    assert_equal(3, pe->data_gauges_.size());
    assert_equal(1329345819, it->first);
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
    storeMetrics(pe, "x.y", 1329345815, 0.5);
    storeMetrics(pe, "x.y", 1329345860, 1.5);
    pe->dumpMetrics(res, new_metrics);
    assert_equal(2, res.size());
    assert_equal(4, new_metrics.size());
    assert_equal("foo_bar{counter=\"1\"} 2 1329345880000\n", new_metrics[0].toString());
    assert_equal("x_y", new_metrics[1].getName());
    assert_equal("x_0y", new_metrics[2].getName());
    assert_equal("foo_bar_baz", new_metrics[3].getName());
    assert_equal("x_y 1.5 1329345860000\n", res[0].toString());
    assert_equal("foo_bar_baz 2 1329345880000\n", res[1].toString());
    assert_equal(1, pe->data_counters_.size());

    //test onCleanup
    pe->data_gauges_.clear();
    storeMetrics(pe, "*foo.bar", 1329345855, 5);
    storeMetrics(pe, "x.0y", 1329345851, 0.5);
    storeMetrics(pe, "*foo.bar", 1329345819, 2);
    storeMetrics(pe, "x.0y", 1329345821, 1);
    storeMetrics(pe, "foo.bar.baz", 1329345819, 2.0);
    storeMetrics(pe, "foo.bar.baz", 1329345880, 0.2);
    pe->onCleanup();
    assert_equal(2, pe->data_gauges_.size());
    it = pe->data_gauges_.begin();
    assert_equal(0.5, (it)->second.getValue());
    assert_equal(0.2, (++it)->second.getValue());
    assert_equal(9, cit->second.getValue());
    assert_equal(1329345855, cit->second.getTimestamp());
    assert_equal(true, cit->second.getCounterStatus());

    // test extract_tags gauge
    pe->data_gauges_.clear();
    pe->dumpMetrics(res, new_metrics);
    std::vector<std::string> ctrs;
    std::string ctr = "x.y.z^host.h123^role.r123^role.r456-1^a-b^a-b.^a-b.ab123.x.y.z";
    ctrs.push_back("x.y.z.host.h123.");
    ctrs.push_back("x.y.z.role.r123");
    ctrs.push_back("x.y.z.role.r456-1");
    ctrs.push_back("x.y.z.a-b.ab123.x.y.z");
    ctrs.push_back("x.y.z.a-b");
    ctrs.push_back("x.y.z.a-b.");
    ctrs.push_back(".x.y.z");
    ctrs.push_back("x.y.");
    ctrs.push_back("x.y.z_host");
    pe->storeMetrics(ctr, "x.y.z", ctrs, 1329345860, 0.2);
    storeMetrics(pe, "*x.y.host.", 1329345875, 1);
    pe->storeMetrics("*x.y.host.", "", std::vector<std::string>(1,"*x.y.host."), 1329345880, 3);
    pe->storeMetrics(ctr, "x.y.z", ctrs, 1329345865, 0.4);
    res.clear();
    new_metrics.clear();
    pe->dumpMetrics(res, new_metrics);
    assert_equal(2, pe->data_counters_.size()); //foo.bar, x_y_host_
    assert_equal(4, res.size());
    assert_equal(6, new_metrics.size());
    assert_equal("# TYPE x_y_host_ counter\n", new_metrics[0].typeString());
    assert_equal("x_y_host_{counter=\"1\"} 4 1329345880000\n", new_metrics[0].toString());
    assert_equal("# TYPE foo_bar counter\n", new_metrics[1].typeString());
    assert_equal("foo_bar{counter=\"1\"} 9 1329345880000\n", new_metrics[1].toString());
    assert_equal("# TYPE x_y_z gauge\n", new_metrics[2].typeString());
    compareMetricString(
            "x_y_z{host=\"h123.\",role=\"r123\"} 0.2 1329345860000\nx_y_z{role=\"r456-1\"} 0.2 1329345860000\n",
            new_metrics[2].toString());
    assert_equal("# TYPE x_y_z_a_b_ab123_x_y_z gauge\n", new_metrics[3].typeString());
    assert_equal("x_y_z_a_b_ab123_x_y_z 0.2 1329345860000\n", new_metrics[3].toString());
    assert_equal("# TYPE x_y_z_a_b gauge\n", new_metrics[4].typeString());
    assert_equal("x_y_z_a_b 0.2 1329345860000\n", new_metrics[4].toString());
    assert_equal("# TYPE x_y_z_a_b_ gauge\n", new_metrics[5].typeString());
    assert_equal("x_y_z_a_b_ 0.2 1329345860000\n", new_metrics[5].toString());
    compareMetricString(
            "x_y_z{host=\"h123.\",role=\"r123\"} 0.4 1329345865000\nx_y_z{role=\"r456-1\"} 0.4 1329345865000\n",
            res[0].toString());
    assert_equal("x_y_z_a_b_ab123_x_y_z 0.4 1329345865000\n", res[1].toString());
    assert_equal("x_y_z_a_b 0.4 1329345865000\n", res[2].toString());
    assert_equal("x_y_z_a_b_ 0.4 1329345865000\n", res[3].toString());

    // test extract_tags counters
    pe->data_gauges_.clear();
    ctrs.clear();
    ctr = "*a.b.c^class.c123^pool.p123^abc^class.c456^a.b.c";
    ctrs.push_back("*a.b.c.class.c123");
    ctrs.push_back("*a.b.c.pool.p123");
    ctrs.push_back("*a.b.c.abc");
    ctrs.push_back("*a.b.c.class.c456");
    ctrs.push_back("*a.b.c.a.b.c");
    pe->storeMetrics(ctr, "*a.b.c", ctrs, 1329345870, 1);
    pe->storeMetrics(ctr, "*a.b.c", ctrs, 1329345875, 1);
    storeMetrics(pe, "*foo.bar", 1329345875, 1);
    res.clear();
    new_metrics.clear();
    pe->dumpMetrics(res, new_metrics);
    assert_equal(5, pe->data_counters_.size());
    assert_equal(0, res.size());
    assert_equal(5, new_metrics.size());
    std::sort(new_metrics.begin(), new_metrics.end(), comparePromMetric);
    assert_equal("# TYPE a_b_c counter\n", new_metrics[0].typeString());
    compareMetricString(
            "a_b_c{class=\"c123\",pool=\"p123\",counter=\"1\"} 2 1329345880000\na_b_c{class=\"c456\",counter=\"1\"} 2 1329345880000\n",
            new_metrics[0].toString());
    assert_equal("# TYPE a_b_c_a_b_c counter\n", new_metrics[1].typeString());
    assert_equal("a_b_c_a_b_c{counter=\"1\"} 2 1329345880000\n", new_metrics[1].toString());
    assert_equal("# TYPE a_b_c_abc counter\n", new_metrics[2].typeString());
    assert_equal("a_b_c_abc{counter=\"1\"} 2 1329345880000\n", new_metrics[2].toString());
    assert_equal("# TYPE foo_bar counter\n", new_metrics[3].typeString());
    assert_equal("foo_bar{counter=\"1\"} 10 1329345880000\n", new_metrics[3].toString());
    assert_equal("# TYPE x_y_host_ counter\n", new_metrics[4].typeString());
    assert_equal("x_y_host_{counter=\"1\"} 4 1329345880000\n", new_metrics[4].toString());

    //test remove stale counters
    size_t expected = pe->data_counters_.size();
    storeMetrics(pe, "*a_stale", 1329245850, 1);
    storeMetrics(pe, "*z_stale", 1329259475, 1);
    assert_equal(expected+2, pe->data_counters_.size());
    pe->onRemoveStaleCounter();
    assert_equal(expected, pe->data_counters_.size());
    assert_equal(true, pe->data_counters_.end() == pe->data_counters_.find("a_stale"));
    assert_equal(true, pe->data_counters_.end() == pe->data_counters_.find("z_stale"));

    //test mamimum meric count
    pe->data_gauges_.clear();
    pe->data_counters_.clear();
    pe->max_metric_count_ = 2;
    storeMetrics(pe, "*a.ok.1", 1329345850, 1);
    storeMetrics(pe, "*a.ok.2", 1329345850, 1);
    storeMetrics(pe, "z.ok", 1329345870, 1);
    storeMetrics(pe, "z.dropped", 1329345880, 1);
    storeMetrics(pe, "*y.dropped", 1329345880, 1);
    assert_equal(3, pe->data_counters_.size());
    assert_equal(1, pe->data_gauges_.size());
    std::string dropped_counter("*istatd.agent.dropped.metrics");
    PromExporter::CumulativeCountsMap::iterator ait = pe->data_counters_.find(dropped_counter);
    assert_equal(true, pe->data_counters_.end() != ait);
    assert_equal(2, ait->second.getValue());
    assert_equal(1329345881, ait->second.getTimestamp());
    assert_equal("istatd_agent_dropped_metrics", ait->second.getName());
}

void test_prom_exporter_no_name_mapping()
{
    std::string config_file = "/tmp/test/config/allowed_tags.txt";
    {
        mkdir("/tmp/test/config", 511);
        std::ofstream ofs(config_file.c_str(), std::ofstream::out);
        ofs << "a.b.c\npo.ol\n";
    }
    istat::FakeTime ft(1329345881);
    boost::asio::io_service svc;
    boost::shared_ptr<PromExporter> pe = boost::make_shared<PromExporter>(boost::ref(svc), config_file, false);
    std::vector<PromMetric> res;
    std::vector<PromMetric> new_metrics;
    pe->max_metric_count_ = -1;

    //test storeMetrics
    storeMetrics(pe, "*foo.bar", 1329345850, 1);
    storeMetrics(pe, "x.0y", 1329345819, 0.5);
    storeMetrics(pe, "foo.bar.baz", 1329345880, 2.0);
    storeMetrics(pe, "foo.bar.baz", 1329345820, 2.5);
    PromExporter::PromGaugeMap::iterator it = pe->data_gauges_.begin();
    PromExporter::CumulativeCountsMap::iterator cit = pe->data_counters_.begin();
    assert_equal(3, pe->data_gauges_.size());
    assert_equal(1329345819, it->first);
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
    storeMetrics(pe, "x.y", 1329345815, 0.5);
    storeMetrics(pe, "x.y", 1329345860, 1.5);
    pe->dumpMetrics(res, new_metrics);
    assert_equal(2, res.size());
    assert_equal(4, new_metrics.size());
    assert_equal("foo.bar{counter=\"1\"} 2 1329345880000\n", new_metrics[0].toString());
    assert_equal("x.y", new_metrics[1].getName());
    assert_equal("x.0y", new_metrics[2].getName());
    assert_equal("foo.bar.baz", new_metrics[3].getName());
    assert_equal("x.y 1.5 1329345860000\n", res[0].toString());
    assert_equal("foo.bar.baz 2 1329345880000\n", res[1].toString());
    assert_equal(1, pe->data_counters_.size());

    //test onCleanup
    pe->data_gauges_.clear();
    storeMetrics(pe, "*foo.bar", 1329345855, 5);
    storeMetrics(pe, "x.0y", 1329345851, 0.5);
    storeMetrics(pe, "*foo.bar", 1329345819, 2);
    storeMetrics(pe, "x.0y", 1329345821, 1);
    storeMetrics(pe, "foo.bar.baz", 1329345819, 2.0);
    storeMetrics(pe, "foo.bar.baz", 1329345880, 0.2);
    pe->onCleanup();
    assert_equal(2, pe->data_gauges_.size());
    it = pe->data_gauges_.begin();
    assert_equal(0.5, (it)->second.getValue());
    assert_equal(0.2, (++it)->second.getValue());
    assert_equal(9, cit->second.getValue());
    assert_equal(1329345855, cit->second.getTimestamp());
    assert_equal(true, cit->second.getCounterStatus());

    // test extract_tags gauge
    pe->data_gauges_.clear();
    pe->dumpMetrics(res, new_metrics);
    std::vector<std::string> ctrs;
    std::string ctr = "x.y.z^host.h123^role.r123^role.r456-1^a-b^a-b.^a-b.ab123.x.y.z^po.ol.a.b.c";
    ctrs.push_back("x.y.z.host.h123.");
    ctrs.push_back("x.y.z.role.r123");
    ctrs.push_back("x.y.z.role.r456-1");
    ctrs.push_back("x.y.z.a-b.ab123.x.y.z");
    ctrs.push_back("x.y.z.po.ol.a.b.c");
    ctrs.push_back("x.y.z.a-b");
    ctrs.push_back("x.y.z.a-b.");
    ctrs.push_back(".x.y.z");
    ctrs.push_back("x.y.");
    ctrs.push_back("x.y.z_host");
    pe->storeMetrics(ctr, "x.y.z", ctrs, 1329345860, 0.2);
    storeMetrics(pe, "*x.y.host.", 1329345875, 1);
    pe->storeMetrics("*x.y.host.", "", std::vector<std::string>(1,"*x.y.host."), 1329345880, 3);
    pe->storeMetrics(ctr, "x.y.z", ctrs, 1329345865, 0.4);
    res.clear();
    new_metrics.clear();
    pe->dumpMetrics(res, new_metrics);
    assert_equal(2, pe->data_counters_.size()); //foo.bar, x.y.host.
    assert_equal(4, res.size());
    assert_equal(6, new_metrics.size());
    assert_equal("# TYPE x.y.host. counter\n", new_metrics[0].typeString());
    assert_equal("x.y.host.{counter=\"1\"} 4 1329345880000\n", new_metrics[0].toString());
    assert_equal("# TYPE foo.bar counter\n", new_metrics[1].typeString());
    assert_equal("foo.bar{counter=\"1\"} 9 1329345880000\n", new_metrics[1].toString());
    assert_equal("# TYPE x.y.z gauge\n", new_metrics[2].typeString());
    compareMetricString(
            "x.y.z{host=\"h123.\",role=\"r123\",po.ol=\"a.b.c\"} 0.2 1329345860000\nx.y.z{role=\"r456-1\"} 0.2 1329345860000\n",
            new_metrics[2].toString());
    assert_equal("# TYPE x.y.z.a-b.ab123.x.y.z gauge\n", new_metrics[3].typeString());
    assert_equal("x.y.z.a-b.ab123.x.y.z 0.2 1329345860000\n", new_metrics[3].toString());
    assert_equal("# TYPE x.y.z.a-b gauge\n", new_metrics[4].typeString());
    assert_equal("x.y.z.a-b 0.2 1329345860000\n", new_metrics[4].toString());
    assert_equal("# TYPE x.y.z.a-b. gauge\n", new_metrics[5].typeString());
    assert_equal("x.y.z.a-b. 0.2 1329345860000\n", new_metrics[5].toString());
    compareMetricString(
            "x.y.z{host=\"h123.\",role=\"r123\",po.ol=\"a.b.c\"} 0.4 1329345865000\nx.y.z{role=\"r456-1\"} 0.4 1329345865000\n",
            res[0].toString());
    assert_equal("x.y.z.a-b.ab123.x.y.z 0.4 1329345865000\n", res[1].toString());
    assert_equal("x.y.z.a-b 0.4 1329345865000\n", res[2].toString());
    assert_equal("x.y.z.a-b. 0.4 1329345865000\n", res[3].toString());

    // test extract_tags counters
    pe->data_gauges_.clear();
    ctrs.clear();
    ctr = "*a.b.c^class.c123^po.ol.p123^abc^class.c456^a.b.c";
    ctrs.push_back("*a.b.c.class.c123");
    ctrs.push_back("*a.b.c.po.ol.p123");
    ctrs.push_back("*a.b.c.abc");
    ctrs.push_back("*a.b.c.class.c456");
    ctrs.push_back("*a.b.c.a.b.c");
    pe->storeMetrics(ctr, "*a.b.c", ctrs, 1329345870, 1);
    pe->storeMetrics(ctr, "*a.b.c", ctrs, 1329345875, 1);
    storeMetrics(pe, "*foo.bar", 1329345875, 1);
    res.clear();
    new_metrics.clear();
    pe->dumpMetrics(res, new_metrics);
    assert_equal(5, pe->data_counters_.size());
    assert_equal(0, res.size());
    assert_equal(5, new_metrics.size());
    std::sort(new_metrics.begin(), new_metrics.end(), comparePromMetric);
    assert_equal("# TYPE a.b.c counter\n", new_metrics[0].typeString());
    compareMetricString(
            "a.b.c{po.ol=\"p123\",class=\"c123\",counter=\"1\"} 2 1329345880000\na.b.c{class=\"c456\",counter=\"1\"} 2 1329345880000\n",
            new_metrics[0].toString());
    assert_equal("# TYPE a.b.c.a.b.c counter\n", new_metrics[1].typeString());
    assert_equal("a.b.c.a.b.c{counter=\"1\"} 2 1329345880000\n", new_metrics[1].toString());
    assert_equal("# TYPE a.b.c.abc counter\n", new_metrics[2].typeString());
    assert_equal("a.b.c.abc{counter=\"1\"} 2 1329345880000\n", new_metrics[2].toString());
    assert_equal("# TYPE foo.bar counter\n", new_metrics[3].typeString());
    assert_equal("foo.bar{counter=\"1\"} 10 1329345880000\n", new_metrics[3].toString());
    assert_equal("# TYPE x.y.host. counter\n", new_metrics[4].typeString());
    assert_equal("x.y.host.{counter=\"1\"} 4 1329345880000\n", new_metrics[4].toString());

    //test remove stale counters
    size_t expected = pe->data_counters_.size();
    storeMetrics(pe, "*a.stale", 1329245850, 1);
    storeMetrics(pe, "*z.stale", 1329259475, 1);
    assert_equal(expected+2, pe->data_counters_.size());
    pe->onRemoveStaleCounter();
    assert_equal(expected, pe->data_counters_.size());
    assert_equal(true, pe->data_counters_.end() == pe->data_counters_.find("a.stale"));
    assert_equal(true, pe->data_counters_.end() == pe->data_counters_.find("z.stale"));

    //test mamimum meric count
    pe->data_gauges_.clear();
    pe->data_counters_.clear();
    pe->max_metric_count_ = 2;
    storeMetrics(pe, "*a.ok.1", 1329345850, 1);
    storeMetrics(pe, "*a.ok.2", 1329345850, 1);
    storeMetrics(pe, "z.ok", 1329345870, 1);
    storeMetrics(pe, "z.dropped", 1329345880, 1);
    storeMetrics(pe, "*y.dropped", 1329345880, 1);
    assert_equal(3, pe->data_counters_.size());
    assert_equal(1, pe->data_gauges_.size());
    std::string dropped_counter("*istatd.agent.dropped.metrics");
    PromExporter::CumulativeCountsMap::iterator ait = pe->data_counters_.find(dropped_counter);
    assert_equal(true, pe->data_counters_.end() != ait);
    assert_equal(2, ait->second.getValue());
    assert_equal(1329345881, ait->second.getTimestamp());
}

void func() {
    test_prom_exporter();
    test_prom_exporter_no_name_mapping();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
