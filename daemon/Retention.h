
#if !defined(daemon_Retention_h)
#define daemon_Retention_h

#include <time.h>
#include <string>
#include <vector>
#include <istat/IRecorder.h>

struct RetentionInterval
{
    time_t interval;
    time_t samples;
    std::string name;
    istat::RecordStats stats;
};

class RetentionPolicy
{
public:
    template<size_t Count>
    RetentionPolicy(RetentionInterval const (&args)[Count])
    {
        init(args, Count);
    }
    RetentionPolicy();
    RetentionPolicy(RetentionInterval const *ri, size_t cnt);
    RetentionPolicy(char const *ivals);

    size_t countIntervals() const;
    RetentionInterval const &getInterval(size_t ix) const;
    void addInterval(RetentionInterval const &ri);
    void addInterval(std::string const &str);
    void addIntervals(std::string const &str);
private:
    void init(RetentionInterval const *ptr, size_t cnt);
    std::vector<RetentionInterval> intervals;
};


#endif  //  daemon_Retention_h

