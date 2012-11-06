
#include "../daemon/Debug.h"
#include <istat/test.h>


void func()
{
    {
        DebugOption a("a");
        DebugOption b("b");
        a.set(true);
        assert_true(a.enabled());
        assert_false(b.enabled());
        DebugOption const *opt = DebugOption::first();
        assert_not_equal(opt, (DebugOption const *)NULL);
        assert_true(opt == &a || opt == &b);
        opt = DebugOption::next(opt);
        assert_not_equal(opt, (DebugOption const *)NULL);
        assert_true(opt == &a || opt == &b);
        opt = DebugOption::next(opt);
        assert_equal(opt, (DebugOption const *)NULL);
    }
    assert_equal(DebugOption::first(), (DebugOption const *)NULL);
}

int main(int argc, char const *argv[])
{
    return istat::test(func, argc, argv);
}
