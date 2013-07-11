#include "istat/Bucket.h"
#include "istat/strfunc.h"
#include <string>
#include <sstream>
#include <cstring>
#include <cmath>
#include <ctime>
#include <iostream>

namespace istat
{

    Bucket::Bucket(bool clear)
    {
        if (clear)
        {
            /* allow for placement new to not clear the data */
            memset(this, 0, sizeof(*this));
        }
    }

    Bucket::Bucket(double s, float sq, float mi, float ma, int c, time_t time)
        : time_(time),
        sum_(s),
        min_(mi),
        max_(ma),
        count_(c),
        sumSq_(sq)
    {
    }

    Bucket::Bucket(Bucket const &o, time_t time)
    {
        memcpy(this, &o, sizeof(*this));
        time_ = time;
    }

    std::string Bucket::dateStr() const
    {
        return iso_8601_datetime(time_);
    }

    time_t Bucket::time() const
    {
        return time_;
    }

    double Bucket::sum() const
    {
        return sum_;
    }

    float Bucket::sumSq() const
    {
        return sumSq_;
    }

    float Bucket::min() const
    {
        return min_;
    }

    float Bucket::max() const
    {
        return max_;
    }

    int Bucket::count() const
    {
        return count_;
    }

    void Bucket::setCount(int count)
    {
        count_ = count;
    }

    void Bucket::update(Bucket const &o)
    {
        if (count_ == 0)
        {
            *this = o;
        }
        else
        {
            sum_ = sum_ + o.sum_;
            sumSq_ = sumSq_ + o.sumSq_;
            min_ = std::min(min_, o.min_);
            max_ = std::max(max_, o.max_);
            count_ = count_ + o.count_;
        }
    }

    void Bucket::expUpdate(Bucket const &o, double lambda)
    {
        if (count_ == 0)
        {
            *this = o;
        }
        else
        {
            double me = lambda;
            double you = 1 - lambda;
            sum_ = sum_ * me + o.sum_ * you;
            sumSq_ = (float)(sumSq_ * me + o.sumSq_ * you);
            min_ = (float)(min_ * me + o.min_ * you);
            max_ = (float)(max_ * me + o.max_ * you);
            //  truncate, round up on 0.5
            count_ = std::max((int32_t)(count_ * me + o.count_ * you + 0.5), 1);
            time_ = o.time_;
        }
    }

    void Bucket::collatedUpdate(double v, time_t t)
    {
        if(count_ == 0)
        {
            time_ = t;
        }
        sum_ += v;
        min_ = max_ = sum_;
        sumSq_ = sum_ * sum_;
        count_ = 1;
    }

    double Bucket::avg() const
    {
        return count_ > 0 ? double(sum_) / double(count_) : 0;
    }

    float Bucket::sdev() const
    {
        if (count_ < 2)
        {
            return 0;
        }
        //  because of cancellation, this may be slightly negative
        double diff = double(count_) * double(sumSq_) - sum_ * sum_;
        if (diff < 0)
        {
            diff = 0;
        }
        return sqrt(diff / (double(count_) * (double(count_) - 1)));
    }
}

