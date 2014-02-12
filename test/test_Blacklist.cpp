#include <string>
#include <istat/test.h>
#include <istat/Log.h>

#include "../daemon/Blacklist.h"

using namespace istat;
void test_blacklist() {
    boost::asio::io_service svc;
    mkdir("/tmp/test/blacklist", 511);
    int i = open("/tmp/test/blacklist/blacklist.set", O_RDWR | O_CREAT, 511);
    assert_not_equal(i, -1);
    close(i);
    i = open("/tmp/test/blacklist/blacklist.set", O_RDWR | O_CREAT, 511);
    char const *str = "hostname1\nhostname2\nhostname3\nHostName4\n";
    assert_not_equal(i, -1);
    assert_equal(write(i, str, strlen(str)), (ssize_t)strlen(str));
    close(i);


    std::string blacklist("/tmp/test/blacklist/blacklist.set");
    Blacklist::Configuration cfg = {};
    cfg.path = blacklist;
    cfg.period = 10;

    std::string blacklisted("hostname2");
    std::string gets_lowered_blacklisted("Hostname2");
    std::string was_lowered_at_load_blacklisted("hostname4");
    std::string good("hostname7");
    Blacklist *bls = new Blacklist(svc, cfg);
    assert_true(bls->check(blacklisted));
    assert_true(bls->check(gets_lowered_blacklisted));
    assert_true(bls->check(was_lowered_at_load_blacklisted));
    assert_false(bls->check(good));
    delete bls;

}

void func() {
    test_blacklist();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
