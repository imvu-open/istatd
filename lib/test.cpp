
#include "istat/test.h"
#include <iostream>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <list>
#include <string>
#include <string.h>
#include <sstream>
#include <iomanip>
#include <boost/filesystem.hpp>
#include <boost/version.hpp>

#define TEST_FAIL_SHOULD_THROW 0

namespace istat
{
    char const *lastFile = 0;
    int lastLine = -1;

    std::string quote(std::string const &in)
    {
        std::stringstream out;
        for (std::string::const_iterator ptr(in.begin()), end(in.end());
            ptr != end; ++ptr)
        {
            switch (*ptr)
            {
            case 9:
                out << "\\t";
                break;
            case 10:
                out << "\\n";
                break;
            case 13:
                out << "\\r";
                break;
            default:
                if (*ptr < 32 || *ptr > 127)
                {
                    out << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (unsigned char)*ptr
                        << std::setw(0) << std::setfill(' ');
                }
                else
                {
                    out << *ptr;
                }
                break;
            }
        }
        return out.str();
    }

    void test_fail(std::string const& message)
    {
        std::cerr << message << std::endl;
#if TEST_FAIL_SHOULD_THROW
        throw std::runtime_error(message);
#else
        exit(1);
#endif
    }

    void test_init_path(std::string const &path)
    {
        boost::filesystem::remove_all(path);
        boost::filesystem::create_directories(path);
        if(chdir(path.c_str()) < 0)
        {
            std::ostringstream oss;
            oss << "could not chdir(\"" << path << "\")";
            test_fail(oss.str());
        }
        char filename[1024];
        char const *result = getcwd(filename, sizeof(filename) / sizeof(filename[0]));
        if(!result)
        {
            std::ostringstream oss;
            oss << "getcwd(...) failed: "
#if BOOST_VERSION >= 106700
                << boost::system::error_code(errno, boost::system::system_category()).message();
#else
                << boost::system::error_code(errno, boost::system::get_system_category()).message();
#endif
            test_fail(oss.str());
        }
    }

    int test(void (*func)(), int argc, char const *argv[])
    {
        test_init_path("/tmp/test");

        try
        {
            func();
        }
        catch (std::exception const &e)
        {
            std::ostringstream oss;
            if (lastFile != 0 && lastLine > 0)
            {
                oss << lastFile << ":" << lastLine << ": last line tested: ";
            }
            oss << "exception thrown in test: " << e.what();
            test_fail(oss.str());
        }
        catch (...)
        {
            std::ostringstream oss;
            if (lastFile != 0 && lastLine > 0)
            {
                oss << lastFile << ":" << lastLine << ": last line tested: ";
            }
            oss << "unknown exception thrown in test" << std::endl;
            test_fail(oss.str());
        }
        return 0;
    }

    void test_asserttrue(char const *file, int line, char const *expr, bool b)
    {
        lastFile = file;
        lastLine = line;
        if (b)
        {
            return;
        }

        std::ostringstream oss;
        oss << file << ":" << line << ": must be true: " << expr;
        test_fail(oss.str());
    }

    void test_assertfalse(char const *file, int line, char const *expr, bool b)
    {
        lastFile = file;
        lastLine = line;
        if (!b)
        {
            return;
        }

        std::ostringstream oss;
        oss << file << ":" << line << ": must be false: " << expr;
        test_fail(oss.str());
    }

    void test_assertequal(char const *file, int line, char const *ea, char const *eb, char const *fail)
    {
        lastFile = file;
        lastLine = line;

        std::ostringstream oss;
        oss << file << ":" << line << ": must be equal: " << quote(ea) << " == " << quote(eb) << "; " << fail;
        test_fail(oss.str());
    }

    void test_assertnotequal(char const *file, int line, char const *ea, char const *eb, char const *fail)
    {
        lastFile = file;
        lastLine = line;

        std::ostringstream oss;
        oss << file << ":" << line << ": must not be equal: " << quote(ea) << " != " << quote(eb) << "; " << fail;
        test_fail(oss.str());
    }

    void test_assertcontains(char const *file, int line, std::string const &haystack, std::string const &needle, std::string const &fail)
    {
        lastFile = file;
        lastLine = line;
        if (haystack.find(needle) != std::string::npos)
        {
            return;
        }

        std::ostringstream oss;
        oss << file << ":" << line << ": " << fail << "; " << haystack << " ~= " << needle;
        test_fail(oss.str());
    }

    void test_assert_does_not_contain(char const *file, int line, std::string const &haystack, std::string const &needle, std::string const &fail)
    {
        lastFile = file;
        lastLine = line;
        if (haystack.find(needle) == std::string::npos)
        {
            return;
        }

        std::ostringstream oss;
        oss << file << ":" << line << ": " << fail << "; " << haystack << " !~= " << needle;
        test_fail(oss.str());
    }

    void test_assertless(char const *file, int line, char const *xa, char const *xb, char const *fail)
    {
        lastFile = file;
        lastLine = line;

        std::ostringstream oss;
        oss << file << ":" << line << ": must be less: " << quote(xa) << " < " << quote(xb) << "; " << fail;
        test_fail(oss.str());
    }

    void test_assertgreater(char const *file, int line, char const *xa, char const *xb, char const *fail)
    {
        lastFile = file;
        lastLine = line;

        std::ostringstream oss;
        oss << file << ":" << line << ": must be greater: " << quote(xa) << " > " << quote(xb) << "; " << fail;
        test_fail(oss.str());
    }

    void test_assertwithin(char const *file, int line, char const *xa, char const *xb, char const *fail)
    {
        lastFile = file;
        lastLine = line;

        std::ostringstream oss;
        oss << file << ":" << line << ": must be near: " << quote(xa) << " ~ " << quote(xb) << "; " << fail;
        test_fail(oss.str());
    }
}

