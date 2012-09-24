
#if !defined(daemon_Timing_h)
#define daemon_Timing_h

#include <time.h>
#include <sys/types.h>
#include <inttypes.h>

class Timing
{
public:
    Timing();
    ~Timing();
    int64_t elapsedMicros() const;
    double elapsedSeconds() const;
    double operator-(Timing const &o) const;
private:
    struct timespec stamp_;
};

#endif  //  daemon_Timing_h

