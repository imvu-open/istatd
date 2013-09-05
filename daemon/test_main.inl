#include <istat/test.h>
#include <istat/Mmap.h>
#include <istat/istattime.h>

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>

#include <json/json.h>

#include <unistd.h>
#include <sys/types.h>

#include "StatStore.h"

boost::asio::io_service svc;

class Stubbed_RequestInFlight : public RequestInFlight
{
public:
    Stubbed_RequestInFlight(boost::shared_ptr<IHttpRequest> &req, boost::asio::io_service &svc) : RequestInFlight(req, svc, "files") { };
    std::string getLastReply() { return strm_.str(); }
protected:
    virtual void complete(int code, char const *type) { }
};

boost::shared_ptr<StatFile> _create_stat_file(istat::StatFile::Settings &init, time_t intervalTime, time_t zeroTime, int64_t numSamples)
{
    init.intervalTime = intervalTime;
    init.zeroTime = zeroTime;
    init.numSamples = numSamples;
    char cd[PATH_MAX];
    char *c = getcwd(cd, PATH_MAX);
    assert_false(c == NULL);
    boost::shared_ptr<StatFile> sfile(new StatFile("testdata.tst", Stats(), init, mm, true));
    return sfile;
}

boost::shared_ptr<StatCounter> _create_stat_counter(boost::shared_ptr<StatFile> sfile)
{
    boost::shared_ptr<StatCounter> statCounter(new StatCounter(sfile, 5, 1024, "5s", false));
    return statCounter;
}

void test_RequestInFlight()
{
    Mmap *mm = NewMmap();

    // test that values with more than 16 significant digits don't have a bare decimal point appended to them

    {
        float hojillion = 1509861124734976.6579516540849084;
        istat::StatFile::Settings init;
        boost::shared_ptr<StatFile> sfile = _create_stat_file(init, 10, 1000000000, 1024);
        
        Bucket b(1, hojillion, 1, 1, 1, 1000000000);
        sfile->updateBucket(b);
        
        boost::shared_ptr<StatCounter> statCounter = _create_stat_counter(sfile);
        FakeTime fakeTime(1000000000);
        HttpRequestHolder htr(boost::shared_ptr<IHttpRequest>((HttpRequest *)0));
        Stubbed_RequestInFlight rif(htr.p_, svc);

        rif.isComplete_ = true;

        rif.generateCounterJson(statCounter, (time_t) 1000000000, (time_t) 1000000000, 1000, 0);
        std::string resp = rif.getLastReply();
        assert_does_not_contain(resp, ".");
    }

    // test that is_counter is set to true iff its corresponding counter has no children (i.e., the counter is accessible
    // via a dotted path x.y.z where x.y.z is valid but x.y.z.? matches nothing)

    {
        Mmap *mm2 = NewMmap(); // we never dispose this because of a race D:
RetentionPolicy rp("10s:10d,5m:140d,1h:5y");
RetentionPolicy xrp("");
        boost::shared_ptr<IStatCounterFactory> statCounterFactory(new StatCounterFactory("testdir", mm2, rp));

        boost::shared_ptr<StatStore> storep(new StatStore("testdir", getuid(), svc, statCounterFactory, mm2));
        storep->record("a.b", 1000, 1, 2, 3, 4, 5);

        std::string jsonresp;
        Json::Value resp;
        Json::Reader reader;
        
        HttpRequestHolder htr(boost::shared_ptr<IHttpRequest>((HttpRequest *)0));

        Stubbed_RequestInFlight rif(htr.p_, svc);
        rif.isComplete_ = true;
        rif.listCountersMatching("a.b", storep);
        jsonresp = rif.getLastReply();
        reader.parse(jsonresp, resp);
        assert_equal(true, resp["matching_names"][0]["is_leaf"].asBool());

        Stubbed_RequestInFlight rif2(htr.p_, svc);
        rif2.isComplete_ = true;
        rif2.listCountersMatching("a", storep);
        jsonresp = rif2.getLastReply();
        reader.parse(jsonresp, resp);
        assert_equal(false, resp["matching_names"][0]["is_leaf"].asBool());
    }

    mm->dispose();
}


void test_Debug()
{
    debug.set("http,record");
    setupDebug();
    assert_true(DebugOption::find("http")->enabled());
    assert_true(DebugOption::find("record")->enabled());
    assert_false(DebugOption::find("admin")->enabled());
    assert_equal(DebugOption::find("derp"), (DebugOption const *)NULL);
}


void test_main()
{
    test_Debug();
    test_RequestInFlight();
}
