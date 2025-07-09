
#include "istat/strfunc.h"
#include <ctype.h>
#include <time.h>
#include <algorithm>
#include <list>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/c_time.hpp>
#include <stdexcept>
#include <iostream>

namespace istat
{
    char const *static_file_type(std::string const &name)
    {
        if (name.find(".htm") != std::string::npos)
        {
            return "text/html; charset=utf-8";
        }
        if (name.find(".css") != std::string::npos)
        {
            return "text/css; charset=utf-8";
        }
        if (name.find(".js") != std::string::npos)
        {
            return "application/javascript; charset=utf-8";
        }
        if (name.find(".jpg") != std::string::npos)
        {
            return "image/jpeg";
        }
        if (name.find(".png") != std::string::npos)
        {
            return "image/png";
        }
        if (name.find(".gif") != std::string::npos)
        {
            return "image/gif";
        }
        if (name.find(".xml") != std::string::npos)
        {
            return "text/xml; charset=utf-8";
        }
        if (name.find(".ico") != std::string::npos)
        {
            return "image/x-icon";
        }
        return "application/octet-stream";
    }

    int split(std::string const &src, char ch, std::string &oLeft, std::string &oRight)
    {
        size_t len = src.length();
        if (len == 0)
        {
            oLeft.clear();
            oRight.clear();
            return 0;
        }
        size_t pos = src.find_first_of(ch);
        if (pos >= len)
        {
            oLeft = src;
            oRight.clear();
            return 1;
        }
        oLeft = src.substr(0, pos);
        oRight = src.substr(pos+1);
        return 2;
    }

    void trim(std::string &str)
    {
        size_t len = str.length();
        size_t olen = len;
        size_t pos = 0;
        while (len > 0 && isspace(str[len-1]))
        {
            --len;
        }
        while (pos < len && isspace(str[pos]))
        {
            ++pos;
        }
        if (pos > 0 || len < olen)
        {
            str = str.substr(pos, len-pos);
        }
    }

    static char munge_char(char ch)
    {
        if (ch < 33 || ch > 126 || ch == '/' || ch == ':' || ch == '\\' || ch == '\"' || ch == '\'')
        {
            return '_';
        }
        return tolower(ch);
    }

    void munge(std::string &key)
    {
        std::transform(key.begin(), key.end(), key.begin(), munge_char);
    }

    std::string counter_filename(std::string const &ctr)
    {
        std::string ret(ctr);
        std::replace(ret.begin(), ret.end(), '.', '/');
        return ret;
    }


    static std::string g_hexdigs("0123456789abcdef");

    static char hexdig(std::string::iterator &ptr, std::string::iterator const &end)
    {
        char ch = 0;
        for (int i = 0; i < 2; ++i)
        {
            ++ptr;
            if (ptr == end)
            {
                return '%';
            }
            ch = (ch << 4) | (g_hexdigs.find_first_of(tolower(*ptr)) & 0xf);
        }
        return ch;
    }

    void urldecode(std::string &str)
    {
        std::string::iterator src(str.begin()), dst(str.begin()), end(str.end());
        for (;
                src != end; ++src)
        {
            switch (*src)
            {
            case '+':
                *(dst++) = ' ';
                break;
            case '%':
                *(dst++) = hexdig(src, end);
                if (src == end)
                {
                    --src;
                }
                break;
            default:
                *(dst++) = *src;
                break;
            }
        }
        size_t len = dst - str.begin();
        str.resize(len);
    }

    int querystring(std::string const &in, std::map<std::string, std::string> &oQuery)
    {
        std::string right(in);
        std::string value;
        std::string key;
        int n = 0;
        while (split(right, '&', value, right))
        {
            if (split(value, '=', key, value) == 2)
            {
                urldecode(key);
                urldecode(value);
                oQuery[key] = value;
                ++n;
            }
        }
        return n;
    }

    struct pat_stat
    {
        size_t pstr;
        size_t ppat;
        pat_stat()
        {
            pstr = 0;
            ppat = 0;
        }
    };

    static char next(std::string const &str, size_t pos)
    {
        ++pos;
        if (pos == str.size())
        {
            return 0;
        }
        return str[pos];
    }

