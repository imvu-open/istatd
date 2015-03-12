
#include <istat/StatFile.h>
#include <istat/Log.h>
#include <istat/Mmap.h>
#include <istat/istattime.h>
#include <istat/IRecorder.h>

#include <boost/lexical_cast.hpp>

#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdexcept>
#include <string.h>
#include <assert.h>
#include <iostream>
#include <errno.h>
#include <stdio.h>


/* 
 * Prefetching only makes sense when using async I/O, 
 * which we did with mmap() but ran into kernel scalability
 * issues.
 */
#define PREFETCH 0
/*
 * Rejecting old data makes sense if you don't want external
 * clients to be able to warp the "current file pointer" back 
 * and forth in the data file/s.
 */
#define REJECT_OLD_DATA 0


namespace istat
{
    StatFile::StatFile(std::string const &path, Stats const &stats, Mmap *mm, bool forUpdate) :
        mm_(mm),
        stats_(stats)
    {
        fileHeader_ = 0;
        fd_ = -1;
        open(path, 0, forUpdate, false);
    }

    StatFile::StatFile(std::string const &path, Stats const &stats, Settings const &init, Mmap *mm, bool forceCreate) :
        mm_(mm),
        stats_(stats)
    {
        fileHeader_ = 0;
        fd_ = -1;
        open(path, &init, true, forceCreate);
    }

    StatFile::~StatFile()
    {
        for (size_t i = 0; i != sizeof(pages)/sizeof(pages[0]); ++i)
        {
            if (pages[i].ptr != 0)
            {
                mm_->unmap(pages[i].ptr, fileHeader_->page_size);
            }
        }
        if (fileHeader_)
        {
            mm_->unmap(fileHeader_, sizeof(Header));
        }
        closefd();
    }

    void StatFile::closefd()
    {
        if (fd_ >= 0)
        {
            mm_->close(fd_);
            fd_ = -1;
        }
    }

    StatFile::Settings const &StatFile::settings() const
    {
        return settings_;
    }

    istat::Header const &StatFile::header() const
    {
        return *fileHeader_;
    }

    void StatFile::cumulative(double &oSum, double &oSumSq, int64_t &oCount, int64_t &oResetTime)
    {
        oSum = fileHeader_->cumulative_sum;
        oSumSq = fileHeader_->cumulative_sum_sq;
        oCount = fileHeader_->cumulative_count;
        oResetTime = fileHeader_->last_cumulative_clear_time;
    }

