
#include <istat/Log.h>
#include <istat/test.h>
#include <vector>
#include <string>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>


using namespace istat;

class LogTest : public LogInstance
{
public:
    LogTest() : rolledCount_(0) {}

    std::vector<std::string> written_;
    int rolledCount_;

    void output(char const *data, size_t size)
    {
        std::string s(data, size);
        written_.push_back(s);
    }
    void rollOver()
    {
        ++rolledCount_;
    }
};

void test_file()
{
    unlink("/tmp/logout.log");
    LogConfig::setOutputFile("/tmp/logout.log");
    LogConfig::outputToFile(LL_Warning, "hello, world!\n", 14);
    LogConfig::rollOver();
    FILE *f = fopen("/tmp/logout.log", "rb");
    assert_true(f != NULL);
    char buf[1024];
    size_t n = fread(buf, 1, 1024, f);
    fclose(f);
    assert_equal(n, 14);
    assert_equal(strncmp(buf, "hello, world!\n", 14), 0);
    unlink("/tmp/logout.log");
}

void test_logging()
{
    LogTest *lt = new LogTest();
    LogConfig::setOutputInstance(boost::shared_ptr<LogInstance>(lt));

    //  hide stderr for this segment
    int old = dup(2);
    close(2);
    open("/dev/null", O_RDWR);
        Log a(LL_Error, "error");
        Log b(LL_Debug, "debug");
        LogConfig::setLogLevel(LL_Error);
        a << "test_a";
        b << "test_b";
        LogConfig::setLogLevel(LL_Debug);
        a << "test_c";
        b << "test_d";
    // unhide stderr
    close(2);
    int d = dup(old);
    assert_equal(d, 2);
    close(old);

    assert_equal(lt->written_.size(), 3);
    assert_contains(lt->written_[0], "test_a");
    assert_contains(lt->written_[1], "test_c");
    assert_contains(lt->written_[2], "test_d");
    assert_contains(lt->written_[0], "error");
    assert_contains(lt->written_[1], "error");
    assert_contains(lt->written_[2], "debug");
    assert_equal(lt->rolledCount_, 0);
    LogConfig::rollOver();
    assert_equal(lt->rolledCount_, 1);
}

void func()
{
    test_file();
    test_logging();
}

int main(int argc, char const *argv[])
{
    return istat::test(func, argc, argv);
}

