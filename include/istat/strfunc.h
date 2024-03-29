
#if !defined(daemon_strfunc_h)
#define daemon_strfunc_h

#include <string>
#include <vector>
#include <map>
#include <stdint.h>

namespace istat
{
    //  Split a given string to the left and right at the first instance
    //  of the given character.
    int split(std::string const &src, char ch, std::string &oLeft, std::string &oRight);
    //  Trim whitespace from the beginning and end of the string.
    void trim(std::string &str);
    //  Turn some ill behaved characters (slashes, quotes, unprintables) into underscores.
    void munge(std::string &key);
    //  Replace periods with slashes, turning counter stems into directories
    std::string counter_filename(std::string const &ctr);
    //  Strip the extension, in place, from a string. If there
    //  is no period in the string, return false.
    bool stripext(std::string &str);
    //  Given a period separated string with ^ substitutions, extract the
    //  possible substitutions. For example:
    //  input: foo.bar.baz^blorg^derp.pred^end
    //  outputs: foo.bar.baz.blorg, foo.bar.baz.derp.pred, foo.bar.baz.end
    //  Each caret serves as a period that takes over from the previous caret.
    //  The first caret just serves as a period. If the string ends with a caret,
    //  the base (before the first caret) will be considered an output in itself.
    //  Thus, "string^" is an alias for "string" whereas "string^x" is an alias
    //  for (only) "string.x" and "string^x^" is an alias for "string.x, string".
    //  Subs will be appended to the vector.
    void extract_ctrs(std::string const &istr, std::vector<std::string> &osubs);
    //  Combine path names -- if the second is absolute, then use that; else
    //  combine the two paths.
    std::string combine_paths(std::string const &left, std::string const &right);
    // explode a string
    void explode(std::string const &istr, char ch, std::vector<std::string> &results);
    //  quote to make safe for SQL string
    std::string sql_quote(std::string const &str);
    std::string sql_unquote(std::string const &str);
    std::string js_quote(std::string const &str);
    std::string js_unquote(std::string const &str);

    int querystring(std::string const &in, std::map<std::string, std::string> &oQuery);
    void urldecode(std::string &str);
    bool str_pat_match(std::string const &str, std::string const &pat);

    char const *static_file_type(std::string const &name);

    time_t interval_to_seconds(std::string const &ival);
    std::string seconds_to_interval(time_t secs);
    std::string http_date(time_t t = 0);
    std::string iso_8601_datetime(int64_t t = 0);

    bool is_valid_settings_name(std::string const &str);

    int splitn(std::string const &s, char ch, std::string &o1, std::string &o2);
    int splitn(std::string const &s, char ch, std::string &o1, std::string &o2, std::string &o3);
    int splitn(std::string const &s, char ch, std::string &o1, std::string &o2, std::string &o3, std::string &o4);
    int splitn(std::string const &s, char ch, std::string &o1, std::string &o2, std::string &o3, std::string &o4, std::string &o5);
    int splitn(std::string const &s, char ch, std::string &o1, std::string &o2, std::string &o3, std::string &o4, std::string &o5, std::string &o6);

    void prom_munge(std::string &s);
}
#endif

