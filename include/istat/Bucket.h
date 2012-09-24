
#if !defined(istat_Bucket_h)
#define istat_Bucket_h

#include <string>
#include <boost/cstdint.hpp>

namespace istat
{
    class Bucket
    {
    public:
        inline Bucket() {} // allow placement new to not clear underlying data
        Bucket(bool clear);
        Bucket(double s, float sq, float mi, float ma, int c, time_t time);
        Bucket(Bucket const &copy, time_t time); // used for bucket aggregation
        void update(Bucket const &o);
        void expUpdate(Bucket const &o, double history);
        void collatedUpdate(double v, time_t t);
        std::string dateStr() const;
        time_t time() const;
        double sum() const;
        float sumSq() const;
        float min() const;
        float max() const;
        int count() const;
        void setCount(int count);

        double avg() const;
        float sdev() const;

    private:
        int64_t time_; // bucket time (unix epoch), not the clock time of the istatd when written
        double sum_;
        float min_;
        float max_;
        int32_t count_;
        float sumSq_;
    };
}

#endif  //  istat_Bucket_h
