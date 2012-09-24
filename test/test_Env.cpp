
#include <istat/Env.h>
#include <istat/test.h>
#include <stdexcept>


using namespace istat;


class IServiceA {
public:
    virtual int returnOne() = 0;
};

class IServiceB {
public:
    virtual int returnTwo() = 0;
};

class ServiceImplementation : public IServiceA, public IServiceB {
public:
    int returnOne() { return 1; }
    int returnTwo() { return 2; }
};

class MockServiceA : public IServiceA {
public:
    int returnOne() { return 0; }
};

void func()
{
    //  registering services works
    ServiceImplementation impl;
    Env::set<IServiceA>(impl);
    Env::set<IServiceB>(impl);
    assert_equal(Env::get<IServiceA>().returnOne(), 1);
    assert_equal(Env::get<IServiceB>().returnTwo(), 2);

    //  replacing services works
    MockServiceA mocka;
    Env::set<IServiceA>(mocka);
    assert_equal(Env::get<IServiceA>().returnOne(), 0);
    assert_equal(Env::get<IServiceB>().returnTwo(), 2);

    //  asking for non-existing thing fails
    Env::clear();
    bool caught = false;
    try
    {
        Env::get<IServiceA>();
        assert_equal(true, false);
    }
    catch (std::runtime_error const &x)
    {
        caught = true;
    }
    assert_equal(true, caught);
}

int main(int argc, char const *argv[])
{
    return istat::test(func, argc, argv);
}