    void StatFile::open(std::string const &path, Settings const *init, bool forUpdate, bool forceCreate)
    {
        Log(LL_Debug, "istat") << "StatFile::open opening " << path;
        memset(expBucket, 0, sizeof(expBucket));
        memset(pages, 0, sizeof(pages));
        lruNextPage_ = 0;
        fileWritable_ = forUpdate;
        int flags = forUpdate ? O_RDWR : O_RDONLY;
        if (init != NULL)
        {
            flags |= O_CREAT;
        }
        fd_ = mm_->open(path.c_str(), flags);
        if (fd_ < 0)
        {
            throw std::runtime_error(std::string("Could not open: ") + path +
                ": " + strerror(errno));
        }
        ptrdiff_t off = mm_->seek(fd_, 0, 2);
        if ((off == 0 || forceCreate) && (init != 0))
        {
            writeNewHeader(*init, path);
            off = mm_->seek(fd_, 0, 2);
        }
        fileHeader_ = (Header *)mm_->map(fd_, 0, sizeof(Header), forUpdate || forceCreate);
        if (!fileHeader_)
        {
            closefd();
            throw std::runtime_error(std::string("Could not map header: ") + path);
        }
        size_t wantFileSize = fileHeader_->page_size * (fileHeader_->page_count + 1);
        if (0 < wantFileSize - off)
        {
            Log(LL_Warning, "istat") << "File" << path << "is too short; extending from"
                << off << "to" << wantFileSize;
            int r = mm_->truncate(fd_, wantFileSize);
            if (r < 0)
            {
                closefd();
                throw std::runtime_error(std::string("Could not extend ") + path + " to "
                    + boost::lexical_cast<std::string>(wantFileSize) + " bytes");
            }
            off = mm_->seek(fd_, 0, 2);
        }
        Header &hdr = *(Header *)fileHeader_;
        if (memcmp(file_magic, hdr.magic, sizeof(file_magic)))
        {
            closefd();
            throw std::runtime_error(std::string("Not an istat file: ") + path);
        }
        if (hdr.rd_version > CUR_HDR_VERSION)
        {
            closefd();
            throw std::runtime_error(std::string("File version is too new: ") + path);
        }
        if (hdr.page_size < sysconf(_SC_PAGE_SIZE))
        {
            closefd();
            throw std::runtime_error(std::string("Page size inconsistent: ") + path);
        }
        if (hdr.hdr_size != sizeof(Header) && hdr.cr_version == CUR_HDR_VERSION)
        {
            closefd();
            throw std::runtime_error(std::string("Header size changed within the current version! ") + path +
                "header version" + boost::lexical_cast<std::string>((int)CUR_HDR_VERSION) + 
                "size" + boost::lexical_cast<std::string>(hdr.hdr_size) + 
                "expected" + boost::lexical_cast<std::string>(sizeof(Header)));
        }
        if (hdr.cr_version < CUR_HDR_VERSION)
        {
            refreshHeader(hdr);
        }
        bucketsPerPage_ = hdr.page_size / sizeof(Bucket);
        bucketCount_ = hdr.page_count * bucketsPerPage_;
        settings_.intervalTime = fileHeader_->cfg_interval;
        settings_.numSamples = bucketCount_;
        strncpy(settings_.unit, fileHeader_->unit, sizeof(settings_.unit));
        settings_.unit[sizeof(settings_.unit)-1] = 0;
        settings_.flags = fileHeader_->flags;
        settings_.lambda = fileHeader_->lambda;
        settings_.season = fileHeader_->season;
        settings_.fixed_count = fileHeader_->fixed_count;
        strncpy(fileHeader_->name, path.c_str(), sizeof(fileHeader_->name));
        fileHeader_->name[sizeof(fileHeader_->name)-1] = 0;
    }

    void StatFile::refreshHeader(Header &hdr)
    {
        if (hdr.cr_version < 5)
        {
            hdr.lambda = 0.9;
            hdr.season = 60*60*24;
            hdr.cr_version = 5;
        }
        if (hdr.cr_version < 7)
        {
            hdr.cr_version = 7;
        }
    }

    void StatFile::writeNewHeader(StatFile::Settings const &init, std::string const &path)
    {
        Header hdr;
        memset(&hdr, 0, sizeof(hdr));
        memcpy(hdr.magic, file_magic, sizeof(file_magic));
        hdr.hdr_size = (int)sizeof(Header);
        hdr.cfg_interval = init.intervalTime;
        hdr.cr_version = CUR_HDR_VERSION;
        hdr.rd_version = RD_HDR_VERSION;
        hdr.flags = init.flags;
        hdr.lambda = init.lambda;
        hdr.season = init.season;
        hdr.fixed_count = init.fixed_count;
        if (hdr.flags & FILE_FLAG_IS_TRAILING)
        {
            if (hdr.lambda <= 0 || hdr.lambda > 1)
            {
                throw std::runtime_error("header lambda must be in (0,1] for trailing history files");
            }
            if (hdr.season < 10 || hdr.season > 373*24*60*60)
            {
                throw std::runtime_error("header season must be in [10s,1y] for trailing history files");
            }
        }
        time_t lTime = init.zeroTime;
        if (lTime == 0)
        {
            istat::istattime(&lTime);
        }
        lTime -= lTime % hdr.cfg_interval;
        hdr.file_create_time = lTime;
        hdr.last_cumulative_clear_time = lTime;
        // Make sure settings don't contain undefined flags.
        if (init.flags & ~(FILE_FLAG_IS_COLLATED |
            FILE_FLAG_IS_TRAILING |
            FILE_FLAG_IS_COUNTER_AGGREGATE
        ))
        {
            throw std::runtime_error("Bad file flags in writeNewHeader()");
        }
        if (hdr.cfg_interval < 1)
        {
            hdr.cfg_interval = 1;
        }
        strncpy(hdr.unit, init.unit, sizeof(hdr.unit));
        strncpy(hdr.name, path.c_str(), sizeof(hdr.name));
        hdr.name[sizeof(hdr.name) - 1] = 0;
        uint64_t pSize = sysconf(_SC_PAGE_SIZE);
        if (pSize < 8192)
        {
            pSize = 8192;
        }
        hdr.page_size = pSize;
        uint64_t bpp = pSize / sizeof(Bucket);
        uint64_t np = (init.numSamples + bpp - 1) / bpp;
        //  When using trailing, I need to have enough pages 
        //  to cover the entire season.
        if ((hdr.flags & FILE_FLAG_IS_TRAILING) &&
            (np * bpp * hdr.cfg_interval < (uint64_t)hdr.season))
        {
            np = hdr.season / bpp / hdr.cfg_interval + 1;
        }
        //  The page-prefetch system requires more than one page in any file.
        if (np < 2)
        {
            np = 2;
        }
        hdr.page_count = np;
        hdr.first_bucket = 0;
        hdr.last_bucket = 0;
        hdr.last_time = 0;
        int i = mm_->truncate(fd_, 0);
        if (i >= 0)
        {
            i = mm_->truncate(fd_, hdr.page_size * (hdr.page_count + 1));
        }
        if (i < 0)
        {
            throw std::runtime_error(std::string("Could not allocate space for: ") + path);
        }
        mm_->seek(fd_, 0, 0);
        if (sizeof(Header) != mm_->write(fd_, &hdr, sizeof(hdr)))
        {
            throw std::runtime_error(std::string("Could not prepare header for: ") + path);
        }
    }

