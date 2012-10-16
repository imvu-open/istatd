
#include "../daemon/Settings.h"
#include "../daemon/IComplete.h"
#include <boost/asio/io_service.hpp>
#include <istat/test.h>
#include <istat/Log.h>
#include <sys/stat.h>

boost::asio::io_service svc;
class complete : public IComplete
{
public:
    complete() : complete_(false) {}
    bool complete_;
    operator complete *() {
        complete_ = false;
        return this;
    }
    void on_complete() {
        complete_ = true;
    }
};
complete comp;

void pump_and_run()
{
    assert_false(comp.complete_);
    int n = 0;
    while (n < 50 && !comp.complete_) {
        n += 1;
        svc.reset();
        svc.poll_one();
    }
    assert_true(comp.complete_);
}

void test_SettingsFactory(ISettingsFactory *fsf)
{
    //  can I open an existing entry?
    boost::shared_ptr<ISettings> set;
    fsf->open("set", false, set, comp);
    pump_and_run();
    assert_not_equal(set.get(), (ISettings *)0);

    //  Can I get a setting that exists?
    std::string oValue;
    bool wasSet = false;
    set->get("name", oValue, wasSet, "");
    assert_true(wasSet);
    assert_equal(oValue, "value");

    //  Can I get the default for a setting that doesn't exist?
    oValue = "";
    wasSet = false;
    set->get("name3", oValue, wasSet, "dflt");
    assert_false(wasSet);
    assert_equal(oValue, "dflt");

    //  can I iterate keys?
    std::vector<std::string> oKeys;
    set->match("name*", oKeys);
    assert_equal(oKeys.size(), 2);
    //  I know the keys come out sorted
    assert_equal(oKeys[0], "name");
    assert_equal(oKeys[1], "name2");

    //  Do I get denied creating a setting that's not pre-created?
    //  (also, this is a bad filename for the real version)
    fsf->open("none/such", true, set, comp);
    pump_and_run();
    assert_equal(set.get(), (ISettings *)0);

    //  Do I get denied not creating something that's allowed to be created?
    fsf->open("allowCreate", false, set, comp);
    pump_and_run();
    assert_equal(set.get(), (ISettings *)0);

    //  Can I create something I'm allowed to create?
    fsf->open("allowCreate", true, set, comp);
    pump_and_run();
    assert_not_equal(set.get(), (ISettings *)0);

    //  Can I set keys and then find them?
    set->set("kk", "vv");
    set->set("qq", "pewpew");
    oValue = "";
    wasSet = false;
    set->get("kk", oValue, wasSet, "");
    assert_equal(oValue, "vv");
    assert_true(wasSet);
    oValue = "";
    wasSet = false;
    set->get("qq", oValue, wasSet, "");
    assert_equal(oValue, "pewpew");
    assert_true(wasSet);
    oValue = "";
    wasSet = false;
    set->get("zz", oValue, wasSet, "");
    assert_equal(oValue, "");
    assert_false(wasSet);
    oKeys.clear();
    set->match("q*", oKeys);
    assert_equal(oKeys.size(), 1);
    assert_equal(oKeys[0], "qq");

    fsf->flush(comp);
    pump_and_run();
}

void func_fake()
{
    IFakeSettingsFactory *fsf = NewFakeSettingsFactory(svc);
    fsf->allowCreate("allowCreate");
    fsf->preCreate("precreate");
    fsf->set("set", "name", "value");
    fsf->set("set", "name2", "value2");
    test_SettingsFactory(fsf);
    fsf->dispose();
}

void func_real()
{
    istat::DisableStderr dl;
    mkdir("/tmp/test/config", 511);
    int i = open("/tmp/test/config/precreate.set", O_RDWR | O_CREAT, 511);
    assert_not_equal(i, -1);
    close(i);
    i = open("/tmp/test/config/set.set", O_RDWR | O_CREAT, 511);
    char const *str = "# istatd settings 1\nname=value\nname2=value2\n";
    assert_not_equal(i, -1);
    assert_equal(write(i, str, strlen(str)), (ssize_t)strlen(str));
    close(i);
    ISettingsFactory *isf = NewSettingsFactory(svc, "/tmp/test/config");
    test_SettingsFactory(isf);
    isf->dispose();

    //  make sure the file exists
    struct stat stbuf;
    assert_equal(0, ::stat("/tmp/test/config/allowCreate.set", &stbuf));
}

void func()
{
    func_fake();
    func_real();
}

int main(int argc, char const *argv[])
{
    return istat::test(&func, argc, argv);
}

