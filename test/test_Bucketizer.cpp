#include <istat/test.h>
#include <istat/Log.h>
#include <istat/istattime.h>

#include "../daemon/Bucketizer.h"

using namespace istat;

void test_buckets_distributed_into_bucketizer() {
    time_t now = (time_t)1000;
    Bucketizer bizer(now);

    assert_true(bizer.get(0).time() == (time_t)0);
    assert_true(bizer.get(1).time() == (time_t)0);
    assert_true(bizer.get(2).time() == (time_t)0);
    assert_true(bizer.get(3).time() == (time_t)0);
    assert_true(bizer.get(4).time() == (time_t)0);

    Bucket a(1.0, 1.0, 1.0, 1.0, 1, now);
    bizer.update(a);

    assert_true(bizer.get(0).time() == (time_t)0);
    assert_true(bizer.get(1).time() == (time_t)0);
    assert_true(bizer.get(2).time() == (time_t)0);
    assert_true(bizer.get(3).time() == (time_t)1000);
    assert_true(bizer.get(4).time() == (time_t)0);

    now = (time_t)1010;
    bizer.update(Bucket(1.0, 1.0, 1.0, 1.0, 1, now));

    assert_true(bizer.get(0).time() == (time_t)0);
    assert_true(bizer.get(1).time() == (time_t)0);
    assert_true(bizer.get(2).time() == (time_t)0);
    assert_true(bizer.get(3).time() == (time_t)1000);
    assert_true(bizer.get(4).time() == (time_t)1010);

    now = (time_t)999;
    bizer.update(Bucket(1.0, 1.0, 1.0, 1.0, 1, now));

    assert_true(bizer.get(0).time() == (time_t)0);
    assert_true(bizer.get(1).time() == (time_t)0);
    assert_true(bizer.get(2).time() == (time_t)999);
    assert_true(bizer.get(3).time() == (time_t)1000);
    assert_true(bizer.get(4).time() == (time_t)1010);

    now = (time_t)988;
    bizer.update(Bucket(1.0, 1.0, 1.0, 1.0, 1, now));

    assert_true(bizer.get(0).time() == (time_t)0);
    assert_true(bizer.get(1).time() == (time_t)988);
    assert_true(bizer.get(2).time() == (time_t)999);
    assert_true(bizer.get(3).time() == (time_t)1000);
    assert_true(bizer.get(4).time() == (time_t)1010);

    now = (time_t)977;
    bizer.update(Bucket(1.0, 1.0, 1.0, 1.0, 1, now));

    assert_true(bizer.get(0).time() == (time_t)977);
    assert_true(bizer.get(1).time() == (time_t)988);
    assert_true(bizer.get(2).time() == (time_t)999);
    assert_true(bizer.get(3).time() == (time_t)1000);
    assert_true(bizer.get(4).time() == (time_t)1010);
}

void test_bucketizer_can_accumulated_buckets() {
    time_t now = (time_t)1000;
    Bucketizer bizer(now);

    bizer.update(Bucket(1.0, 1.0, 1.0, 1.0, 1, now));

    now = (time_t)1001;
    bizer.update(Bucket(2.0, 4.0, 2.0, 2.0, 1, now));

    assert_true(bizer.get(0).time() == (time_t)0);
    assert_true(bizer.get(1).time() == (time_t)0);
    assert_true(bizer.get(2).time() == (time_t)0);
    assert_true(bizer.get(3).time() == (time_t)1000);
    assert_true(bizer.get(4).time() == (time_t)0);

    Bucket b = bizer.get(3);

    assert_true(b.sum() == 3.0);
    assert_true(b.sumSq() == 5.0);
    assert_true(b.min() == 1.0);
    assert_true(b.max() == 2.0);
    assert_true(b.count() == 2);
}

void test_bucketizer_update_sets_error_flag() {
    time_t now = (time_t)1000;
    Bucketizer bizer(now);

    // good
    bizer.update(Bucket(1.0, 1.0, 1.0, 1.0, 1, now));
    assert_true(bizer.getUpdateErrMsg() == "");

    // bad too old
    now = (time_t)969;
    bizer.update(Bucket(1.0, 1.0, 1.0, 1.0, 1, now));
    assert_false(bizer.getUpdateErrMsg() == "");

    // bad too new
    now = (time_t)1020;
    bizer.update(Bucket(1.0, 1.0, 1.0, 1.0, 1, now));
    assert_false(bizer.getUpdateErrMsg() == "");

    // good again
    now = (time_t)1001;
    bizer.update(Bucket(1.0, 1.0, 1.0, 1.0, 1, now));
    assert_true(bizer.getUpdateErrMsg() == "");
}

void test_bucketizer_constructor_with_update_sets_error_flag() {
    time_t now = (time_t)1000;
    time_t too_old = (time_t)969;

    Bucketizer bizer_good(now, Bucket(1.0, 1.0, 1.0, 1.0, 1, now));
    Bucketizer bizer_bad(now, Bucket(1.0, 1.0, 1.0, 1.0, 1, too_old));

    assert_true(bizer_good.getUpdateErrMsg() == "");
    assert_true(bizer_bad.getUpdateErrMsg() == "bad time: 960 not close enough to 1000 at offset -1 delta -40");
}

void func() {
    test_buckets_distributed_into_bucketizer();
    test_bucketizer_can_accumulated_buckets();
    test_bucketizer_update_sets_error_flag();
    test_bucketizer_constructor_with_update_sets_error_flag();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