    int64_t StatFile::firstBucket() const
    {
        return fileHeader_->first_bucket;
    }

    int64_t StatFile::lastBucket() const
    {
        return fileHeader_->last_bucket;
    }

    time_t StatFile::firstBucketTime() const
    {
        // the time is relative to the last bucket in the file,
        // and _not_ the time of the bucket currently at the first 
        // file index
        return fileHeader_->last_time - (settings_.numSamples-1) * settings_.intervalTime;
    }

    time_t StatFile::lastBucketTime() const
    {
        return fileHeader_->last_time;
    }

    int64_t StatFile::readBuckets(Bucket *out_bucket_buf, int cnt, int64_t first_bucket) const
    {
        int64_t first_bucket_file_index = StatFile::mapBucketIndexToFileIndex(first_bucket);

        if (first_bucket_file_index < 0)
        {
            if (cnt+first_bucket_file_index < 0)
            {
                return 0;
            }
            cnt += first_bucket_file_index;   //  first_bucket_file_index is negative
            first_bucket_file_index = 0;
        }
        if (cnt > bucketCount_)
        {
            cnt = bucketCount_;
        }
        int64_t ret = 0;
        while (cnt > 0)
        {
            int64_t file_index = (first_bucket_file_index + ret) % bucketCount_;
            int64_t page_index = file_index / bucketsPerPage_;
            int64_t off = file_index - (page_index * bucketsPerPage_);
            int64_t ntr = bucketsPerPage_ - off;

            assert(ntr > 0);
            if (ntr > cnt)
            {
                ntr = cnt;
            }
            cnt -= ntr;
            mm_->seek(fd_, (page_index + 1) * fileHeader_->page_size + off * sizeof(Bucket), SEEK_SET);
            int64_t n = mm_->read(fd_, out_bucket_buf, sizeof(Bucket) * ntr);
            if (n > 0)
            {
                //  page_index is zero-based
                copyAliasedBuckets(out_bucket_buf, n / sizeof(Bucket), page_index, off);
            }
            if (0 < ntr * sizeof(Bucket) - n || cnt == 0)
            {
                if (n > 0)
                {
                    ret += n / sizeof(Bucket);
                }
                break;
            }
            out_bucket_buf += ntr;
            ret += ntr;
        }
        return ret;
    }

    void StatFile::copyAliasedBuckets(Bucket *buf, size_t bCount, int64_t bPage, int64_t bOffset) const
    {
        //  the "pages" array uses 1-based page indices (counting file header as 0)
        bPage = bPage + 1;
        for (size_t i = 0; i != sizeof(pages)/sizeof(pages[0]); ++i)
        {
            //  does this page alias?
            if (pages[i].page == bPage && pages[i].ptr && pages[i].writable)
            {
                memcpy(buf, pages[i].ptr + bOffset, bCount * sizeof(Bucket));
                return;
            }
        }
    }


