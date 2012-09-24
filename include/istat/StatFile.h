
#if !defined(istat_StatFile_h)
#define istat_StatFile_h

#include "istat/Bucket.h"
#include "istat/Header.h"
#include <string.h>

namespace istat
{
    class Mmap;
    class IRecorder;

    struct Stats
    {
        Stats() : statHit(0), statMiss(0) {}
        IRecorder *statHit;
        IRecorder *statMiss;
    };

    enum RawUpdateMode
    {
        // Default behaviour. Respects StatFile flags,
        // and appropriately aggregates new data.
        RAWUP_AGGREGATE,
        // For importing. Overwrite raw bucket
        // data with new data. Disregards StatFile flags
        // and just dumps data. Be careful!
        RAWUP_OVERWRITE,
        // For importing. Like RAWUP_OVERWRITE,
        // but only copies over empty buckets.
        RAWUP_FILL_EMPTY
    };

    class StatFile
    {
    public:
        struct Settings
        {
            time_t zeroTime;        //  if 0, use current time rounded down
            time_t intervalTime;    //  in seconds
            int64_t numSamples;     //  number of buckets stored in the file (fixed on file creation)
            char unit[64];          //  currently stored but unused
            uint32_t flags;         //  FILE_FLAG_IS_COLLATED, FILE_FLAG_IS_TRAILING
            uint64_t season;        //  If trailing, the season (trail time)
            double lambda;          //  If trailing, the blend (how much to keep of the previous season)
            int64_t fixed_count;    //  If counter aggregate, the exact number of samples in a recorded bucket.
            Settings() { memset(this, 0, sizeof(*this)); }
        };
        StatFile(std::string const &path, Stats const &stats, Mmap *mm, bool forUpdate=true);
        StatFile(std::string const &path, Stats const &stats, Settings const &init, Mmap *mm, bool forceCreate = false);
        ~StatFile();

        const std::string getPath();
        
        bool updateBucket(Bucket const &data);
        void flush();
        void resetCumulative(time_t forTime=0);

        bool rawUpdateBucket(Bucket const &data, RawUpdateMode mode);

        Settings const &settings() const;
        Header const &header() const;
        void cumulative(double &oSum, double &oSumSq, int64_t &oCount, int64_t &oResetTime);
        int64_t firstBucket() const;
        int64_t lastBucket() const;
        time_t firstBucketTime() const;
        time_t lastBucketTime() const;
        Bucket const &bucket(int64_t ix) const;
        int64_t readBuckets(Bucket *o, int cnt, int64_t offsetBack) const;
        int64_t mapTimeToBucketIndex(time_t time, bool round_up=false) const;
        int64_t mapBucketIndexToFileIndex(int64_t bucketIndex) const;
        bool isBucketIndexInFile(int64_t bucketIndex) const;
        int64_t numBucketsBetween(time_t start_time, time_t end_time, bool round_up=false) const;


    private:
        Settings settings_;
        Header *fileHeader_;
        Mmap *mm_;
        Stats stats_;
        int fd_;
        bool fileWritable_;
        struct BucketPage
        {
            int64_t page;
            Bucket *ptr;
            bool writable;
        };
        BucketPage pages[3];
        Bucket expBucket[3];
        int lruNextPage_;

        int64_t bucketsPerPage_;
        int64_t bucketCount_;

        void closefd();
        void open(std::string const &path, Settings const *init, bool forUpdate, bool forceCreate);
        void writeNewHeader(Settings const &init, std::string const &path);
        void refreshHeader(Header &hdr);
        void flushOneExpBucket();
        Bucket *mapBucketPage(int64_t page, bool toWrite);
        Bucket *mapSinglePage(int64_t page, bool toWrite);
        Bucket *replaceMap(int cacheIndex, int64_t pageIndex, bool toWrite);
        Bucket *writableBucket(int64_t ix);
        Bucket *getTrailingBucket(time_t time);
        void copyAliasedBuckets(Bucket *buf, size_t bCount, int64_t bPage, int64_t bOffset) const;
        void recordPageHit();
        void recordPageMiss();

        //  noncopyable
        StatFile(StatFile const &o);
        StatFile &operator=(StatFile const &o);
    };
}

#endif  //  istat_StatFile_h
