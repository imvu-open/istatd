#include <istat/test.h>
#include <istat/Mmap.h>
#include <istat/istattime.h>
#include "../daemon/StatCounter.h"
#include "../daemon/StatCounterFactory.h"
#include <boost/filesystem.hpp>

using namespace istat;

Mmap *mm(NewMmap());

RetentionPolicy rp("10s:1h,5m:1d");
RetentionPolicy xrp("");

void run_tests(void)
{
    // test creating a counter works
    {
        assert_true(true);
        boost::filesystem::remove_all("/tmp/test");
        StatCounterFactory factory("/tmp/test", mm, rp);
        boost::shared_ptr<IStatCounter> counter = factory.create("taco", false, istattime(0));
        assert_true(counter.get()!=0);
        assert_false(counter->isCollated());

    }
    {
        boost::filesystem::remove_all("/tmp/test");
        StatCounterFactory factory("/tmp/test", mm, rp);
        boost::shared_ptr<IStatCounter> counter = factory.create("taco", true, istattime(0));
        boost::shared_ptr<IStatCounter> reopen = factory.create("taco", true, istattime(0));
        assert_true(reopen.get()!=0);
    }
    {
        boost::filesystem::remove_all("/tmp/test");
        StatCounterFactory factory("/tmp/test", mm, rp);
        boost::shared_ptr<IStatCounter> one = factory.create("bbq", true, istattime(0));
        assert_true(one.get()!=0);
        boost::shared_ptr<IStatCounter> two = factory.create("bbq", true, istattime(0));
        assert_true(two.get()!=0);
        assert_equal(((StatCounter*)one.get())->counters_[0].file->settings().numSamples, ((StatCounter*)two.get())->counters_[0].file->settings().numSamples);
    }
    {
        boost::filesystem::remove_all("/tmp/test");
        StatCounterFactory factory("/tmp/test", mm, rp);
        boost::shared_ptr<IStatCounter> one = factory.create("sandwich", true, istattime(0));
        assert_true(one.get()!=0);
        assert_true(one->isCollated());
    }
}

int main(int argc, char const *argv[])
{
    return istat::test(run_tests, argc, argv);
}
