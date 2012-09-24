
#include "istat/StatFile.h"
#include "istat/test.h"
#include "istat/IRecorder.h"
#include <istat/Mmap.h>
#include <stdexcept>
#include <iostream>

using namespace istat;


Mmap *mm(NewMmap());

class WrapMmap : public Mmap
{
public:
    WrapMmap(Mmap *mm) :
        mm_(mm)
    {
        open_ = close_ = read_ = write_ = seek_ = tell_ = truncate_ =
            map_ = unmap_ = flush_ = available_ = 0;
        lastOffsetMapped_ = -1;
    }
    Mmap *mm_;
    int open_;
    int close_;
    int read_;
    int write_;
    int seek_;
    int tell_;
    int truncate_;
    int map_;
    int unmap_;
    int flush_;
    int available_;

    int64_t lastOffsetMapped_;

    int open(char const *name, int flags)
    {
        ++open_;
        return mm_->open(name, flags);
    }
    int close(int fd)
    {
        ++close_;
        return mm_->close(fd);
    }
    ssize_t read(int fd, void *ptr, ssize_t amt)
    {
        ++read_;
        return mm_->read(fd, ptr, amt);
    }
    ssize_t write(int fd, void *ptr, ssize_t amt)
    {
        ++write_;
        return mm_->write(fd, ptr, amt);
    }
    ptrdiff_t seek(int fd, ptrdiff_t offset, int whence)
    {
        ++seek_;
        return mm_->seek(fd, offset, whence);
    }
    ssize_t tell(int fd)
    {
        ++tell_;
        return mm_->tell(fd);
    }
    int truncate(int fd, ssize_t size)
    {
        ++truncate_;
        return mm_->truncate(fd, size);
    }
    void *map(int fd, int64_t offset, size_t size, bool writable)
    {
        ++map_;
        lastOffsetMapped_ = offset;
        return mm_->map(fd, offset, size, writable);
    }
    bool unmap(void const *ptr, size_t size)
    {
        ++unmap_;
        return mm_->unmap(ptr, size);
    }
    bool flush(void const *ptr, size_t size, bool immediate)
    {
        ++flush_;
        return mm_->flush(ptr, size, immediate);
    }
    int64_t availableSpace(char const *path)
    {
        ++available_;
        return mm_->availableSpace(path);
    }
    void dispose()
    {
        // do nothing
    }
    void counters(int64_t *oMaps, int64_t *oUnmaps, int64_t *oOpens, int64_t *oCloses)
    {
        *oMaps = map_;
        *oUnmaps = unmap_;
        *oOpens = open_;
        *oCloses = close_;
    }
};

void setUp()
{
    unlink("created.sf");

    StatFile::Settings sett;
    sett.zeroTime = 1000000000;
    sett.intervalTime = 10;
    sett.numSamples = 10000;
    RecordStats rs;
    StatFile sf("created.sf", rs, sett, mm, true);

    Bucket b(4, 16, 4, 4, 1, 1000000000);
    sf.updateBucket(b);

    b = Bucket(10, 50, 5, 5, 2, 1000000001);
    sf.updateBucket(b);

    b = Bucket(9, 16+25, 4, 5, 2, 1000000010);
    sf.updateBucket(b);

    b = Bucket(10, 9+9+16, 3, 4, 3, 1000000020);
    sf.updateBucket(b);

    assert_equal(rs.nHits.stat_, 3);
    assert_equal(rs.nMisses.stat_, 1);
}

