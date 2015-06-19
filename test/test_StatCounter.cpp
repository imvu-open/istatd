#include <iostream>

#include <istat/test.h>
#include <istat/istattime.h>
#include <istat/Mmap.h>
#include <istat/Log.h>
#include "../daemon/StatCounter.h"
#include "../daemon/Retention.h"
#include "../daemon/FakeStatStore.h"

#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>

using namespace istat;

Mmap *mm(NewMmap());

RetentionPolicy rp("10s:1h,5m:1d");
RetentionPolicy xrp("");
RetentionPolicy trp("10s:1h,1m:1h:ma-1h:0.875");

void run_tests(void)
{
    boost::asio::io_service svc;
    FakeStatStore *fss = new FakeStatStore(svc);
    boost::shared_ptr<IStatStore> ssp(fss);

    // test counter created as gauge stays a gauge even if reopened as a collated counter
    {
        test_init_path("/tmp/test");
        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, istattime(0), mm, rp);
        assert_true(gauge.get()!=0);
        assert_false(gauge->isCollated());
        gauge->flush(ssp);

        boost::shared_ptr<IStatCounter> counter = boost::make_shared<StatCounter>("/tmp/test/taco", true, istattime(0), mm, rp);
        assert_true(counter.get()!=0);
        assert_false(counter->isCollated());
    }
    // test counter created as collated counter stays a collated even if reopened as a gauge
    {
        test_init_path("/tmp/test");

        boost::shared_ptr<IStatCounter> counter = boost::make_shared<StatCounter>("/tmp/test/taco", true, istattime(0), mm, rp);
        assert_true(counter.get()!=0);
        assert_true(counter->isCollated());
        counter->flush(ssp);

        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, istattime(0), mm, rp);
        assert_true(gauge.get()!=0);
        assert_true(gauge->isCollated());
    }
    // test counter created as collated counter stays a collated when reopened as a collated counted
    {
        test_init_path("/tmp/test");

        boost::shared_ptr<IStatCounter> counter = boost::make_shared<StatCounter>("/tmp/test/taco", true, istattime(0), mm, rp);
        assert_true(counter.get()!=0);
        assert_true(counter->isCollated());
        counter->flush(ssp);

        boost::shared_ptr<IStatCounter> counter2 = boost::make_shared<StatCounter>("/tmp/test/taco", true, istattime(0), mm, rp);
        assert_true(counter2.get()!=0);
        assert_true(counter2->isCollated());
    }

    // test counter created as gauge stays a gauge when reopened as a gauge
    {
        test_init_path("/tmp/test");

        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, istattime(0), mm, rp);
        assert_true(gauge.get()!=0);
        assert_false(gauge->isCollated());
        gauge->flush(ssp);

        boost::shared_ptr<IStatCounter> gauge2 = boost::make_shared<StatCounter>("/tmp/test/taco", false, istattime(0), mm, rp);
        assert_true(gauge2.get()!=0);
        assert_false(gauge2->isCollated());
    }

    // test pickStatFile
    {
        // with RetentionPolicy rp("10s:1h,5m:1d"); -- see above
        //   assuming 256 buckets per 8k block
        //   number of 10s buckets: 512 or   5120 seconds or        85m 20s
        //   number of  5m buckets: 512 or 153600 seconds or 1d 18h 40m

        time_t TIME_ZERO        = (time_t)202020;
        time_t TIME_2_HOURS     = TIME_ZERO + (time_t)(2*3600);
        time_t TIME_NOW         = TIME_ZERO + (time_t)(3*3600);

        time_t DELTA_85_MINUTES = 85*60;
        time_t DELTA_153600_SECONDS = 153600;

        test_init_path("/tmp/test");

        FakeTime faketime(TIME_NOW);
        boost::shared_ptr<StatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, TIME_ZERO, mm, rp);

        // create 2 hours of data. note that 10s counter will wrap since
        // it can only hold 85m and 20s.
        for (time_t t = TIME_ZERO; t <= TIME_2_HOURS; t += (time_t)10) {
            gauge->record(t, 1.0, 1.0, 1.0, 1.0, 1);
        }
        gauge->flush(ssp);

        boost::shared_ptr<istat::StatFile> sf;

        time_t interval;

        // pickStatFile where both start and end time in the 10s file
        sf = gauge->pickStatFile(TIME_2_HOURS-DELTA_85_MINUTES, TIME_2_HOURS-10, interval);
        assert_equal(10, sf->settings().intervalTime);
        assert_equal(10, interval);

        // pickStatFile where start before 10s file and end time in the 10s file
        sf = gauge->pickStatFile(TIME_ZERO, TIME_2_HOURS-10, interval);
        assert_equal(300, sf->settings().intervalTime);
        assert_equal(300, interval);

        // pickStatFile where start in 10s file end end time is beyond 10s file
        sf = gauge->pickStatFile(TIME_2_HOURS-10, TIME_NOW, interval);
        assert_equal(10, sf->settings().intervalTime);
        assert_equal(10, interval);

        // pickStatFile where start and end are beyond youngest data in 10s and 5m file
        sf = gauge->pickStatFile(TIME_2_HOURS+310, TIME_NOW, interval);
        assert_equal(boost::shared_ptr<istat::StatFile>((StatFile*)0), sf);
        assert_equal(10, interval);

        // pickStatFile where start and end are in the future
        sf = gauge->pickStatFile(TIME_NOW+10, TIME_NOW+30, interval);
        assert_equal(boost::shared_ptr<istat::StatFile>((StatFile*)0), sf);
        assert_equal(10, interval);

        // pickStatFile where start and end is before 10s oldest possible 10s data,
        sf = gauge->pickStatFile(TIME_ZERO, TIME_ZERO+60, interval);
        assert_equal(300, sf->settings().intervalTime);
        assert_equal(300, interval);

        // pickStatFile where start is before 10s oldest possible 10s data, and end
        // is past the youngest data in 10s and 5m data
        sf = gauge->pickStatFile(TIME_ZERO, TIME_NOW, interval);
        assert_equal(300, sf->settings().intervalTime);
        assert_equal(300, interval);

        // pickStatFile where start and end is before oldest possible 10s and 5m data
        sf = gauge->pickStatFile(TIME_2_HOURS - DELTA_153600_SECONDS - 600, TIME_2_HOURS - DELTA_153600_SECONDS - 310, interval);
        assert_equal(boost::shared_ptr<istat::StatFile>((StatFile*)0), sf);
        assert_equal(300, interval);

    }

    // test normalizeRange
    {
        time_t TIME_ZERO = (time_t)202020;

        test_init_path("/tmp/test");

        FakeTime faketime(TIME_ZERO);
        boost::shared_ptr<StatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, TIME_ZERO, mm, rp);

        time_t start, end, interval;

        // test that crash is gone
        start = 1000;
        end   = 2000;
        interval = 0;
        gauge->normalizeRange(start, end, interval, 600);
        assert_equal(1000, start);
        assert_equal(2000, end);
        assert_equal(  10, interval);

        // test that same values returned when maxSamples is 0
        start = 1000;
        end   = 2000;
        interval = 10;
        gauge->normalizeRange(start, end, interval, 0);
        assert_equal(1000, start);
        assert_equal(2000, end);
        assert_equal(  10, interval);

        // test that same values returned when maxSamples > samples in range
        start = 1000;
        end   = 2000;
        interval = 10;
        gauge->normalizeRange(start, end, interval, 200);
        assert_equal(1000, start);
        assert_equal(2000, end);
        assert_equal(  10, interval);

        // test that start and stop times example outward from center when maxSamples is 0
        start = 1005;
        end   = 1995;
        interval = 10;
        gauge->normalizeRange(start, end, interval, 0);
        assert_equal(1000, start);
        assert_equal(2000, end);
        assert_equal(  10, interval);

        // test that interval becomes 20
        start = 1000;
        end   = 2000;
        interval = 10;
        gauge->normalizeRange(start, end, interval, 50);
        assert_equal(1000, start);
        assert_equal(2000, end);
        assert_equal(  20, interval);

        // test that start and stop expand outward to new interval boundry
        start = 1010;
        end   = 1990;
        interval = 10;
        gauge->normalizeRange(start, end, interval, 50);
        assert_equal(1000, start);
        assert_equal(2000, end);
        assert_equal(  20, interval);

        // test interval comes back on weird max sample size
        start = 1000;
        end   = 2000;
        interval = 10;
        gauge->normalizeRange(start, end, interval, 97);
        assert_equal(1000, start);
        assert_equal(2000, end);
        assert_equal(  20, interval);

        // test that 5m interval data can reduce to 1h interval data
        start = 3600;
        end   = 5*3600;
        interval = 300;
        gauge->normalizeRange(start, end, interval, 4);
        assert_equal(3600, start);
        assert_equal(5*3600, end);
        assert_equal(3600, interval);
    }

    // test updates from too far in the future are ignored
    {
        time_t TIME_ZERO = (time_t)202020;
        time_t TIME_60_SECONDS = TIME_ZERO + (time_t)60;
        time_t TIME_61_SECONDS  = TIME_ZERO + (time_t)61;
        time_t TIME_120_SECONDS = TIME_ZERO + (time_t)120;

        test_init_path("/tmp/test");
        FakeTime faketime(TIME_ZERO);

        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, TIME_ZERO, mm, rp);

        gauge->record(TIME_ZERO, 1.0, 1.0, 1.0, 1.0, 1);
        gauge->record(TIME_60_SECONDS, 4.0, 4.0, 2.0, 2.0, 2);
        gauge->record(TIME_61_SECONDS, 9.0, 9.0, 2.0, 2.0, 3);
        gauge->flush(ssp);

        faketime.set(TIME_120_SECONDS);
        std::vector<istat::Bucket> buckets;
        time_t normalized_start;
        time_t normalized_end;
        time_t interval;

        gauge->select(TIME_ZERO, TIME_120_SECONDS, false, buckets, normalized_start, normalized_end, interval, 0);

        assert_equal(TIME_ZERO, normalized_start);
        assert_equal(TIME_120_SECONDS, normalized_end);
        assert_equal(10, interval);
        assert_equal(buckets.size(),2);
        assert_equal(buckets[0].count(), 1);
        assert_equal(buckets[1].count(), 2);
    }

    // Test that an empty statfile returns no data.
    {
        test_init_path("/tmp/test");

        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, 10, mm, rp);

        std::vector<istat::Bucket> buckets;
        time_t normalized_start, normalized_end, interval;
        gauge->select(0, 0, false, buckets, normalized_start, normalized_end, interval, 0);

        assert_equal(0, buckets.size());
    }

    // Test a basic gauge.
    {
        FakeTime faketime(1000000020);
        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, 10, mm, rp);

        gauge->record(1000000000, 1, 1, 1, 1, 1);
        gauge->record(1000000010, 1, 1, 1, 1, 1);
        gauge->record(1000000020, 1, 1, 1, 1, 1);
        gauge->flush(ssp);

        std::vector<istat::Bucket> buckets;
        time_t normalized_start, normalized_end, interval;
        gauge->select(1000000010, 1000000022, false, buckets, normalized_start, normalized_end, interval, 0);
        assert_equal(2, buckets.size());
        assert_equal(1000000010, buckets[0].time());
        assert_equal(1000000020, buckets[1].time());
        assert_equal(10, interval);

        gauge->select(1000000010, 1000000010, false, buckets, normalized_start, normalized_end, interval, 0);
        assert_equal(1, buckets.size());
        assert_equal(1000000010, buckets[0].time());
        assert_equal(10, interval);
    }

    // Test min / avg / max rounding (positive values).
    {
        FakeTime faketime(1000000020);
        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, 10, mm, rp);
        IStatCounter::recordsRejected_ = LoopbackCounter("test", TypeEvent);

        gauge->record((time_t) 1000000020,
                      boost::lexical_cast<float>("9.38006e+10"), // value
                      boost::lexical_cast<float>("8.79856e+21"), // valsq
                      boost::lexical_cast<float>("9.38006e+10"), // min
                      boost::lexical_cast<float>("9.38006e+10"), // max
                      (size_t) 1);                               // count
        gauge->flush(ssp);

        assert_equal(IStatCounter::recordsRejected_.getAggregate(), 0);
    }

    // Test min / avg / max rounding (negative values).
    {
        FakeTime faketime(1000000020);
        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, 10, mm, rp);
        IStatCounter::recordsRejected_ = LoopbackCounter("test", TypeEvent);

        gauge->record((time_t) 1000000020,
                      boost::lexical_cast<double>("-9.38006e+10"),
                      boost::lexical_cast<double>("-8.79856e+21"),
                      boost::lexical_cast<double>("-9.38006e+10"),
                      boost::lexical_cast<double>("-9.38006e+10"),
                      (size_t) 1);
        gauge->flush(ssp);

        assert_equal(IStatCounter::recordsRejected_.getAggregate(), 0);
    }

    // test request for unaligned times should round inwards
    {
        test_init_path("/tmp/test");

        FakeTime faketime(1000000030);
        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, 10, mm, rp);

        gauge->record(1000000000, 1, 1, 1, 1, 1);
        gauge->record(1000000010, 2, 4, 2, 2, 1);
        gauge->record(1000000020, 3, 5, 1, 2, 2);
        gauge->record(1000000030, 3, 5, 1, 2, 2);
        gauge->flush(ssp);

        time_t normalized_start, normalized_end, interval;

        std::vector<istat::Bucket> buckets;

        gauge->select(1000000001, 1000000011, false, buckets, normalized_start, normalized_end, interval, 0);

        assert_equal(2, buckets.size());
        assert_equal(1000000000, buckets[0].time());
        assert_equal(1000000010, buckets[1].time());
    }

    // test that a wrapped counter works
    {
        test_init_path("/tmp/test");

        FakeTime faketime(3610);
        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, 10, mm, rp);

        gauge->record(10, 1, 1, 1, 1, 1);
        gauge->record(3590, 2, 4, 2, 2, 1);
        gauge->record(3600, 3, 5, 1, 2, 2);
        gauge->record(3610, 4, 5, 1, 2, 2);
        gauge->flush(ssp);

        std::vector<istat::Bucket> buckets;
        time_t normalized_start, normalized_end, interval;

        gauge->select(3590, 3620, false, buckets, normalized_start, normalized_end, interval, 0);
        assert_equal(10, interval);
        assert_equal(3, buckets.size());
        assert_equal(3590, buckets[0].time());
        assert_equal(3600, buckets[1].time());
        assert_equal(3610, buckets[2].time());
    }

    // Test that the last 15 minutes are returned if start and end are both 0.
    {
        test_init_path("/tmp/test");

        const int FAKE_TIME = 5010;
        const int FIFTEEN_MINUTES = 900;
        const int ORIGIN = 20;
        const int INTERVAL = 10;
        const size_t RECORDED_BUCKETS = 500;
        const int EXPECTED_START_TIME = FAKE_TIME - FIFTEEN_MINUTES;
        const int EXPECTED_END_TIME = FAKE_TIME - INTERVAL;
        const size_t EXPECTED_BUCKETS = FIFTEEN_MINUTES / INTERVAL;

        FakeTime fake(FAKE_TIME);
        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, ORIGIN, mm, rp);

        for(size_t i = 0; i != RECORDED_BUCKETS; ++i)
        {
            gauge->record(i * INTERVAL + ORIGIN, i, i * i, i, i, 1);
        }
        gauge->flush(ssp);

        std::vector<istat::Bucket> buckets;
        time_t normalized_start, normalized_end, interval;

        gauge->select(0, 0, false, buckets, normalized_start, normalized_end, interval, 0);

        assert_equal(normalized_start, FAKE_TIME - FIFTEEN_MINUTES);
        assert_equal(normalized_end, FAKE_TIME);
        assert_equal(INTERVAL, interval);
        assert_equal(EXPECTED_BUCKETS, buckets.size());
        assert_true(EXPECTED_BUCKETS < RECORDED_BUCKETS);

        // Validate a few times of returned buckets.
        assert_equal(EXPECTED_START_TIME,            buckets[0].time());
        assert_equal(EXPECTED_START_TIME + INTERVAL, buckets[1].time());
        assert_equal(EXPECTED_END_TIME - INTERVAL,   buckets[buckets.size()-2].time());
        assert_equal(EXPECTED_END_TIME,              buckets[buckets.size()-1].time());
    }

    // Test that the last 15 minutes from end are returned, if start is zero, and end is nonzero.
    {
        test_init_path("/tmp/test");

        const int FAKE_TIME = 5010;
        const int ORIGIN = 10;
        const int INTERVAL = 10;
        const int TIME_FROM_LAST = 900;
        const size_t RECORDED_BUCKETS = 500;
        const int END_TIME = 1000;
        const size_t EXPECTED_BUCKETS = TIME_FROM_LAST / INTERVAL;

        FakeTime fake(FAKE_TIME);
        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, ORIGIN, mm, rp);

        for(size_t i = 0; i != RECORDED_BUCKETS; ++i)
        {
            gauge->record(i * INTERVAL + ORIGIN, i, i * i, i, i, 1);
        }
        gauge->flush(ssp);

        // Ensure our end time is *not* the current timestamp.
        assert_not_equal(istattime(0), END_TIME);

        std::vector<istat::Bucket> buckets;
        time_t normalized_start, normalized_end, interval;

        gauge->select(0, END_TIME, false, buckets, normalized_start, normalized_end, interval, 0);

        assert_equal(EXPECTED_BUCKETS, buckets.size());
        assert_true(EXPECTED_BUCKETS < RECORDED_BUCKETS);
        assert_equal(INTERVAL, interval);

        // Validate a few times of returned buckets.
        assert_equal(END_TIME - TIME_FROM_LAST, buckets[0].time());
        assert_equal(END_TIME - TIME_FROM_LAST + INTERVAL * 2, buckets[2].time());
        assert_equal(END_TIME - INTERVAL * 3, buckets[buckets.size() - 3].time());
        assert_equal(END_TIME - INTERVAL,     buckets[buckets.size() - 1].time());
    }

    // Test that buckets with times in the range start .. istattime(0)
    // are returned if start is nonzero, and end is zero.
    {
        test_init_path("/tmp/test");

        const int FAKE_TIME = 5010;
        const int FIFTEEN_MINUTES = 900;
        const int ORIGIN = 10;
        const int INTERVAL = 10;
        const size_t RECORDED_BUCKETS = 500;
        const size_t EXPECTED_BUCKETS = FIFTEEN_MINUTES / INTERVAL;

        FakeTime fake(FAKE_TIME);
        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, ORIGIN, mm, rp);

        for(size_t i = 0; i != RECORDED_BUCKETS; ++i)
        {
            gauge->record(i * INTERVAL + ORIGIN, i, i * i, i, i, 1);
        }
        gauge->flush(ssp);

        std::vector<istat::Bucket> buckets;
        time_t normalized_start, normalized_end, interval;

        gauge->select(ORIGIN, 0, false, buckets, normalized_start, normalized_end, interval, 0);

        assert_equal(EXPECTED_BUCKETS, buckets.size());
        assert_equal(INTERVAL, interval);

        // Validate a few times of returned buckets.
        assert_equal(ORIGIN, buckets[0].time());
        assert_equal(ORIGIN + INTERVAL * 2, buckets[2].time());
        assert_equal(ORIGIN + FIFTEEN_MINUTES - INTERVAL * 2, buckets[buckets.size() - 2].time());
        assert_equal(ORIGIN + FIFTEEN_MINUTES - INTERVAL,     buckets[buckets.size() - 1].time());
    }

    // test max_count == 1 select() synthetic bucket reduction
    {
        test_init_path("/tmp/test");

        FakeTime faketime(3610);
        boost::shared_ptr<IStatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco2", false, 10, mm, rp);

        gauge->record(10, 1, 1, 1, 1, 1);
        gauge->record(3590, 2, 4, 2, 2, 1);
        gauge->record(3600, 3, 5, 1, 2, 2);
        gauge->record(3610, 4, 5, 1, 2, 2);
        gauge->flush(ssp);

        std::vector<istat::Bucket> buckets;
        time_t normalized_start, normalized_end, interval;

        gauge->select(3580, 3610, false, buckets, normalized_start, normalized_end, interval, 1);
        //  interval will be 30 seconds (difference between counters)
        //  because the round-down of 3590 is 3570, and the round-up of 3620 is 3630, 
        //  the interval will be extended to 60 seconds to cover the entire range.
        assert_equal(60, interval);
        assert_equal(1, buckets.size());
        //  Note that this is not divisible by 60, but only by 30 (the original interval)
        assert_equal(3570, buckets[0].time());
        assert_equal(9, buckets[0].sum());
    }

    // test stat file picker
    {
        RetentionPolicy rp("10s:10d,5m:140d,1h:5y");
        RetentionPolicy xrp("");
        boost::shared_ptr<StatCounter> statCounter = boost::make_shared<StatCounter>("fake/counter", false, 1000000000, mm, rp);
        statCounter->record(1000000000, 1, 1, 1, 1, 1);

        FakeTime fake(1000000000);


        // last 100 seconds
        time_t interval;
        boost::shared_ptr<istat::StatFile> sf = statCounter->pickStatFile(999999900,1000000000, interval);
        assert_equal(10, sf->settings().intervalTime);
        assert_equal(10, interval);

        // last 4 weeks
        sf = statCounter->pickStatFile(1000000000-2419200,1000000000, interval);
        assert_equal(300, sf->settings().intervalTime);
        assert_equal(300, interval);

        // last 13 months
        // 3600*24*30*13 = 33696000
        sf = statCounter->pickStatFile(1000000000-33696000,1000000000, interval);
        assert_equal(3600, sf->settings().intervalTime);
        assert_equal(3600, interval);

        sf = statCounter->pickStatFile(1000000000+33696000,1000000000+(336960000*2), interval);
        assert_equal(10, interval);
        assert_equal(0, sf);
    }



    // Test that collation flushing works, at each interval size, going up.
    {
        // Aligned to be divisible by 300s (5m).
        time_t TIME_ZERO = 201900;
        time_t TIME_LAST_VALUE = TIME_ZERO + 5 * 60;
        time_t TIME_2_HOURS_AGO = TIME_ZERO - 2 * 3600;

        test_init_path("/tmp/test");
        FakeTime faketime(TIME_ZERO);

        boost::shared_ptr<StatCounter> counter = boost::make_shared<StatCounter>("/tmp/test/quesadilla", true, TIME_ZERO, mm, rp);

        // Let's create intentional gaps of 20s, to see if bigger intervals handle this correctly.
        for(time_t t = TIME_ZERO; t <= TIME_LAST_VALUE; t += 20)
        {
            faketime.set(t);
            counter->record(t, 1.0, 1.0, 1.0, 1.0, 1);
        }
        counter->maybeShiftCollated(TIME_LAST_VALUE + 60);

        boost::shared_ptr<istat::StatFile> sf;
        time_t interval;
        std::vector<Bucket> buckets;
        int64_t startIndex;
        int64_t bucketCount;
        int64_t recordedBucketCount;

        // Check the 10s resolution statfile first to see if it recorded properly.
        sf = counter->pickStatFile(TIME_ZERO, TIME_LAST_VALUE, interval);
        assert_not_equal(sf, 0);
        assert_equal(interval, 10);

        startIndex = sf->mapTimeToBucketIndex(TIME_ZERO, false);
        bucketCount = sf->numBucketsBetween(TIME_ZERO, TIME_LAST_VALUE, false);
        buckets.resize(bucketCount);
        bucketCount = sf->readBuckets(&buckets[0], bucketCount, startIndex);
        buckets.resize(bucketCount);

        recordedBucketCount = 0;
        for(std::vector<Bucket>::iterator b = buckets.begin(), end = buckets.end(); b != end; ++b)
        {
            if(b->time() >= TIME_ZERO && b->time() < TIME_LAST_VALUE && b->count() > 0)
            {
                ++recordedBucketCount;
            }
        }
        assert_equal(recordedBucketCount, 15);
        assert_within(buckets[0].avg(), 1.0/10.0, 1e-8);

        // Now, let's check the 5m resolution, to see if it was recorded correctly.
        sf = counter->pickStatFile(TIME_2_HOURS_AGO, TIME_LAST_VALUE, interval);
        assert_not_equal(sf, 0);
        assert_equal(interval, 300);

        startIndex = sf->mapTimeToBucketIndex(TIME_2_HOURS_AGO, false);
        bucketCount = sf->numBucketsBetween(TIME_2_HOURS_AGO, TIME_LAST_VALUE, false);
        buckets.resize(bucketCount);
        bucketCount = sf->readBuckets(&buckets[0], bucketCount, startIndex);
        buckets.resize(bucketCount);

        recordedBucketCount = 0;
        for(std::vector<Bucket>::iterator b = buckets.begin(), end = buckets.end(); b != end; ++b)
        {
            if(b->time() >= TIME_2_HOURS_AGO && b->time() < TIME_LAST_VALUE && b->count() > 0)
            {
                if(recordedBucketCount == 0)
                {
                    // Desired behaviour.
                    assert_equal(b->count(), 30);
                    assert_within(b->avg(), 15.0/300.0, 1e-8);
                }
                ++recordedBucketCount;
            }
        }
        assert_equal(recordedBucketCount, 1);
    }

    // Test that a record call with known invalid data gets rejected.
    {
        test_init_path("/tmp/test");

        time_t TIME_NOW = 123456;
        FakeTime faketime(TIME_NOW);
        boost::shared_ptr<StatCounter> gauge = boost::make_shared<StatCounter>("/tmp/test/taco", false, TIME_NOW, mm, rp);

        IStatCounter::recordsRejected_ = LoopbackCounter("test", TypeEvent);

        // Accept: Valid data.
        gauge->record(TIME_NOW, 1, 1, 1, 2, 1);
        assert_equal(IStatCounter::recordsRejected_.getAggregate(), 0);
        // Accept: negative data.
        gauge->record(TIME_NOW, -1, 1, -1, -1, 1);

        // Reject: Invalid count.
        gauge->record(TIME_NOW, 1, 1, 1, 2, 0);
        // Reject: min > max.
        gauge->record(TIME_NOW, 1, 1, 2, 1, 1);
        // Reject: value < min.
        gauge->record(TIME_NOW, 0, 0, 1, 2, 1);
        // Reject: value > max.
        gauge->record(TIME_NOW, 3, 3, 1, 2, 1);
        assert_equal(IStatCounter::recordsRejected_.getAggregate(), 4);
    }

    // Test that a collated counter reported at a timestamp that isn't a multiple
    // of the collation interval will be put in a bucket that is.
    {
        test_init_path("/tmp/test");

        // Not a multiple of 10s (our collation interval). Uh oh.
        time_t TIME_WRITTEN = 123456;
        time_t TIME_ROUNDED = 123450;
        time_t TIME_NOW = TIME_WRITTEN + 120;
        FakeTime faketime(TIME_NOW);
        boost::shared_ptr<StatCounter> counter = boost::make_shared<StatCounter>("/tmp/test/quesadilla", true, TIME_WRITTEN, mm, rp);
        counter->record(TIME_WRITTEN, 1.0, 1.0, 1.0, 1.0, 1);
        counter->maybeShiftCollated(TIME_NOW);

        boost::shared_ptr<istat::StatFile> sf;
        time_t interval;
        std::vector<Bucket> buckets;
        int64_t startIndex;
        int64_t bucketCount;

        sf = counter->pickStatFile(TIME_ROUNDED, TIME_NOW, interval);
        assert_not_equal(sf, 0);
        assert_equal(interval, 10);

        startIndex = sf->mapTimeToBucketIndex(TIME_ROUNDED, false);
        bucketCount = sf->numBucketsBetween(TIME_ROUNDED, TIME_NOW, false);
        buckets.resize(bucketCount);
        bucketCount = sf->readBuckets(&buckets[0], bucketCount, startIndex);
        buckets.resize(bucketCount);
        assert_not_equal(buckets.size(), 0);

        // Should be rounded to nearest multiple of collation time.
        assert_equal(buckets[0].time(), TIME_ROUNDED);
    }

    // Test that periodic flushing of collated counters will continue to permit
    // values from 10s before the current time.
    {
        test_init_path("/tmp/test");

        time_t TIME_RECORD1 = 120;
        time_t TIME_RECORD2 = 240;
        time_t TIME_NOW = 255;

        IStatCounter::recordsFromThePast_ = LoopbackCounter("past", TypeEvent);

        FakeTime faketime(TIME_NOW);
        boost::shared_ptr<StatCounter> counter = boost::make_shared<StatCounter>("/tmp/test/quesadilla", true, 10, mm, rp);
        counter->record(TIME_RECORD1, 1.0, 1.0, 1.0, 1.0, 1);
        counter->flush(ssp);
        counter->record(TIME_RECORD2, 1.0, 1.0, 1.0, 1.0, 1);

        assert_equal(IStatCounter::recordsFromThePast_.getAggregate(), 0);
    }

    //  test trailing files
    {
        test_init_path("/tmp/test");

        time_t TIME_RECORD1 = 10000;
        time_t TIME_RECORD2 = 13600;
        time_t TIME_RECORD3 = 13660;
        time_t TIME_NOW = 13720;

        FakeTime faketime(TIME_NOW);
        boost::shared_ptr<StatCounter> counter = boost::make_shared<StatCounter>("/tmp/test/trailertrash", false, TIME_RECORD1, mm, trp);
        counter->record(TIME_RECORD1, 1.0, 1.0, 1.0, 1.0, 1);
        counter->flush(ssp);
        counter->record(TIME_RECORD2, 2.0, 4.0, 2.0, 2.0, 1);
        counter->flush(ssp);
        counter->record(TIME_RECORD3, 2.0, 4.0, 2.0, 2.0, 1);
        counter->flush(ssp);
        counter->record(TIME_NOW, 0.0, 0.0, 0.0, 0.0, 0);
        counter->flush(ssp);

        std::vector<istat::Bucket> oBucks;
        time_t nstart(0), nend(0), ival(0);
        counter->select(TIME_NOW-3600, TIME_NOW, true, oBucks, nstart, nend, ival, 100);
        assert_equal(oBucks.size(), 1);
        assert_equal(oBucks[0].sum(), (1.0 * 0.875 + 2.0 * (1 - 0.875)));
    }
}

int main(int argc, char const *argv[])
{
    return istat::test(run_tests, argc, argv);
}