    Bucket const &StatFile::bucket(int64_t ix) const
    {
        return *const_cast<StatFile *>(this)->writableBucket(ix);
    }

    //  "page" is raw page -- from beginning of file
    Bucket *StatFile::mapBucketPage(int64_t page, bool toWrite)
    {
        if (page < 0)
        {
            throw std::range_error(std::string("Bad index in StatFile::mapBucketPage() ")
                + fileHeader_->name);
        }
        if (toWrite && !fileWritable_)
        {
            throw std::logic_error(std::string("Attempt to map writable bucket in read-only StatFile")
                + fileHeader_->name);
        }
        //  Translate from "index among bucket pages" to "index among
        //  file pages" where the first page in the file is reserved for header.
        //  page_count can be 0 when page is 0 during setup
        if (page == 0)
        {
            page = 1;
        }
        else
        {
            page = (page % fileHeader_->page_count) + 1;
        }
        Bucket *ret = mapSinglePage(page, toWrite);
/* PREFETCH only makes sense when using asynchronous I/O */
#if PREFETCH
        if (toWrite)
        {
            //  'page' is in file space, which is 1 greater than page space.
            //  Thus, already incremented in page space. Wrap to page count,
            //  and add one to get back to page space -- voila, next page!
            page = (page % fileHeader_->page_count) + 1;
            mapSinglePage(page, toWrite);
        }
#endif
        return ret;
    }

    Bucket *StatFile::mapSinglePage(int64_t page, bool toWrite)
    {
        for (size_t i = 0; i < sizeof(pages)/sizeof(pages[0]); ++i)
        {
            if (pages[i].page == page && (!toWrite || pages[i].writable) && pages[i].ptr != 0)
            {
                recordPageHit();
                return pages[i].ptr;
            }
        }
        void *mapped = replaceMap(lruNextPage_, page, toWrite);
        lruNextPage_ = (lruNextPage_ + 1) % (sizeof(pages) / sizeof(pages[0]));
        return (Bucket *)mapped;
    }

    Bucket *StatFile::replaceMap(int cacheIndex, int64_t pageIndex, bool toWrite)
    {
        if (pages[cacheIndex].ptr != 0)
        {
            mm_->unmap(pages[cacheIndex].ptr, fileHeader_->page_size);
        }
        recordPageMiss();
        void *mapped = mm_->map(fd_, pageIndex * fileHeader_->page_size, fileHeader_->page_size, toWrite);
        if (!mapped)
        {
            std::stringstream msg;
            msg << "mmap(" << fileHeader_->name << ", offset=" << pageIndex * fileHeader_->page_size << ", size=" << fileHeader_->page_size << ") failed in StatFile::mapBucketPage()";
            std::string mstr(msg.str());
            Log(LL_Error, "istat") << mstr;
            throw std::runtime_error(mstr);
        }
        //  tell the kernel to please slurp these pages in, asynchronously
        pages[cacheIndex].ptr = (Bucket *)mapped;
        pages[cacheIndex].writable = toWrite;
        pages[cacheIndex].page = pageIndex;
        return (Bucket *)mapped;
    }

    std::string StatFile::getPath()
    {
        return std::string(fileHeader_ ? fileHeader_->name : "");
    }

    bool StatFile::updateBucket(Bucket const &data)
    {
        return rawUpdateBucket(data, istat::RAWUP_AGGREGATE);
    }

