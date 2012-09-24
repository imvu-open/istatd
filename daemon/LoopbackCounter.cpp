#include "LoopbackCounter.h"
#include "IStatStore.h"
#include "Logs.h"
#include "Debug.h"

#include <istat/Atomic.h>
#include <stdexcept>


LoopbackCounterRecord *LoopbackCounterRecord::first_;

IStatStore *LoopbackCounter::ss_;
boost::asio::io_service *LoopbackCounter::svc_;
boost::asio::deadline_timer *LoopbackCounter::timer_;

DebugOption debugLoopback("loopback");

LoopbackCounterRecord::LoopbackCounterRecord()
{
    isAggregate_ = false;
    aggregate_ = 0;
    next_ = NULL;
}

LoopbackCounter::LoopbackCounter(char const *name, CounterType type)
{
    if (debugLoopback.enabled())
    {
        LogNotice << "loopback" << name;
    }
    me_ = new LoopbackCounterRecord();
    if (TypeEvent == type)
    {
        me_->name_ = "*istatd.";
        me_->isAggregate_ = true;
    }
    else if (TypeCounted == type)
    {
        me_->name_ = "istatd.";
        me_->isAggregate_ = true;
    }
    else
    {
        me_->name_ = "istatd.";
        me_->isAggregate_ = false;
    }
    me_->name_ += name;
    me_->type_ = type;
    if ((TypeEvent == type) || (TypeCounted == type))
    {
        me_->next_ = LoopbackCounterRecord::first_;
        LoopbackCounterRecord::first_ = me_;
    }
}

LoopbackCounter::~LoopbackCounter()
{
    if (!me_->isAggregate_)
    {
        assert(!me_->next_);
        delete me_;
    }
}

LoopbackCounter::LoopbackCounter(LoopbackCounter const &o)
{
    if (o.me_->isAggregate_)
    {
        me_ = o.me_;
    }
    else
    {
        me_ = new LoopbackCounterRecord(*o.me_);
    }
}

LoopbackCounter &LoopbackCounter::operator=(LoopbackCounter const &o)
{
    if (&o != this)
    {
        this->~LoopbackCounter();
        new (this) LoopbackCounter(o);
    }
    return *this;
}

void LoopbackCounter::value(double value)
{
    if (me_->isAggregate_)
    {
        istat::atomic_add(&me_->aggregate_, (int64_t)value);
    }
    else if (ss_)
    {
        ss_->record(me_->name_, value);
    }
}

void LoopbackCounter::setup(IStatStore *ss, boost::asio::io_service &svc)
{
    if (debugLoopback.enabled())
    {
        LogNotice << "loopback setup";
    }
    LogDebug << "LoopbackCounter::setup()";
    if (svc_)
    {
        throw std::runtime_error("Cannot re-initialize LoopbackCounter");
    }
    ss_ = ss;
    svc_ = &svc;
    timer_ = new boost::asio::deadline_timer(*svc_);
    reschedule();
}

void LoopbackCounter::forceUpdates()
{
    debugLoopback.enabled() ? (LogNotice << "loopback forceUpdates()") : (LogSpam << "LoopbackCounter::forceUpdates()");
    if (!LoopbackCounter::ss_)
    {
        return;
    }
    //  I rely on internal counters not ever being deleted
    for (LoopbackCounterRecord *r = LoopbackCounterRecord::first_; r; r = r->next_)
    {
        int64_t v = r->aggregate_;
        if (v)
        {
            //  if there actually was data, update it
            if (TypeEvent == r->type_) {
                __sync_add_and_fetch(&r->aggregate_, -v);
            }
            LoopbackCounter::ss_->record(r->name_, (int64_t)v);
        }
    }
}

void LoopbackCounter::updateAggregates(boost::system::error_code const &err)
{
    if (!!err)
    {
        LogError << "Error in LoopbackCounter::updateAggregates():" << err;
        return;
    }
    forceUpdates();
    reschedule();
}

void LoopbackCounter::reschedule()
{
    timer_->expires_from_now(boost::posix_time::milliseconds(500));
    timer_->async_wait(&LoopbackCounter::updateAggregates);
}

LoopbackCounter &LoopbackCounter::operator++()
{
    assert(me_->isAggregate_);
    value(1);
    return *this;
}

LoopbackCounter &LoopbackCounter::operator++(int)
{
    assert(me_->isAggregate_);
    value(1);
    return *this;
}

LoopbackCounter &LoopbackCounter::operator--()
{
    assert(me_->isAggregate_);
    value(-1);
    return *this;
}

LoopbackCounter &LoopbackCounter::operator--(int)
{
    assert(me_->isAggregate_);
    value(-1);
    return *this;
}

int64_t LoopbackCounter::getAggregate() const
{
    assert(me_->isAggregate_);
    return me_->aggregate_;
}
