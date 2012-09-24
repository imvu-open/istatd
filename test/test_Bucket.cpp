
#include "istat/Bucket.h"
#include "istat/test.h"

using namespace istat;

void func()
{
    Bucket a(true);
    assert_equal(a.sum(), 0.0);
    assert_equal(a.sumSq(), 0.0);
    assert_equal(a.min(), 0.0);
    assert_equal(a.max(), 0.0);
    assert_equal(a.count(), 0);
    assert_equal(a.time(), 0);

    Bucket b(2, 4, 2, 2, 1, 100000);
    assert_equal(b.sum(), 2.0);
    assert_equal(b.sumSq(), 4.0);
    assert_equal(b.min(), 2.0);
    assert_equal(b.max(), 2.0);
    assert_equal(b.count(), 1);
    assert_equal(b.time(), 100000);

    a.update(b);
    a.update(b);
    assert_equal(a.sum(), 4);
    assert_equal(a.sumSq(), 8);
    assert_equal(a.min(), 2);
    assert_equal(a.max(), 2);
    assert_equal(a.count(), 2);
    assert_equal(a.time(), 100000);
    
    Bucket c(98.6215057F, 4863.10059F, 49.2929001F, 49.3286018F, 2, 0);
    assert_equal(0, c.sdev());
}


int main(int argc, char const *argv[])
{
    return istat::test(func, argc, argv);
}

