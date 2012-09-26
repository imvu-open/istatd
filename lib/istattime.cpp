#include <stdexcept>
#include <istat/test.h>
#include <istat/istattime.h>


namespace
{
    time_t fake = 0;
}

namespace istat
{

FakeTime::FakeTime(time_t timestamp)
{
    if (fake != 0)
    {
        throw std::runtime_error("attempted to create multiple FakeTime instances at once.");
    }
    FakeTime::set(timestamp);
}

FakeTime::~FakeTime()
{
    fake = 0;
}

void FakeTime::set(time_t timestamp) {
    if (timestamp == 0)
    {
        throw std::runtime_error("invalid timestamp argument to FakeTime -- must be non-zero.");
    }

    fake = timestamp;
}

time_t istattime(time_t *t)
{
	if (fake)
    {
		if (t)
        {
			*t = fake;
		}
		return fake;
	}
	return time(t);
}

}

