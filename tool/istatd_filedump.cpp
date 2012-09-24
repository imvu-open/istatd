
#include "istat/StatFile.h"
#include "istat/strfunc.h"
#include <istat/Mmap.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <stdexcept>


using namespace istat;

bool full = false;
bool force = false;

Mmap *mm(NewMmap());


void dump_str(char const *name, char const *data, size_t sz)
{
    std::cout << name << "=\"";
    while (sz > 0)
    {
        if (*data < 32 || *data > 126)
        {
            if (*data == 10)
            {
                std::cout << "\\n";
            }
            else if (*data == 13)
            {
                std::cout << "\\r";
            }
            else
            {
                std::cout << "\\";
                std::cout << std::oct << std::setw(3) << std::setfill('0') <<
                    (int)*(unsigned char *)data << std::dec << std::setw(0) <<
                    std::setfill(' ');
            }
        }
        else
        {
            if (*data == '"' || *data == '\\')
            {
                std::cout << "\\";
            }
            std::cout << *data;
        }
        ++data;
        --sz;
    }
    std::cout << "\"" << std::endl;
}


template<typename T>
void dumpHdr(char const *name, T const &v)
{
    std::cout << name << "=" << v << std::endl;
}
//  specialize for std::string
template<>
void dumpHdr(char const *name, std::string const &v)
{
    dump_str(name, v.data(), v.size());
}
//  specialize for char arrays
template<size_t Size>
void dumpHdr(char const *name, char const (&v)[Size])
{
    dump_str(name, &v[0], Size);
}




void checkHeader(Header &h, int64_t sz, bool ignoreError)
{
    size_t hs;
    if (!memcpy(h.magic, file_magic, sizeof(h.magic)))
    {
        std::cerr << "header magic is wrong -- not an istat file" << std::endl;
        if (!ignoreError)
        {
            goto error;
        }
    }
    if (h.rd_version < 1 || h.rd_version > CUR_HDR_VERSION)
    {
        std::cerr << "bad file version " << h.rd_version << "; wanted 1-" << CUR_HDR_VERSION << std::endl;
        if (!ignoreError)
        {
            goto error;
        }
        std::cerr << "assuming file version " << CUR_HDR_VERSION << std::endl;
        h.rd_version = CUR_HDR_VERSION;
    }
    if (h.page_size < 4096 || h.page_size > 65536 || (h.page_size & (h.page_size - 1)))
    {
        std::cerr << "page size " << h.page_size << " is not an appropriate power of 2." << std::endl;
        if (!ignoreError)
        {
            goto error;
        }
        h.page_size = 8192;
        std::cerr << "assuming page size of 8192." << std::endl;
    }
    if (h.page_count < 2 || h.page_count > 1024*1024*1024)
    {
        std::cerr << "page count " << h.page_count << " is out of range." << std::endl;
        if (!ignoreError)
        {
            goto error;
        }
        h.page_count = sz / h.page_size - 1;
        std::cerr << "assuming page count of " << h.page_count << std::endl;
    }
    if (h.page_count > sz / h.page_size - 1)
    {
        std::cerr << "page count " << h.page_count << " is larger than the file." << std::endl;
        if (!ignoreError)
        {
            goto error;
        }
        h.page_count = sz / h.page_size - 1;
        std::cerr << "assuming page count of " << h.page_count << std::endl;
    }
    hs = 0;
    switch (h.cr_version)
    {
        case 0:
        case 1:
        case 2:
        case 3:
            hs = 0;
            break;
        case 4:
        default:
            hs = sizeof(Header4);
            break;
    }
    if (h.hdr_size < (int64_t)hs)
    {
        std::cerr << "Header size " << h.hdr_size << " is too small; expected " << hs << std::endl;
        if (!ignoreError)
        {
            goto error;
        }
        std::cerr << "assuming header size " << hs << std::endl;
        h.hdr_size = hs;
    }
    if (h.hdr_size > h.page_size)
    {
        std::cerr << "Header size " << h.hdr_size << " is bigger than page size " << h.page_size << std::endl;
        if (!ignoreError)
        {
            goto error;
        }
        std::cerr << "assuming header size " << hs << std::endl;
        h.hdr_size = hs;
    }
    if (h.cfg_interval < 1 || h.cfg_interval > 86400)
    {
        std::cerr << "Retention interval " << h.cfg_interval << " is out of bounds!" << std::endl;
        if (!ignoreError)
        {
            goto error;
        }
        std::cerr << "assuming header size " << 3600 << std::endl;
        h.cfg_interval = 3600;
    }
    return;
error:
    throw std::runtime_error("header check failed");
}

void dumpHeader(Header &h)
{
    std::cout << "# Header: Name=Value" << std::endl;
    dumpHdr("magic", h.magic);
    dumpHdr("hdr_size", h.hdr_size);
    dumpHdr("cfg_interval", h.cfg_interval);
    dumpHdr("cr_version", h.cr_version);
    dumpHdr("rd_version", h.rd_version);
    dumpHdr("unit", h.unit);
    dumpHdr("name", h.name);
    dumpHdr("page_size", h.page_size);
    dumpHdr("page_count", h.page_count);
    dumpHdr("first_bucket", h.first_bucket);
    dumpHdr("last_bucket", h.last_bucket);
    dumpHdr("last_time", iso_8601_datetime(h.last_time));
    dumpHdr("cumulative_sum", h.cumulative_sum);
    dumpHdr("cumulative_sum_sq", h.cumulative_sum_sq);
    dumpHdr("cumulative_count", h.cumulative_count);
    dumpHdr("last_cumulative_clear_time", h.last_cumulative_clear_time);
    dumpHdr("file_create_time", h.file_create_time);
    dumpHdr("flags", h.flags);
}

