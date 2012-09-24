
#if !defined(daemon_LoopbackCounter_h)
#define daemon_LoopbackCounter_h

#include <string>
#include <boost/asio/io_service.hpp>
#include <boost/asio/deadline_timer.hpp>

/*
 * Use istatd to monitor itself. All declared counters will be put in a 
 * "istatd" root level group.
 *
 * usage:
 *
 * LoopbackCounter gSomeEvent("some.event", TypeEvent);
 * ...
 * gSomeEvent++;    //  or
 * ++gSomeEvent;    //  or
 * gSomeEvent.value(); // or
 * 
 * This will create a coalesced counter that gets recorded through the 
 * normal "coalesce" path.
 *
 * LoopbackCounter gSomeGauge("some.gauge", TypeGauge);
 *
 * gSomeEvent.value(3218);
 *
 * main.cpp will set up the recording function by calling LoopbackCounter::setup()
 *
 */


class IStatStore;

enum CounterType
{
    TypeGauge = 0,              /* standard gauge reported directly as a number */
    TypeEvent = 1,              /* counts events in the bucket */
    TypeCounted = 2             /* internal counter which gets ++ / -- events, similar to Event but NOT reset to zero at the end of each bucket.  Relies on the -- operator to avoid monotonically increasing numbers */
};

struct LoopbackCounterRecord
{
    LoopbackCounterRecord();
    //  LoopbackCounterRecords for aggregate counters never die
    //  for threaded efficiency reasons (avoiding expensive locks)
    std::string name_;
    bool isAggregate_;
    CounterType type_;
    int64_t volatile aggregate_;
    LoopbackCounterRecord *next_;
    static LoopbackCounterRecord *first_;
};

class LoopbackCounter
{
public:
    LoopbackCounter(char const *name, CounterType type);
    ~LoopbackCounter();
    LoopbackCounter(LoopbackCounter const &o);
    LoopbackCounter &operator=(LoopbackCounter const &o);
    void value(double value = 1.0f);
    LoopbackCounter &operator++();
    LoopbackCounter &operator++(int);
    LoopbackCounter &operator--();
    LoopbackCounter &operator--(int);
    int64_t getAggregate() const;
    static void setup(IStatStore *ss, boost::asio::io_service &svc);
    static void forceUpdates();
private:
    friend class LoopbackCounterRecord;
    LoopbackCounterRecord *me_;
    static IStatStore *ss_;
    static boost::asio::io_service *svc_;
    static boost::asio::deadline_timer *timer_;
    static void reschedule();
    static void updateAggregates(boost::system::error_code const &err);
};

#endif  //  daemon_LoopbackCounter_h
