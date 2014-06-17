
#if !defined(istat_Atomic_h)
#define istat_Atomic_h

#include <inttypes.h>

namespace istat
{

int64_t atomic_add(int64_t volatile *vp, int64_t delta);
bool atomic_compare_exchange(void * volatile *vpp, void *oldValue, void *newValue);
void* atomic_compare_exchange_and_return(void * volatile *vpp, void *oldValue, void *newValue);
bool atomic_compare_exchange(int64_t volatile *vp, int64_t oldValue, int64_t newValue);

}

#endif