void dumpData(Header &h, int fd)
{
    size_t bucketsPerPage = h.page_size / sizeof(Bucket);
    Bucket *b = new Bucket[bucketsPerPage];

    int64_t numBuckets = h.page_count * bucketsPerPage;
    int64_t lastInterval = h.last_time;
    int64_t lastIndex = h.last_bucket % numBuckets;
    std::cout << "# Valid Bucket Time Sum SumSq Min Max Cnt" << std::endl;

    for (int64_t pix = 1; pix <= h.page_count; ++pix)
    {
        int64_t offset = pix * h.page_size;
        if (offset != lseek64(fd, offset, 0))
        {
            std::cerr << "file I/O error seeking to offset " << offset << std::endl;
            throw std::runtime_error("I/O error");
        }
        if ((ssize_t)(sizeof(Bucket) * bucketsPerPage) != ::read(fd, b, sizeof(Bucket) * bucketsPerPage))
        {
            std::cerr << "file I/O error reading bucket page size " << sizeof(Bucket) * bucketsPerPage << " at offset " << offset << std::endl;
            throw std::runtime_error("I/O error");
        }
        for (size_t bix = 0; bix < bucketsPerPage; ++bix)
        {
            int64_t bucketNumber = (pix - 1) * bucketsPerPage + bix;
            int64_t targetTime = lastInterval + (bucketNumber - lastIndex) * h.cfg_interval;
            bool valid = (b[bix].time() >= targetTime) && (b[bix].time() < (targetTime + h.cfg_interval));
            std::cout << std::right 
                << std::setw(2) << (valid ? 1 : 0) 
                << std::setw(8) << bucketNumber 
                << std::setw(14) << b[bix].time() 
                << std::setw(12) << b[bix].sum() 
                << std::setw(12) << b[bix].sumSq() 
                << std::setw(12) << b[bix].min() 
                << std::setw(12) << b[bix].max() 
                << std::setw(8) << b[bix].count() 
                << std::endl;
        }
    }

    delete[] b;
}


void fullDump(char const *fileName, bool ignoreError)
{
    int fd = ::open(fileName, O_RDONLY, 0444);
    if (fd < 0)
    {
error:
        if (fd >= 0)
        {
            ::close(fd);
        }
        throw std::runtime_error("cannot dump file");
    }
    int64_t sz = lseek64(fd, 0, 2);
    if (sz < 4096)
    {
        std::cerr << "file size " << sz << " is too short." << std::endl;
        goto error;
    }
    lseek64(fd, 0, 0);
    Header h;
    memset(&h, 0, sizeof(h));
    if (sizeof(h) != read(fd, &h, sizeof(h)))
    {
        std::cerr << "error reading header" << std::endl;
        goto error;
    }

    checkHeader(h, sz, ignoreError);
    dumpHeader(h);
    dumpData(h, fd);

    ::close(fd);
}

void csvDump(char const *fileName)
{
    StatFile sf(fileName, Stats(), mm);
    std::cout << "DATE,SUM,SUMSQ,MIN,MAX,COUNT,AVG,SDEV" << std::endl;
    for (int64_t i = sf.firstBucket(), n = sf.lastBucket(); true; ++i)
    {
        Bucket const &b = sf.bucket(i);
        if (b.time() > 0)
        {
            std::cout << "\"" << b.dateStr().c_str() << "\"," <<
                (double)b.sum() << "," << (double)b.sumSq() << "," <<
                (double)b.min() << "," << (double)b.max() << "," <<
                (long)b.count() << "," << b.avg() << "," <<
                b.sdev() << std::endl;
        }
        if (i == n)
        {
            break;
        }
    }
}


int main(int argc, char const *argv[])
{
    if (argc < 2 && argc > 4)
    {
usage:
        std::cerr << "usage: istatd_filedump [--full [--force]] filename" << std::endl;
        std::cerr << "istatd_filedump dumps the samples from an istatd file, and also " << std::endl <<
                     "calculates average/standard deviation for each sample as part of the dump. " << std::endl;
        return 1;
    }
    if (argc > 2)
    {
        if (strcmp(argv[1], "--full"))
        {
            goto usage;
        }
        full = true;
        //  eat one argument
        --argc;
        ++argv;
        if (argc > 2)
        {
            if (strcmp(argv[1], "--force"))
            {
                goto usage;
            }
            force = true;
            //  eat another argument
            --argc;
            ++argv;
        }
    }
    try
    {
        if (full)
        {
            fullDump(argv[1], force);
        }
        else
        {
            csvDump(argv[1]);
        }
    }
    catch (...)
    {
        std::cerr << "Cannot open: " << argv[1] << std::endl;
        return 1;
    }
    return 0;
}

