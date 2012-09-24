
#if !defined(istat_header_h)
#define istat_header_h

#include <boost/cstdint.hpp>

namespace istat
{
    enum
    {
        //  Current version written (and max version read)
        //  Version 2: add 'flags' field
        //  Version 4: bucket format changed to use "double" for sum
        //  Version 6: lambda, season
        //  Version 8: fixed count for counter aggregate buckets.
        CUR_HDR_VERSION = 8,
        //  Files written are readable by this version
        RD_HDR_VERSION = 4
    };

    enum
    {
        // COLLLATED is a flag to indicate that counter data is being stored in the lowest resolution bucket, e.g. 10s interval
        FILE_FLAG_IS_COLLATED = 0x1,
        // 
        FILE_FLAG_IS_TRAILING = 0x2,
        // COUNTER_AGGREGATE is term to describe rollup data from COLLATED counter files, e.g. the 5m data stat files from 
        // a COLLATED 10s file
        FILE_FLAG_IS_COUNTER_AGGREGATE = 0x4
    };

    struct Header4
    {
        char magic[16];
        int32_t hdr_size;
        int32_t cfg_interval;
        int32_t flags;
        int32_t cr_version;
        int32_t rd_version;

        int64_t page_size;
        int64_t page_count;
        int64_t first_bucket;
        int64_t last_bucket;
        int64_t last_time;
        double cumulative_sum;
        double cumulative_sum_sq;
        int64_t cumulative_count;
        int64_t last_cumulative_clear_time;
        int64_t file_create_time;

        char unit[64];
        char name[256];
    };

    struct Header6 : Header4
    {
        double lambda;
        uint64_t season;
    };

    struct Header8 : Header6
    {
        int64_t fixed_count;
    };

    typedef struct Header8 Header;

    extern const unsigned char file_magic[16];
}

#endif  //  istat_header_h

