
#if !defined(daemon_Signal_h)
#define daemon_Signal_h

#include <boost/signals2.hpp>

/*
    boost::signals2::signal<...>::disconnect_all_slots() does not destroy the disconnected
    slots. It only mark them as invalid, which prevents code relying on reference counting of
    the slots from working correctly. Invoking the signal after disconnecting slots will
    purge disconnected slots.
*/
template <typename signal_type> void disconnect_all_slots(signal_type & signal) {
    signal.disconnect_all_slots();
    signal();
}
template <> void disconnect_all_slots(boost::signals2::signal<void (size_t)> & signal);

#endif // daemon_Signal_h

