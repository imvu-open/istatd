
#include <istat/Atomic.h>
#if defined(_WIN32)
#include <windows.h>
#endif


namespace istat
{

int64_t atomic_add(int64_t volatile *vp, int64_t delta)
{
#if defined(_WIN32)
    return (int64_t)::InterlockedAdd64((LONGLONG *)vp, (LONGLONG)delta);
#else
    return __sync_add_and_fetch(vp, delta);
#endif
}

bool atomic_compare_exchange(void * volatile * vpp, void *oldValue, void *newValue)
{
#if defined(_WIN32)
#error "implement me"
#else
    return __sync_bool_compare_and_swap(vpp, oldValue, newValue);
#endif
}

bool atomic_compare_exchange(int64_t volatile *vp, int64_t oldValue, int64_t newValue)
{
#if defined(_WIN32)
#error "implement me"
#else
    return __sync_bool_compare_and_swap(vp, oldValue, newValue);
#endif
}

void* atomic_compare_exchange_and_return(void * volatile * vpp, void *oldValue, void *newValue)
{
#if defined(_WIN32)
#error "implement me"
#else
    return __sync_val_compare_and_swap(vpp, oldValue, newValue);
#endif
}
}
