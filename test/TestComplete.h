
#include "../daemon/IComplete.h"

class complete : public IComplete
{
public:
    complete() : complete_(false) {}
    bool complete_;
    operator complete *() {
        complete_ = false;
        return this;
    }
    void on_complete() {
        complete_ = true;
    }
};

