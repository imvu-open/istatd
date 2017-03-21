#include <istat/test.h>
#include <istat/istattime.h>
#include <istat/Mmap.h>
#include "../daemon/StatCounterFactory.h"
#include "../daemon/StatStore.h"
#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>


using namespace istat;

RetentionPolicy rp("10s:1d");
RetentionPolicy xrp("");

class FakeProtectedDiskMmap : public Mmap
{
public:
    FakeProtectedDiskMmap(int64_t freeSpace)
        : freeSpace_(freeSpace)
    {
    }

    virtual int open(char const *name, int flags)
    {
        return -1;
    }

    virtual int close(int fd)
    {
        return -1;
    }

    virtual ssize_t read(int fd, void *ptr, ssize_t amt)
    {
        return -1;
    }

    virtual ssize_t write(int fd, void *ptr, ssize_t amt)
    {
        return -1;
    }

    virtual ptrdiff_t seek(int fd, ptrdiff_t offset, int whence)
    {
        return -1;
    }

    virtual ssize_t tell(int fd)
    {
        return -1;
    }

    virtual int truncate(int fd, ssize_t size)
    {
        return -1;
    }

    virtual void *map(int fd, int64_t offset, size_t size, bool writable)
    {
        return 0;
    }

    virtual bool unmap(void const *ptr, size_t size)
    {
        return false;
    }

    virtual bool flush(void const *ptr, size_t size, bool immediate)
    {
        return false;
    }

    virtual int64_t availableSpace(char const *path)
    {
        return freeSpace_;
    }

    virtual void dispose()
    {
    }

    virtual void counters(int64_t *oMaps, int64_t *oUnmaps, int64_t *oOpens, int64_t *oCloses)
    {
    }
private:
    int64_t freeSpace_;
};

void run_tests(void)
{
    Mmap *mm;

    mm = NewMmap();
    {
        boost::asio::io_service service;

        std::string storepath("/tmp/test/statstore");
        boost::filesystem::remove_all(storepath);
        boost::filesystem::create_directories(storepath);
        boost::shared_ptr<IStatCounterFactory> statCounterFactory = boost::make_shared<StatCounterFactory>(storepath, mm, boost::ref(rp));
        StatStore store(storepath, getuid(), service, statCounterFactory, mm);

        store.record("taco", 42.42);
        std::list<std::pair<std::string, CounterResponse> > oList;
        store.listMatchingCounters("bbq", oList);
        assert_equal(0, oList.size());
        store.listMatchingCounters("taco is delicious!", oList);
        assert_equal(0, oList.size());
        store.listMatchingCounters("taco", oList);
        assert_equal(1, oList.size());
        store.record("taco.bell", 42.42);
        store.record("*taco.cheese", 42.42);
        std::list<std::pair<std::string, CounterResponse> > oList2;
        store.listMatchingCounters("taco*", oList2);
        assert_equal(3, oList2.size());
        std::list<std::pair<std::string, CounterResponse> >::iterator ptr = oList2.begin();
        assert_equal("taco.bell", (*ptr).first);
        assert_equal(true, (*ptr).second.isLeaf);
        assert_equal(CounterResponse::DisplayTypeGauge, (*ptr).second.counterType);
        std::advance(ptr, 1);
        assert_equal("taco.cheese", (*ptr).first);
        assert_equal(true, (*ptr).second.isLeaf);
        assert_equal(CounterResponse::DisplayTypeEvent, (*ptr).second.counterType);
        std::advance(ptr, 1);
        assert_equal("taco", (*ptr).first);
        assert_equal(false, (*ptr).second.isLeaf);
        assert_equal(CounterResponse::DisplayTypeAggregate, (*ptr).second.counterType);

        store.record("ortz.bell", 42.42);
        std::list<std::pair<std::string, CounterResponse> > oList3;
        store.listMatchingCounters("ortz*", oList3);
        assert_equal(2, oList3.size());
        std::list<std::pair<std::string, CounterResponse> >::iterator ptr2 = oList3.begin();
        assert_equal("ortz.bell", (*ptr2).first);
        assert_equal(true, (*ptr2).second.isLeaf);
        assert_equal(CounterResponse::DisplayTypeGauge, (*ptr2).second.counterType);
        std::advance(ptr2, 1);
        assert_equal("ortz", (*ptr2).first);
        assert_equal(false, (*ptr2).second.isLeaf);
        assert_equal(CounterResponse::DisplayTypeAggregate, (*ptr2).second.counterType);
    }
    mm->dispose();

    // Ensure full disk does not have available space.
    mm = new FakeProtectedDiskMmap(0);
    {
        boost::asio::io_service service;
        std::string storepath("/tmp/test/statstore");
        boost::filesystem::remove_all(storepath);
        boost::filesystem::create_directories(storepath);
        boost::shared_ptr<IStatCounterFactory> statCounterFactory = boost::make_shared<StatCounterFactory>(storepath, mm, boost::ref(rp));
        StatStore store(storepath, getuid(), service, statCounterFactory, mm);

        assert_equal(store.hasAvailableSpace(), false);
    }
    mm->dispose();

    // Ensure disk with 1GB free still has available space.
    mm = new FakeProtectedDiskMmap(1024L * 1024L * 1024L);
    {
        boost::asio::io_service service;
        std::string storepath("/tmp/test/statstore");
        boost::filesystem::remove_all(storepath);
        boost::filesystem::create_directories(storepath);
        boost::shared_ptr<IStatCounterFactory> statCounterFactory = boost::make_shared<StatCounterFactory>(storepath, mm, boost::ref(rp));
        StatStore store(storepath, getuid(), service, statCounterFactory, mm);

        assert_equal(store.hasAvailableSpace(), true);
    }
    mm->dispose();

    mm = NewMmap();
    {
        boost::asio::io_service service;

        std::string storepath("/tmp/test/statstore");
        boost::filesystem::remove_all(storepath);
        boost::filesystem::create_directories(storepath);
        boost::shared_ptr<IStatCounterFactory> statCounterFactory = boost::make_shared<StatCounterFactory>(storepath, mm, boost::ref(rp));
        StatStore store(storepath, getuid(), service, statCounterFactory, mm, -1, 60000, false);

        // Still end up with the aggregates in the key list as this is divorced from creating the file on disk
        store.record("ortz.bell", 42.42);
        std::list<std::pair<std::string, CounterResponse> > oList3;
        store.listMatchingCounters("ortz*", oList3);
        assert_equal(2, oList3.size());
        std::list<std::pair<std::string, CounterResponse> >::iterator ptr2 = oList3.begin();
        assert_equal("ortz.bell", (*ptr2).first);
        assert_equal(true, (*ptr2).second.isLeaf);
        assert_equal(CounterResponse::DisplayTypeGauge, (*ptr2).second.counterType);
    }
    mm->dispose();
}


void func()
{
    run_tests();
}

int main(int argc, char const *argv[])
{
    return istat::test(func, argc, argv);
}
