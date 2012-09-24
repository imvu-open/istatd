
#if !defined(daemon_Debug_h)
#define daemon_Debug_h

#include <string>
#include <set>

class DebugOption
{
public:
    DebugOption(char const *name);
    ~DebugOption();
    operator bool() const;
    bool enabled() const;
    bool operator!() const;
    std::string const &name() const;
    void set(bool value);
    static DebugOption const *first();
    static DebugOption const *next(DebugOption const *);
    static DebugOption const *find(std::string const &name);
    static void setOptions(std::set<std::string> const &values);
private:
    std::string name_;
    bool enabled_;
    DebugOption *next_;
    static DebugOption *first_;
};

#endif  //  daemon_Debug_h

