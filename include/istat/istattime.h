
#if !defined(istat_istattime_h)
#define istat_istattime_h

#include <ctime>

namespace istat
{
    class FakeTime
    {
        public:
            FakeTime(time_t timestamp);
            ~FakeTime();
            void set(time_t timestamp);
    };

    time_t istattime(time_t *t);
}

#endif // istat_istattime_h