    bool StatFile::rawUpdateBucket(Bucket const &data, RawUpdateMode mode)
    {
        if (!fileWritable_)
        {
            throw std::logic_error(std::string("Attempt to update bucket in read-only StatFile ")
                + fileHeader_->name);
        }

        //  Work out which bucket to put the data in
        int64_t targetBucketIndex = mapTimeToBucketIndex(data.time(), false);
        int64_t targetBucketTime = data.time() - data.time() % fileHeader_->cfg_interval;
        int64_t latestBucketIndex  = fileHeader_->last_bucket;
        Bucket *bp = 0;

        if (targetBucketIndex > latestBucketIndex)
        {
            //  Note: I don't clear the bucket here. It will contain
            //  old data. The user will have to detect this and discard
            //  the data instead. This saves potentially tons of useless
            //  writes, which is the scarce resource in this system.
            fileHeader_->last_bucket = targetBucketIndex;
            fileHeader_->last_time = targetBucketTime;
            if (fileHeader_->first_bucket + bucketCount_ <= fileHeader_->last_bucket)
            {
                fileHeader_->first_bucket = fileHeader_->last_bucket - bucketCount_ + 1;
            }
        }

        if (!StatFile::isBucketIndexInFile(targetBucketIndex))
        {
            Log(LL_Warning, "libistat") << "Cannot go back to time" << data.time() << 
                "when at" << fileHeader_->last_time << ":" << fileHeader_->name;
            return false;
        }

        if (targetBucketIndex < fileHeader_->first_bucket)
        {
            fileHeader_->first_bucket = targetBucketIndex;
        }

        if (fileHeader_->flags & FILE_FLAG_IS_TRAILING)
        {
            bp = getTrailingBucket(targetBucketTime);
        }
        else
        {
            bp = writableBucket(targetBucketIndex);
            if (bp->time() != targetBucketTime)
            {
                memset(bp, 0, sizeof(*bp));
            }
        }

        bool updateAggregate = false;
        if (mode == RAWUP_ONLY_IF_EMPTY)
        {
            if (bp->count() == 0)
            {
                memcpy(bp, &data, sizeof(*bp));
                updateAggregate = true;
            }
        }
        else if((fileHeader_->flags & FILE_FLAG_IS_COLLATED)
            || mode == RAWUP_OVERWRITE
            || (mode == RAWUP_FILL_EMPTY && bp->count() == 0))
        {
            memcpy(bp, &data, sizeof(*bp));
            updateAggregate = true;
        }
        else
        {
            Bucket timeData(data, targetBucketTime);
            bp->update(timeData);

            if(fileHeader_->flags & FILE_FLAG_IS_COUNTER_AGGREGATE)
            {
                bp->setCount(fileHeader_->fixed_count);
            }
            updateAggregate = true;
        }
        if (updateAggregate)
        {
            fileHeader_->cumulative_sum += data.sum();
            fileHeader_->cumulative_sum_sq += data.sumSq();
            fileHeader_->cumulative_count += data.count();
        }
        return true;
    }

    Bucket *StatFile::writableBucket(int64_t bucket_index)
    {
        Log(LL_Debug, "istat") << "StatFile::writableBucket(" << bucket_index << ")";
        int64_t file_index = StatFile::mapBucketIndexToFileIndex(bucket_index);
        int64_t page_index = file_index / bucketsPerPage_;
        int64_t off = file_index - (page_index * bucketsPerPage_);
        Bucket *bp = mapBucketPage(page_index, true);
        return &bp[off];
    }

    Bucket *StatFile::getTrailingBucket(time_t time)
    {
        Log(LL_Debug, "istat") << "StatFile::getTrailingBucket()" << time << getPath();
        size_t n = sizeof(expBucket)/sizeof(expBucket[0]);
        for (size_t i = 0; i != n; ++i)
        {
            if (expBucket[i].time() == time)
            {
                return &expBucket[i];
            }
        }
        //  maybe flush to disk -- also, shift buckets over one
        flushOneExpBucket();
        expBucket[n-1] = Bucket(0, 0, 0, 0, 0, time);
        return &expBucket[n-1];
    }

    void StatFile::flushOneExpBucket()
    {
        //  flush the bucket being shifted out, but nothing else
        Bucket &eb0(expBucket[0]);
        Log(LL_Debug, "istat") << "StatFile::flushOneExpBucket()" << eb0.time() << getPath();
        if (eb0.time() > 0)
        {
            int64_t ix = mapTimeToBucketIndex(eb0.time());
            int64_t oix = mapTimeToBucketIndex(eb0.time() - fileHeader_->season);
            Bucket *wb = writableBucket(ix);
            Bucket const &o = bucket(oix);
            if (o.time() > 0)
            {
                *wb = o;
                wb->expUpdate(eb0, fileHeader_->lambda);
            }
            else
            {
                *wb = eb0;
            }
        }
        //  must move one over
        memmove(expBucket, &expBucket[1], sizeof(expBucket)-sizeof(expBucket[0]));
        expBucket[sizeof(expBucket)/sizeof(expBucket[0])-1] = Bucket(true);
    }

