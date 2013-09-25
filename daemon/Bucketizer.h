
#if !defined(daemon_Bucketizer_h)
#define daemon_Bucketizer_h

#include <string>
#include <istat/Bucket.h>
#include <boost/cstdint.hpp>

class Bucketizer
{
public:
    inline Bucketizer() {} // allow placement new to not clear underlying data
    Bucketizer(time_t &time);
    Bucketizer(time_t &time, istat::Bucket const &b);
    void update(istat::Bucket const &o);
    istat::Bucket const &get(int i);
    static size_t const BUCKET_COUNT = 5;
    inline std::string const & getUpdateErrMsg() {return update_err_msg_;}

private:
    time_t now;
    std::string update_err_msg_;
    istat::Bucket buckets[BUCKET_COUNT];
};

#endif  //  daemon_Bucketizer_h