void func()
{
    // test when file does not exist
    {
        bool ok = false;
        try
        {
            StatFile sf("doesnotexist.sf", Stats(), mm);
        }
        catch (std::runtime_error const &re)
        {
            ok = true;
        }
        assert_true(ok);
    }

    setUp();

    // test mapTimeToBucketIndex() and mapBucketIndexToFileIndex
    {
        StatFile sf("created.sf", Stats(), mm);
        assert_equal(2, sf.lastBucket());

        assert_equal(0, sf.mapTimeToBucketIndex(1000000000));
        assert_equal(0, sf.mapTimeToBucketIndex(1000000001));
        assert_equal(0, sf.mapBucketIndexToFileIndex(0));
        assert_equal(1, sf.mapTimeToBucketIndex(1000000010));
        assert_equal(1, sf.mapBucketIndexToFileIndex(1));
        assert_equal(2, sf.mapTimeToBucketIndex(1000000020));
        assert_equal(2, sf.mapBucketIndexToFileIndex(2));

        int64_t calculated_first_bucket_time = 1000000020 - (10240-1)*10;
        assert_equal(-10237, sf.mapTimeToBucketIndex(calculated_first_bucket_time));
        assert_equal(3, sf.mapBucketIndexToFileIndex(-10237));

        // go backwards across the wrap boundry
        assert_equal(-1, sf.mapTimeToBucketIndex(1000000000-10));
        assert_equal(10239, sf.mapBucketIndexToFileIndex(-1));

        // go forwards twice across the wrap boundry
        assert_equal(10241, sf.mapTimeToBucketIndex(1000000020+(10239*10)));
        assert_equal(-1, sf.mapBucketIndexToFileIndex(10241));

        // go forwards twice across the wrap boundry
        assert_equal(20481, sf.mapTimeToBucketIndex(1000000020+(10240*10)+(10239*10)));
        assert_equal(-1, sf.mapBucketIndexToFileIndex(20481));

        // test round_up
        assert_equal(1, sf.mapTimeToBucketIndex(1000000001, true));
        assert_equal(1, sf.mapBucketIndexToFileIndex(1));

    }
    mm->dispose();
    mm = NewMmap();

    // test numBucketsBetween()
    {
        StatFile sf("created.sf", Stats(), mm);
        assert_equal(2, sf.lastBucket());

        assert_equal(1, sf.numBucketsBetween(1000000000, 1000000000));
        assert_equal(1, sf.numBucketsBetween(1000000000, 1000000001));
        assert_equal(2, sf.numBucketsBetween(1000000001, 1000000011, true));
        assert_equal(1, sf.numBucketsBetween(1000000000, 1000000010));
    }

    mm->dispose();
    mm = NewMmap();

    {
        StatFile sf("created.sf", Stats(), mm);
        assert_equal(2, sf.lastBucket());

        Bucket const &b = sf.bucket(0);
        assert_equal(b.count(), 3);
        assert_equal(b.min(), 4.0);
        assert_equal(b.max(), 5.0);
        assert_equal(b.sum(), 14.0);
        assert_equal(b.sumSq(), 66);
        assert_equal(b.time(), 1000000000);

        Bucket const &c = sf.bucket(1);
        assert_equal(c.count(), 2);
        assert_equal(c.sumSq(), 16+25);
        assert_equal(c.time(), 1000000010);
        
        assert_equal(sf.settings().intervalTime, 10);
        ssize_t bpp = 8192 / sizeof(Bucket);
        ssize_t n = 10000;
        int ns = 0;
        while (n > 0)
        {
            ns = ns + bpp;
            n = n - bpp;
        }
        assert_equal(sf.settings().numSamples, ns);
        assert_equal(sf.settings().unit[0], 0);
    }

    mm->dispose();
    mm = NewMmap();
    //  test that pre-fetching is turned off
    {
        WrapMmap wm(mm);
        StatFile sf("created.sf", Stats(), &wm);
        assert_equal(2, sf.lastBucket());
        Bucket const &b = sf.bucket(2);
        assert_equal(b.count(), 3);
        assert_equal(wm.open_, 1);
        assert_equal(wm.close_, 0);
        assert_equal(wm.map_, 2);   //  file header, first page
        assert_equal(wm.unmap_, 0);
        assert_equal(wm.lastOffsetMapped_, 8192);   //  page size
        size_t n = 8192 / sizeof(Bucket);
        for (size_t i = 3; i < n-1; ++i)
        {
            Bucket c(10, 50, 5, 5, 2, 1000000000 + 10 * i);
            sf.updateBucket(c);
        }
        assert_equal(wm.map_, 2);
        assert_equal(wm.lastOffsetMapped_, 8192);
        {
            Bucket c(10, 50, 5, 5, 2, 1000000000 + 10 * n);
            sf.updateBucket(c);
        }
        //  fetched new now that we hit the next bucket
        assert_equal(wm.map_, 3);
        assert_equal(wm.lastOffsetMapped_, 8192 * 2);
        int64_t no, nc, nm, nu;
        mm->counters(&nm, &nu, &no, &nc);
        assert_equal(nm, wm.map_);
        assert_equal(nu, wm.unmap_);
        assert_equal(no, wm.open_);
        assert_equal(nc, wm.close_);
    }

    //  test that we deal with intermediate empty buckets
    {
        setUp();
        StatFile sf("created.sf", Stats(), mm);

        Bucket b(123, 456, 3323, 4, 3, 1000000000 + 100);
        sf.updateBucket(b);

        for (int i = 0; i < 9; ++i)
        {
            Bucket const &c = sf.bucket(i);
            if (c.time() != 0)
            {
                assert_equal(c.time(), 1000000000 + i * 10);
            }
        }

        Bucket const &c = sf.bucket(6);
        assert_equal(c.min(), 0);

        Bucket const &d = sf.bucket(10);
        assert_equal(d.min(), 3323);
    }

    //  test seasonal buckets
    {
        unlink("season.sf");
        StatFile::Settings settings;
        settings.zeroTime = 1000000000;
        settings.intervalTime = 10;
        settings.numSamples = 8640;
        settings.flags = FILE_FLAG_IS_TRAILING;
        settings.season = 86400;
        settings.lambda = 0.5;
        StatFile sf("season.sf", Stats(), settings, mm, true);
        {
            Bucket b1(2, 4, 2, 2, 1, 1000100000);
            sf.updateBucket(b1);
            sf.updateBucket(Bucket(b1, 1000100100));
            sf.updateBucket(Bucket(b1, 1000100110));
            sf.updateBucket(Bucket(b1, 1000100120));
            sf.updateBucket(Bucket(b1, 1000100130));
            sf.updateBucket(Bucket(b1, 1000100140));
            sf.updateBucket(Bucket(b1, 1000110100));
            sf.updateBucket(Bucket(b1, 1000110110));
            sf.updateBucket(Bucket(b1, 1000110120));
            sf.updateBucket(Bucket(b1, 1000110130));
            sf.updateBucket(Bucket(b1, 1000110140));
        }
        {
            //  86400 after the previous write -- one season
            Bucket b2(0, 0, 0, 0, 1, 1000196500);
            sf.updateBucket(b2);
            sf.updateBucket(Bucket(b2,1000196530));
            sf.updateBucket(Bucket(b2,1000196540));
            sf.updateBucket(Bucket(b2,1000196550));
        }
        int64_t ix = sf.mapTimeToBucketIndex(1000196500);
        Bucket bOut;
        int64_t r = sf.readBuckets(&bOut, 1, ix);
        assert_equal(r, 1);
        assert_equal(bOut.count(), 1);
        assert_equal(bOut.min(), 0.5 * 2);
        assert_equal(bOut.max(), 0.5 * 2);
        assert_equal(bOut.sum(), 0.5 * 2);
        assert_equal(bOut.sumSq(), 0.5 * 4);
        sf.flush();
    }

    //test time_to_bucket_index returns -1 for time older than bucket range
    {
        setUp();

        StatFile sf("created.sf", Stats(), mm);
        StatFile::Settings const &settings = sf.settings();
        Bucket const &last_bucket = sf.bucket(sf.lastBucket());

        time_t last_time = last_bucket.time();
        time_t old_time = last_time - (settings.numSamples+1) * settings.intervalTime;
        int64_t old_index = sf.mapTimeToBucketIndex(old_time, true);
        assert_equal(-10239, old_index);
        assert_equal(-1, sf.mapBucketIndexToFileIndex(old_index));

    }

    //test firstBucketTime and lastBucketTime
    {
        setUp();
        StatFile sf("created.sf", Stats(), mm);
        assert_equal(1000000020, sf.lastBucketTime());
        assert_equal(1000000020 - (10240-1)*10, sf.firstBucketTime());
        assert_equal(1000000000, sf.bucket(sf.firstBucket()).time());
    }

}

int main(int argc, char const *argv[])
{
    return istat::test(func, argc, argv);
}