    void StatFile::flush()
    {
        Log(LL_Debug, "istat") << "StatFile::flush()";
        //  Flush at most one expBucket, because the test may still be waiting for data.
        if (fileHeader_->flags & FILE_FLAG_IS_TRAILING)
        {
            flushOneExpBucket();
        }
        mm_->flush(fileHeader_, sizeof(Header), false);
        for (size_t i = 0; i != sizeof(pages) / sizeof(pages[0]); ++i)
        {
            if (pages[i].ptr != 0)
            {
                mm_->flush(pages[i].ptr, fileHeader_->page_size, false);
            }
        }
    }

    void StatFile::resetCumulative(time_t lTime)
    {
        fileHeader_->cumulative_sum = 0;
        fileHeader_->cumulative_sum_sq = 0;
        fileHeader_->cumulative_count = 0;
        if (lTime == 0)
        {
            istat::istattime(&lTime);
        }
        fileHeader_->last_cumulative_clear_time = lTime;
    }

    /* Bucket indices are just quantized time -- they are not raw (physical) 
       indices into the file, but have to be modulo-ed by file size to find 
       actual location in file. This modulo has to happen at time-of-use. 
       This is a good thing. 
       See StatFile::mapBucketIndexToFileIndex() */
    int64_t StatFile::mapTimeToBucketIndex(time_t timestamp, bool round_up) const
    {
    	int interval = fileHeader_->cfg_interval;
        time_t create_time = fileHeader_->file_create_time;

        int round_delta = (round_up && (timestamp % interval)) ? 1 : 0;
        int64_t bucket_index = ((timestamp - create_time) / interval + round_delta);

        return bucket_index;
    }

    time_t StatFile::mapBucketIndexToTime(int64_t bucket_index) const
    {
        int interval = fileHeader_->cfg_interval;
        time_t create_time = fileHeader_->file_create_time;

        time_t bucket_time = create_time + (bucket_index * interval);
        return bucket_time;
    }

    bool StatFile::isBucketIndexInFile(int64_t bucket_index) const
    {
        int64_t num_buckets = settings_.numSamples;
        int64_t latest_bucket = fileHeader_->last_bucket;

        if (bucket_index <= (latest_bucket-num_buckets)) 
        {
            //timestamp is older than anything in the file
            Log(LL_Debug, "istat") << "StatFile::isBucketIndexInFile(" << bucket_index << ") is less than earliest bucket " << (latest_bucket) << "-" << num_buckets << " .. rejecting.";
            return false;
        }

        if (bucket_index > latest_bucket) 
        {
            time_t now = istat::istattime(0);
            time_t bucket_index_time = mapBucketIndexToTime(bucket_index);
            // if we are collated, and the requested time is greater than latest bucket, and less than "now", return true 
            if ( (settings_.flags & FILE_FLAG_IS_COLLATED)
                 && (bucket_index_time < now)) {
                return true;
            } 
            Log(LL_Debug, "istat") << "StatFile::isBucketIndexInFile(" << bucket_index << ") is greater than latest bucket " << latest_bucket << " .. rejecting.";
            return false;
        }

        return true;
    }

     int64_t StatFile::mapBucketIndexToFileIndex(int64_t bucket_index) const
     {
        int64_t num_buckets = settings_.numSamples;

        if (!StatFile::isBucketIndexInFile(bucket_index))
        {
            return -1;
        }

        int64_t file_index = bucket_index % num_buckets;

        if (file_index < 0) 
        {
            file_index += num_buckets;
        }

        return file_index;
    }

    int64_t StatFile::numBucketsBetween(time_t start_time, time_t end_time, bool round_up) const
    {
    	int interval = fileHeader_->cfg_interval;

        int maybe_round_up = (round_up && (end_time % interval)) ? 1 : 0;
    	time_t normalized_start_time = start_time - (start_time % interval);
       	time_t normalized_end_time = end_time - (end_time % interval) + (maybe_round_up * interval);

        time_t delta = normalized_end_time - normalized_start_time;
        return delta ? delta / interval : 1;    
    }

    void StatFile::recordPageHit()
    {
        if (stats_.statHit)
        {
            stats_.statHit->record(1);
        }
    }

    void StatFile::recordPageMiss()
    {
        if (stats_.statMiss)
        {
            stats_.statMiss->record(1);
        }
    }
}
