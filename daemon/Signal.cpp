
#include "Signal.h"

template <> void disconnect_all_slots(boost::signals2::signal<void (size_t)> & signal) {
    signal.disconnect_all_slots();
    signal(0);
}

