
#include "Debug.h"


DebugOption *DebugOption::first_;

DebugOption::DebugOption(char const *name) :
    name_(name),
    enabled_(false)
{
    next_ = first_;
    first_ = this;
}

DebugOption::~DebugOption()
{
    //  Don't leave anything dangling! Even if I'm not first,
    //  once a DebugOption is gone, you can't find anything.
    first_ = 0;
}

DebugOption::operator bool() const
{
    return enabled_;
}

bool DebugOption::enabled() const
{
    return enabled_;
}

bool DebugOption::operator!() const
{
    return !enabled_;
}

std::string const &DebugOption::name() const
{
    return name_;
}

void DebugOption::set(bool value)
{
    enabled_ = value;
}

DebugOption const *DebugOption::first()
{
    return first_;
}

DebugOption const *DebugOption::next(DebugOption const *opt)
{
    return opt ? opt->next_ : 0;
}

DebugOption const *DebugOption::find(std::string const &name)
{
    for (DebugOption *opt = first_; opt; opt = opt->next_)
    {
        if (opt->name_ == name)
        {
            return opt;
        }
    }
    return 0;
}

void DebugOption::setOptions(std::set<std::string> const &values)
{
    for (DebugOption *opt = first_; opt; opt = opt->next_)
    {
        opt->set(values.find(opt->name()) != values.end());
    }
}


