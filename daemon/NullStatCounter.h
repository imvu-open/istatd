#if !defined(daemon_NullStatCounter_h)
#define daemon_NullStatCounter_h

#include "IStatCounter.h"

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio/strand.hpp>

#include <string>
#include <vector>

class NullStatCounter : public IStatCounter, public boost::noncopyable
{
	std::string name_;
	
public:
    NullStatCounter(const std::string &name);
	virtual ~NullStatCounter();

    virtual bool isCollated() const;

    virtual void record(time_t time, 
						double value, 
						double valueSq, 
						double min, 
						double max, 
						size_t cnt);

    virtual void flush(boost::shared_ptr<IStatStore> const &store);

    virtual void forceFlush(boost::shared_ptr<IStatStore> const &store);

    virtual void maybeShiftCollated(time_t t);

    virtual void select(time_t start, 
						time_t end, 
						std::vector<istat::Bucket> &oBuckets, 
                        time_t &normalized_start,
						time_t &normalized_end, 
						time_t &interval, 
                        size_t max_samples);
};

#endif  //  daemon_NullStatCounter_h