    bool str_pat_match(std::string const &str, std::string const &pat)
    {
        pat_stat S;
        size_t estr = str.size();
        size_t epat = pat.size();
        std::list<pat_stat> backup;
        backup.push_back(S);

        bool pop = true;
        char ch;
        char item;
        while (true)
        {
            if (pop)
            {
                if (backup.empty())
                {
                    break;
                }
                S = backup.back();
                backup.pop_back();
                pop = false;
            }
            if (S.ppat == epat)
            {
                if (S.ppat == epat && S.pstr == estr)
                {
                    return true;    //  success!
                }
                pop = true;
                continue;
            }
            switch ((ch = pat[S.ppat]))
            {
            case '?': // match anything non-.
                switch ((item = istat::next(pat, S.ppat)))
                {
                case 0:
                    if (str.find_first_of('.', S.pstr) == std::string::npos)
                    {
                        return true; // success
                    }
                    pop = true; //  failure
                    continue;
                case '?':
                    ++S.ppat;
                    break;
                case '*':
                    ++S.ppat;
                    break;
                default:
                {
                    size_t pos = str.find_first_of(item, S.pstr);
                    if (pos == std::string::npos)
                    {
                        pop = true; //  failure
                        continue;
                    }
                    size_t dot = str.find_first_of('.', S.pstr);
                    if (dot != std::string::npos && dot < pos)
                    {
                        pop = true; // failure
                    }
                    else
                    {
                        S.pstr = pos+1;
                        if (item != '.')
                        {
                            backup.push_back(S);
                        }
                        S.ppat += 2;
                    }
                }
                break;
                }
                break;
                case '*': // match anything
                {
                    size_t npos = S.ppat;
                    while ((item = istat::next(pat, npos++)) == '?')
                    {
                        //  iterate
                    }
                    if (item == 0)
                    {
                        return true;    //  success
                    }
                    if (item == '*')
                    {
                        S.ppat = npos;
                    }
                    else
                    {
                        size_t pos = str.find_first_of(item, S.pstr);
                        if (pos == std::string::npos)
                        {
                            pop = true; //  failure
                            continue;
                        }
                        S.pstr = pos+1;
                        backup.push_back(S);
                        S.ppat += 2;
                    }
                }
                break;
                default:
                    if (S.pstr == estr)
                    {
                        if (S.ppat == epat)
                        {
                            return true;    //  success
                        }
                        pop = true;
                        continue;
                    }
                    if (ch != str[S.pstr])
                    {
                        pop = true; //  failure
                        continue;
                    }
                    ++S.pstr;
                    ++S.ppat;
                    break;
            }
        }
        return S.pstr == estr && S.ppat == epat; // matched the entire pattern
    }

    bool stripext(std::string &str)
    {
        size_t n = str.size();
        while (n > 0)
        {
            if (str[n-1] == '.')
            {
                str.resize(n-1);
                return true;
            }
            --n;
        }
        return false;
    }

    std::string combine_paths(std::string const &left, std::string const &right)
    {
        if ((right.size() && right[0] == '/') || !left.size())
        {
            return right;
        }
        if (left[left.size()-1] == '/')
        {
            return left + right;
        }
        return left + "/" + right;
    }

    void explode(std::string const &istr, char ch, std::vector<std::string> &results)
    {
        std::string left;
        std::string right(istr);
        while(split(right, ch, left, right)) {
            results.push_back(left);
        }
    }

    void extract_ctrs(std::string const &istr, std::vector<std::string> &osubs, std::string & prombase)
    {
        std::string base;
        size_t pos = istr.find_first_of('^');
        if (pos == std::string::npos)
        {
            if (istr.size())
            {
                osubs.push_back(istr);
                prombase = istr;
            }
            return;
        }
        base = istr.substr(0, pos);
        prombase = istr.substr(0, pos);
        if (pos > 0)
        {
            base.push_back('.');
        }
        //  at this point, base is empty, or ending with a period, length 2+
        while (true)
        {
            size_t next = istr.find_first_of('^', pos+1);
            if (next == std::string::npos)
            {
                //  final element
                if (pos+1 < istr.size())
                {
                    //  just a final piece
                    osubs.push_back(base + istr.substr(pos+1));
                }
                else if (base.size())
                {
                    //  ends with a caret -- use the string, if
                    //  it's not empty (trim the last period)
                    osubs.push_back(base.substr(0, base.size()-1));
                }
                return;
            }
            if (next > pos+1)
            {
                //  don't allow empty substrings
                osubs.push_back(base + istr.substr(pos+1, next-(pos+1)));
                pos = next;
            }
        }
    }

    std::string sql_quote(std::string const &sql)
    {
        std::stringstream ss;
        for (std::string::const_iterator ptr(sql.begin()), end(sql.end());
                ptr != end; ++ptr)
        {
            switch (*ptr)
            {
            case '\\':
                ss << "\\\\";
                break;
            case '\'':
                ss << "\\'";
                break;
            case '\"':
                ss << "\\\"";
                break;
            case '%':
                ss << "\\%";
                break;
            case '_':
                ss << "\\_";
                break;
            case 0:
                ss << "\\0";
                break;
            case 8:
                ss << "\\b";
                break;
            case 9:
                ss << "\\t";
                break;
            case 10:
                ss << "\\n";
                break;
            case 13:
                ss << "\\r";
                break;
            case 26:
                ss << "\\Z";
                break;
            default:
                ss << *ptr;
                break;
            }
        }
        return ss.str();
    }

