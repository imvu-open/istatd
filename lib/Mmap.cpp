
#include <istat/Mmap.h>
#include <sys/fcntl.h>
#include <stdexcept>
#include <iostream>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/statvfs.h>


/* Use an alternative version of the Mmap interface, 
   reading/writing synchronous files instead.
   */
#define BLOCKING 1


namespace istat
{

    std::string dirname(std::string const &path)
    {
        size_t pos = path.find_last_of('/');
        if (pos == std::string::npos)
        {
            return std::string("./");
        }
        return path.substr(0, pos + 1);
    }

    struct MmapInfo {
        int64_t offset;
        size_t size;
        int fd;
    };

    class MmapImpl : public Mmap
    {
    public:
        MmapImpl() :
            nMaps_(0),
            nUnmaps_(0),
            nOpens_(0),
            nCloses_(0),
            hint_(0)
        {
        }
        int64_t nMaps_;
        int64_t nUnmaps_;
        int64_t nOpens_;
        int64_t nCloses_;
        void *hint_;
        AllocationStrategy allocationStrategy_;
        virtual int open(char const *name, int flags)
        {
            int i = ::open(name, flags, 0664);
            if (i >= 0)
            {
                __sync_add_and_fetch(&nOpens_, (int64_t)1);
                i |= 0x40000000;
            }
            return i;
        }
        virtual int close(int fd)
        {
            int i = verify(fd);
            __sync_add_and_fetch(&nCloses_, (int64_t)1);
            return ::close(i);
        }
        virtual ssize_t read(int fd, void *ptr, ssize_t amt)
        {
            int i = verify(fd);
            return ::read(i, ptr, amt);
        }
        virtual ssize_t write(int fd, void *ptr, ssize_t amt)
        {
            int i = verify(fd);
            return ::write(i, ptr, amt);
        }
        virtual ptrdiff_t seek(int fd, ptrdiff_t offset, int whence)
        {
            int i = verify(fd);
            return ::lseek64(i, offset, whence);
        }
        virtual ssize_t tell(int fd)
        {
            int i = verify(fd);
            return ::lseek64(i, 0, SEEK_CUR);
        }
        virtual int truncate(int fd, ssize_t size)
        {
            int i = verify(fd);
            int ret = ::ftruncate(i, size);
            if (ret >= 0 && size > 0)
            {
                if (allocationStrategy_ == allocateAll)
                {
                    ret = ::posix_fallocate(i, 0, size);
                }
            }
            return ret;
        }


        // the special bit (0x40000000) is to keep folks from passing in regular file descriptors
        int verify(int fd)
        {
            if (fd < 0 || !(fd & 0x40000000))
            {
                throw std::runtime_error("Bad file descriptor passed to Mmap");
            }
            return fd & ~0x40000000;
        }

        virtual void *map(int fd, int64_t offset, size_t size, bool writable)
        {
#if BLOCKING
            verify(fd);
            MmapInfo *ptr = (MmapInfo *)malloc(sizeof(MmapInfo) + size);
            ptr->offset = offset;
            ptr->size = size;
            ptr->fd = fd;
            this->seek(fd, offset, SEEK_SET);
            this->read(fd, ptr + 1, size);
            __sync_add_and_fetch(&nMaps_, (int64_t)1);
            return ptr + 1;
#else
            int i = verify(fd);
            void *hint = hint_;
            void *ptr = mmap(hint, size, PROT_READ | (writable ? PROT_WRITE : 0),
                MAP_SHARED, i, offset);
            if (ptr != 0 && ptr != (void *)MAP_FAILED)
            {
                //  add a little bit of padding to make sure we die if we write out of bounds
                hint = __sync_val_compare_and_swap(&hint_, hint, (char *)ptr + offset + 8192);
                // atomic bus operation in gcc to update the counter safe across threads
                __sync_add_and_fetch(&nMaps_, (int64_t)1);
                // tell the kernel we might need this "soon" to prioritize io
                madvise(ptr, offset, MADV_WILLNEED);
            }
            return (ptr == MAP_FAILED) ? 0 : ptr;
#endif
        }
        virtual bool unmap(void const *ptr, size_t size)
        {
#if BLOCKING
            MmapInfo *mmi = (MmapInfo *)ptr - 1;
            verify(mmi->fd);
            this->seek(mmi->fd, mmi->offset, SEEK_SET);
            this->write(mmi->fd, mmi + 1, mmi->size);
            free(mmi);
            __sync_add_and_fetch(&nUnmaps_, (int64_t)1);
            return true;
#else
            int res = munmap(const_cast<void *>(ptr), size);
            if (res >= 0)
            {
                __sync_add_and_fetch(&nUnmaps_, (int64_t)1);
            }
            return res >= 0;
#endif
        }
        virtual bool flush(void const *ptr, size_t size, bool immediate)
        {
#if BLOCKING
            MmapInfo *mmi = (MmapInfo *)ptr - 1;
            verify(mmi->fd);
            this->seek(mmi->fd, mmi->offset, SEEK_SET);
            this->write(mmi->fd, mmi + 1, mmi->size);
            return true;
#else
            // int flags = (immediate ? MS_SYNC : MS_ASYNC);
            int res = msync(const_cast<void *>(ptr), size, MS_SYNC);
            return res >= 0;
#endif
        }
        virtual int64_t availableSpace(char const *path)
        {
            struct statvfs sv;
            std::string dn(dirname(path));
            if (statvfs(dn.c_str(), &sv) < 0)
            {
                //  error? no space!
                return -1;
            }
            if (sv.f_favail < 10)
            {
                //  no inodes? no space!
                return 0;
            }
            return (int64_t)sv.f_bsize * (int64_t)sv.f_bavail;
        }
        virtual void dispose()
        {
            delete this;
        }
        virtual void counters(int64_t *oMaps, int64_t *oUnmaps, int64_t *oOpens, int64_t *oCloses)
        {
            *oMaps = nMaps_;
            *oUnmaps = nUnmaps_;
            *oOpens = nOpens_;
            *oCloses = nCloses_;
        }

        virtual void setAllocationStrategy(AllocationStrategy as)
        {
            allocationStrategy_ = as;
        }
    };

    Mmap *NewMmap()
    {
        return new MmapImpl();
    }
}

