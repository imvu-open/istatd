
#if !defined(daemon_Argument_h)
#define daemon_Argument_h

#include <string>
#include <boost/any.hpp>
#include <map>
#include <stdexcept>
#include <boost/lexical_cast.hpp>


template<typename T> struct argument_type;
template<> struct argument_type<char *> { typedef std::string type; };
template<> struct argument_type<char const *> { typedef std::string type; };
template<> struct argument_type<int> { typedef int type; };
template<> struct argument_type<bool> { typedef bool type; };
template<> struct argument_type<std::string> { typedef std::string type; };
template<> struct argument_type<float> { typedef float type; };

class ArgumentBase
{
public:
    ~ArgumentBase();
    std::string const &name() const { return name_; }
    boost::any const &value() const { return value_; }
    std::string const &help() const { return help_; }
    virtual std::string str() const = 0;
    virtual int set_from_argv(char const *argv[], int iterator) = 0;
    virtual void set(std::string const &value) = 0;

    typedef std::map<std::string, ArgumentBase *>::const_iterator iterator;
    static inline iterator begin() { return arguments().begin(); }
    static inline iterator end() { return arguments().end(); }
    static ArgumentBase *find(std::string const &name);
    //  Set to 'true' to print an error in parseArg()
    static bool verbose_;

    //  Start with iterator=1, and keep calling parseArg until it returns false.
    //  argv[iterator] is then either 0, or a filename or other unknown argument.
    //  Long argument names are expected to start with --, and '--' will make the 
    //  parser increment iterator once and return false.
    //  Return true means that an argument was recognized and iterator incremented 
    //  past the argument.
    static bool parseArg(int argc, char const *argv[], int &iterator);
    //  parseData parses a config file, that you have read into the given string. 
    //  The format is simply <argname> <argvalue>, line by line.
    //  Lines starting with # are ignored, as are blank lines.
    static bool parseData(std::string const &data, std::string &oErr);
protected:
    template<typename T>
    ArgumentBase(char const *name, T dflt, char const *help) :
        name_(name),
        value_((typename argument_type<T>::type)dflt),
        help_(help)
    {
        if (arguments().find(name_) != arguments().end())
        {
            throw std::logic_error("multiple definitions of argument " + name_);
        }
        arguments()[name_] = this;
    }
    std::string name_;
    boost::any value_;
    std::string help_;
    static std::map<std::string, ArgumentBase *> &arguments();
    virtual bool hasParameters() { return true; }
};

template<typename T>
class GenericArgument : public ArgumentBase
{
public:
    typedef typename argument_type<T>::type type;
    GenericArgument(char const *name, T dflt, char const *help) :
        ArgumentBase(name, dflt, help)
    {
    }
    std::string str() const { return boost::lexical_cast<std::string>(boost::any_cast<type>(value_)); }
    int set_from_argv(char const *argv[], int iterator) {
        ++iterator;
        set(argv[iterator]);
        return iterator;
    }
    void set(std::string const &value) { value_ = boost::lexical_cast<type>(value); }
    type const get() { return boost::any_cast<type>(value_); }
};

template<typename T>
class Argument : public GenericArgument<T>
{
public:
    Argument(char const *name, T dflt, char const *help) :
        GenericArgument<T>(name, dflt, help) { }
};

template<>
class Argument<bool> : public GenericArgument<bool>
{
public:
    Argument(char const *name, bool dflt, char const *help) :
        GenericArgument<bool>(name, dflt, help) { }
    int set_from_argv(char const *argv[], int iterator) {
        value_ = true;
        return iterator;
    }
    bool hasParameters() { return false; }
};
#endif  //  daemon_Argument_h