    std::string sql_unquote(std::string const &str)
    {
        std::stringstream ss;
        bool quote = false;
        char tmp[2] = { 0, 0 };
        for (std::string::const_iterator ptr(str.begin()), end(str.end());
                ptr != end; ++ptr)
        {
            if (quote)
            {
                switch (*ptr)
                {
                case 'r':
                    tmp[0] = 13;
                    break;
                case 'n':
                    tmp[0] = 10;
                    break;
                case 't':
                    tmp[0] = 9;
                    break;
                case 'b':
                    tmp[0] = 8;
                    break;
                case '0':
                    //   todo: this won't work!
                    tmp[0] = 0;
                    break;
                case 'Z':
                    tmp[0] = 26;
                    break;
                default:
                    tmp[0] = *ptr;
                    break;
                }
                ss << tmp;
                quote = false;
            }
            else if (*ptr == '\\')
            {
                quote = true;
            }
            else
            {
                tmp[0] = *ptr;
                ss << tmp;
            }
        }
        return ss.str();
    }

    std::string js_quote(std::string const &sql)
    {
        std::stringstream ss;
        for (std::string::const_iterator ptr(sql.begin()), end(sql.end());
                ptr != end; ++ptr)
        {
            switch (*ptr)
            {
            case '\\':
                ss << "\\\\";
                break;
            case '\'':
                ss << "\\'";
                break;
            case '\"':
                ss << "\\\"";
                break;
            case 0:
                ss << "\\0";
                break;
            case 8:
                ss << "\\b";
                break;
            case 9:
                ss << "\\t";
                break;
            case 10:
                ss << "\\n";
                break;
            case 13:
                ss << "\\r";
                break;
            default:
                ss << *ptr;
                break;
            }
        }
        return ss.str();
    }

    std::string js_unquote(std::string const &str)
    {
        std::stringstream ss;
        bool quote = false;
        char tmp[2] = { 0, 0 };
        for (std::string::const_iterator ptr(str.begin()), end(str.end());
                ptr != end; ++ptr)
        {
            if (quote)
            {
                switch (*ptr)
                {
                case 'r':
                    tmp[0] = 13;
                    break;
                case 'n':
                    tmp[0] = 10;
                    break;
                case 't':
                    tmp[0] = 9;
                    break;
                case 'b':
                    tmp[0] = 8;
                    break;
                case '0':
                    //  todo: this won't work!
                    tmp[0] = 0;
                    break;
                default:
                    tmp[0] = *ptr;
                    break;
                }
                ss << tmp;
                quote = false;
            }
            else if (*ptr == '\\')
            {
                quote = true;
            }
            else
            {
                tmp[0] = *ptr;
                ss << tmp;
            }
        }
        return ss.str();
    }

    struct interval_t {
        time_t size;
        time_t maxCount;
        char const *str;
    }
    intervals[] = {
            { 1, 60, "s" },
            { 60, 60, "m" },
            { 60, 24, "h" },
            { 24, 365, "d" },
            { 365, 0, "y" },
    };

    typedef std::pair<time_t, std::string> ivalpair;
    typedef std::vector<ivalpair> ivalvec;

    static void split(std::string const &ival, ivalvec &ivals)
    {
        std::string::const_iterator ptr(ival.begin()), base(ptr), end(ival.end());
        ivalpair ip;
        bool newDig = true;
        while (ptr < end)
        {
            if (isdigit(*ptr))
            {
                if (!newDig)
                {
                    std::string val(base, ptr);
                    ip.second = val;
                    newDig = true;
                    base = ptr;
                    ivals.push_back(ip);
                }
            }
            else
            {
                if (newDig)
                {
                    std::string val(base, ptr);
                    if (val.empty())    //  happens at start of string
                    {
                        throw std::runtime_error("invalid interval format: " + ival);
                    }
                    ip.first = boost::lexical_cast<time_t>(val);
                    newDig = false;
                    base = ptr;
                }
            }
            ++ptr;
        }
        //  format chars are one long
        if (ptr != base + 1 || newDig)
        {
            throw std::runtime_error("invalid interval format: " + ival);
        }
        std::string val(base, ptr);
        ip.second = val;
        ivals.push_back(ip);
    }

    time_t interval_to_seconds(std::string const &ival)
    {
        ivalvec ivals;
        split(ival, ivals);
        time_t ret = 0;
        for (ivalvec::iterator ptr(ivals.begin()), end(ivals.end()); ptr != end; ++ptr)
        {
            bool found = false;
            for (interval_t const *itp = intervals, *ite = &intervals[sizeof(intervals)/sizeof(interval_t)];
                    itp != ite; ++itp)
            {
                if ((*ptr).second == itp->str)
                {
                    found = true;
                    time_t mul = itp->size;
                    while (itp > intervals)
                    {
                        --itp;
                        mul = mul * itp->size;
                    }
                    ret = ret + (*ptr).first * mul;
                    break;
                }
            }
            if (!found)
            {
                throw std::runtime_error(std::string("Unknown time unit: ") + (*ptr).second);
            }
        }
        return ret;
    }

