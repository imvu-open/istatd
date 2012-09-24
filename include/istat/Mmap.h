#if !defined(istat_Mmap_h)
#define istat_Mmap_h

#include <cstddef>
#include <boost/cstdint.hpp>

namespace istat
{
    class Mmap
    {
    public:
        virtual int open(char const *name, int flags) = 0;
        virtual int close(int fd) = 0;
        virtual ssize_t read(int fd, void *ptr, ssize_t amt) = 0;
        virtual ssize_t write(int fd, void *ptr, ssize_t amt) = 0;
        virtual ptrdiff_t seek(int fd, ptrdiff_t offset, int whence) = 0;
        virtual ssize_t tell(int fd) = 0;
        virtual int truncate(int fd, ssize_t size) = 0;
        virtual void *map(int fd, int64_t offset, size_t size, bool writable) = 0;
        virtual bool unmap(void const *ptr, size_t size) = 0;
        virtual bool flush(void const *ptr, size_t size, bool immediate) = 0;
        virtual int64_t availableSpace(char const *path) = 0;
        virtual void dispose() = 0;
        virtual void counters(int64_t *oMaps, int64_t *oUnmaps, int64_t *oOpens, int64_t *oCloses) = 0;
    protected:
        virtual ~Mmap() {}
    };

    Mmap *NewMmap();
}


#endif  //  istat_Mmap_h

