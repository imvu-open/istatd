
#include <istat/test.h>
#include "../daemon/Timing.h"

void func()
{
    Timing a;
    int64_t i;
    while ((i = a.elapsedMicros()) <= 0)
        ;
    // resoilution better than a second
    assert_less_than(i, 1000000);
    Timing b;
    double d;
    while ((d = b.elapsedSeconds()) <= 0.0)
        ;
    assert_less_than(d, 1.0);

    double c(b - a);
    assert_greater_than(c, 0.0);
    assert_less_than(c, 1.0);
}

int main(int argc, char const *argv[])
{
    return istat::test(func, argc, argv);
}

