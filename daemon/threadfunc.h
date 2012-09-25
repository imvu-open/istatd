
#if !defined(daemon_threadfunc_h)
#define daemon_threadfunc_h

#include <boost/thread/locks.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/asio.hpp>

typedef boost::lock_guard<boost::recursive_mutex> grab;
typedef boost::recursive_mutex lock;


#endif  //  daemon_threadfunc_h
