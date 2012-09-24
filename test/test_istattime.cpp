#include <stdexcept>
#include <istat/test.h>
#include <istat/istattime.h>


using namespace istat;

int main()
{
    {
    	time_t now, now2;
	    now = istattime(&now2);
    	assert_equal(now, now2);

        time_t a, b, c;

        // Can we fake time?
        {
            FakeTime fake_time(1234);

            a = istattime(&b);

            assert_equal(a, 1234)
            assert_equal(a, b);
            assert_not_equal(a, now);
        }

        // After the FakeTime is destroyed, we get 'current' time again.
        c = istattime(0);
        assert_not_equal(a, c);
    }

    // Assert that an exception is thrown if two fake times coexist.
    {
        bool caught_exception = false;
        try
        {
            FakeTime fake_time_a(1234);
            FakeTime fake_time_b(4321);
        }
        catch (std::runtime_error& e)
        {
            caught_exception = true;

            std::string what(e.what());
            assert_equal(what, "attempted to create multiple FakeTime instances at once.");
        }
        assert_true(caught_exception);
    }

    // Assert that an exception is thrown when fake time is zero.
    {
        bool caught_exception = false;
        try
        {
            FakeTime fake_time(0);
        }
        catch (std::runtime_error& e)
        {
            caught_exception = true;

            std::string what(e.what());
            assert_equal(what, "invalid timestamp argument to FakeTime -- must be non-zero.");
        }
        assert_true(caught_exception);
    }

    // assert that we can change fake time
    {
        FakeTime fake_time(1234);
        assert_equal(istattime(0), 1234);

        fake_time.set(5678);
        assert_equal(istattime(0), 5678);
    }

    // assert that we can set fake time to zero
    {
        bool caught_exception = false;

        FakeTime fake_time(1234);
        assert_equal(istattime(0), 1234);

        try
        {
            fake_time.set(0);
        }
        catch (std::runtime_error& e)
        {
            caught_exception = true;

            std::string what(e.what());
            assert_equal(what, "invalid timestamp argument to FakeTime -- must be non-zero.");
        }
        assert_true(caught_exception);
    }
}
