
#include "Argument.h"
#include <boost/lexical_cast.hpp>
#include <sstream>
#include <iostream>
#include <ctype.h>

bool ArgumentBase::verbose_;

std::map<std::string, ArgumentBase *> &ArgumentBase::arguments()
{
    static std::map<std::string, ArgumentBase *> it;
    return it;
}

ArgumentBase *ArgumentBase::find(std::string const &name)
{
    std::map<std::string, ArgumentBase *>::iterator ptr(arguments().find(name));
    if (ptr == arguments().end())
    {
        return 0;
    }
    return (*ptr).second;
}

ArgumentBase::~ArgumentBase()
{
    arguments().erase(arguments().find(name_));
}

bool ArgumentBase::parseArg(int argc, char const *argv[], int &iterator)
{
    if (iterator >= argc || iterator < 1)
    {
        return false;
    }
    std::string key(argv[iterator]);
    if (key[0] != '-' || key[1] != '-')
    {
        if (verbose_) std::cerr << "Not a long format option: " << key << std::endl;
        return false;
    }
    if (key.length() == 2)
    {
        if (verbose_) std::cerr << "End of options: " << key << std::endl;
        ++iterator;
        return false;
    }
    key = key.substr(2);
    ArgumentBase *arg = ArgumentBase::find(key);
    if (!arg)
    {
        if (verbose_) std::cerr << "Option not found: --" << key << std::endl;
        return false;
    }
    if (iterator > argc-1)
    {
        if (verbose_) std::cerr << "Option missing an argument: --" << key << std::endl;
        //  missing argument!
        return false;
    }
    try
    {
        iterator = arg->set_from_argv(argv, iterator);
    }
    catch (boost::bad_lexical_cast const &)
    {
        if (verbose_) std::cerr << "Option argument is bad: " << argv[iterator] << std::endl;
        return false;
    }
    iterator += 1;
    return true;
}

bool ArgumentBase::parseData(std::string const &data, std::string &oErr)
{
    size_t pos = 0;
    size_t end = 0;
    size_t len = data.length();
    int lineNo = 1;

    while (pos < len)
    {
        end = data.find_first_of('\n', pos);
        while (pos < len && pos < end && isspace(data[pos]))
        {
            ++pos;
        }
        size_t tend = std::min(end, len);
        while (tend > pos && isspace(data[tend-1]))
        {
            --tend;
        }
        std::string line(data.substr(pos, tend - pos));
        pos = std::min(end, len)+1;
        if (!line.empty() && line[0] != '#')
        {
            std::string value;
            size_t split = line.find_first_of(' ');
            std::string key(line.substr(0, split));
            tend = line.length();
            if (split == std::string::npos)
            {
                if (line.size() > 2)
                {
                    ArgumentBase *ab = ArgumentBase::find(line.substr(2));
                    if (ab != NULL && !ab->hasParameters())
                    {
                        goto no_params;
                    }
                }
                oErr = "Error on line " + boost::lexical_cast<std::string>(lineNo) + ": '"
                    + line + "' should have an argument.";
                return false;
            }
            while (split+1 < tend && isspace(line[split+1]))
            {
                split += 1;
            }
            while (tend > split+1 && isspace(line[tend-1]))
            {
                --tend;
            }
            value = line.substr(split+1, tend-(split+1));
            if (value.length() >= 2 && value[0] == '"' && value[value.length()-1] == '"')
            {
                value = value.substr(1, value.length()-2);
            }
no_params:
            char const *data[5] = { "", key.c_str(), value.c_str(), "end of line", 0 };
            int ix = 1;
            if (!parseArg(3, data, ix))
            {
                oErr = "Error on line " + boost::lexical_cast<std::string>(lineNo) + ": "
                    + "near " + data[ix];
                return false;
            }
        }
        ++lineNo;
    }
    return true;
}