    std::string seconds_to_interval(time_t secs)
    {
        std::stringstream ret;
        interval_t const *itp = &intervals[sizeof(intervals)/sizeof(interval_t)];
        while (itp > intervals)
        {
            --itp;
            time_t size = 1;
            interval_t const *sump = itp;
            while (sump > intervals)
            {
                size = size * sump->size;
                --sump;
            }
            if (secs >= size)
            {
                size_t n = secs / size;
                ret << n << itp->str;
                secs -= n;
            }
        }
        std::string rstr = ret.str();
        if (rstr.empty())
        {
            rstr = "0s";
        }
        return rstr;
    }

    std::string http_date(time_t t)
    {
        if (t == 0)
        {
            time(&t);
        }
        struct tm stm;
        gmtime_r(&t, &stm);
        char buf[100];
        //  Wed, 15 Nov 1995 04:58:08 GMT
        strftime(buf, 100, "%a, %d %b %Y %H:%M:%S GMT", &stm);
        return std::string(buf);
    }

    std::string iso_8601_datetime(int64_t epoch)
    {
        char buf[32];
        struct tm tm;

        time_t t = epoch;
        boost::date_time::c_time::gmtime(&t, &tm);
        strftime(buf, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
        return std::string(buf);
    }

    bool is_valid_settings_name(std::string const &str)
    {
        for (std::string::const_iterator ptr(str.begin()), end(str.end());
            ptr != end; ++ptr)
        {
            if(!isalnum(*ptr) && *ptr != '_' && *ptr != '-' && *ptr != '.')
            {
                return false;
            }
        }
        return true;
    }

    int splitn(std::string const &s, char ch, std::string &o1, std::string &o2)
    {
        size_t p = s.find_first_of(ch);
        if (p == s.npos)
        {
            o1 = s;
            o2 = "";
            return 1;
        }
        //  do it in reverse order, to support &o1 == &s
        if (&o2 == &s)
        {
            throw std::runtime_error("unsupported argument aliasing in splitn()");
        }
        o2 = s.substr(p+1);
        o1 = s.substr(0, p);
        return 2;
    }

    int splitn(std::string const &s, char ch, std::string &o1, std::string &o2, std::string &o3)
    {
        if (splitn(s, ch, o1, o2) == 1)
        {
            return 1;
        }
        if (splitn(o2, ch, o2, o3) == 1)
        {
            return 2;
        }
        return 3;
    }

    int splitn(std::string const &s, char ch, std::string &o1, std::string &o2, std::string &o3, std::string &o4)
    {
        int n = splitn(s, ch, o1, o2, o3);
        if (n < 3)
        {
            return n;
        }
        if (splitn(o3, ch, o3, o4) == 1)
        {
            return 3;
        }
        return 4;
    }

    int splitn(std::string const &s, char ch, std::string &o1, std::string &o2, std::string &o3, std::string &o4, std::string &o5)
    {
        int n = splitn(s, ch, o1, o2, o3, o4);
        if (n < 4)
        {
            return n;
        }
        if (splitn(o4, ch, o4, o5) == 1)
        {
            return 4;
        }
        return 5;
    }

    int splitn(std::string const &s, char ch, std::string &o1, std::string &o2, std::string &o3, std::string &o4, std::string &o5, std::string &o6)
    {
        int n = splitn(s, ch, o1, o2, o3, o4, o5);
        if (n < 5)
        {
            return n;
        }
        if (splitn(o5, ch, o5, o6) == 5)
        {
            return 5;
        }
        return 6;
    }

    static char prom_munge_char(char ch)
    {
        /*
         * Prometheus metric name must match the regex [a-zA-Z_:][a-zA-Z0-9_:]*.
         * The colons are reserved for user defined recording rules.
         */
        if (! ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || (ch>= '0' && ch <= '9')))
        {
            return '_';
        }

        return tolower(ch);
    }

    void prom_munge(std::string &key)
    {
        std::transform(key.begin(), key.end(), key.begin(), prom_munge_char);
        if (key.size() > 0 && key[0] >= '0' && key[0] <= '9')
        {
            key.insert(key.begin(), '_');
        }
    }

    void prom_munge_or_munge(std::string &s, bool do_prom_mung)
    {
        if (do_prom_mung) {
            istat::prom_munge(s);
        } else {
            istat::munge(s);
        }
    }
}

