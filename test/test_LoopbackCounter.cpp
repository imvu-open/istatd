
#include <istat/test.h>
#include "../daemon/FakeStatStore.h"
#include <boost/asio/io_service.hpp>
#include <istat/istattime.h>

using namespace istat;


boost::asio::io_service g_svc;
FakeStatStore fss(g_svc);

void func()
{
    LoopbackCounter gauge("gauge", TypeGauge);
    LoopbackCounter event("event", TypeEvent);
    {
        istat::FakeTime ft(100000);
        gauge.value(1); // won't be counted -- not setup() yet!
        event.value(1);
        LoopbackCounter::setup(&fss, g_svc);
        assert_equal(fss.recorded_.size(), 0);
        LoopbackCounter::forceUpdates();
        assert_equal(fss.recorded_.size(), 1);
        assert_equal(fss.recorded_["*istatd.event"].size(), 1);
        assert_equal(fss.recorded_["*istatd.event"][0].sum(), 1);
    }
    {
        istat::FakeTime ft(100001);
        gauge.value(2);
        event.value(1);
        event.value(1);
        assert_equal(fss.recorded_.size(), 2);
        assert_equal(fss.recorded_["istatd.gauge"].size(), 1);
        assert_equal(fss.recorded_["istatd.gauge"][0].sum(), 2);
        assert_equal(fss.recorded_["*istatd.event"].size(), 1);
        LoopbackCounter::forceUpdates();
        assert_equal(fss.recorded_["*istatd.event"].size(), 2);
        assert_equal(fss.recorded_["*istatd.event"][1].sum(), 2);
        assert_equal(fss.recorded_["*istatd.event"][1].count(), 1);
    }
    {
        istat::FakeTime ft(100002);
        {
            LoopbackCounter x(event);
            ++x;
            x = gauge;
            x.value(3);
            assert_equal(fss.recorded_["istatd.gauge"].size(), 2);
        }
        //  make sure these don't crash
        gauge.value(5);
        ++event;
        LoopbackCounter::forceUpdates();
        assert_equal(fss.recorded_["istatd.gauge"].size(), 3);
        assert_equal(fss.recorded_["*istatd.event"].size(), 3);
    }
}

int main(int argc, char const *argv[])
{
    return istat::test(&func, argc, argv);
}

