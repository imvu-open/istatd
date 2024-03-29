
#include <iostream>
#include "istat/test.h"
#include "istat/strfunc.h"
#include <stdexcept>

namespace istat
{

    void test_splitn()
    {
        std::string a, b, c("foo"), d, e, f, g;
        a = "x";
        int n = splitn(a, ':', b, c, d, e, f, g);
        assert_equal(n, 1);
        assert_equal(b, a);
        assert_equal(c, "");
        a = "1:22:333:4444:555:66:7";
        n = splitn(a, ':', b, c, d, e, f, g);
        assert_equal(n, 6);
        assert_equal(b, "1");
        assert_equal(c, "22");
        assert_equal(d, "333");
        assert_equal(e, "4444");
        assert_equal(f, "555");
        assert_equal(g, "66:7");
        assert_equal(splitn("", ':', a, b), 1);
    }

    void test_split()
    {
        std::string a;
        std::string left;
        std::string right;
        assert_equal(0, split(a, 'x', left, right));
        a = "foo bar";
        assert_equal(1, split(a, 'x', left, right));
        assert_equal(a, left);
        a = "fooxbarxbaz";
        assert_equal(2, split(a, 'x', left, right));
        assert_equal("foo", left);
        assert_equal("barxbaz", right);
        a = "x";
        assert_equal(2, split(a, 'x', left, right));
        assert_equal("", left);
        assert_equal("", right);
    }

    void test_trim()
    {
        std::string a;
        std::string b(" ");
        std::string c("  ");
        std::string d("   ");
        std::string e("x");
        std::string f("x y");
        std::string g(" x y");
        std::string h("x y ");
        std::string i(" x y ");
        std::string j("this is a journey into the sound      ");
        std::string k("      this is a journey into the sound");
        std::string l("   this is a journey into the sound   ");
        trim(a);
        assert_equal(a, "");
        trim(b);
        assert_equal(b, "");
        trim(c);
        assert_equal(c, "");
        trim(d);
        assert_equal(d, "");
        trim(e);
        assert_equal(e, "x");
        trim(f);
        assert_equal(f, "x y");
        trim(g);
        assert_equal(g, "x y");
        trim(h);
        assert_equal(h, "x y");
        trim(i);
        assert_equal(i, "x y");
        trim(j);
        assert_equal(j, "this is a journey into the sound");
        trim(k);
        assert_equal(k, "this is a journey into the sound");
        trim(l);
        assert_equal(l, "this is a journey into the sound");
    }

    void test_urldecode()
    {
        std::string ia("");
        urldecode(ia);
        assert_equal(ia, "");

        std::string ib("abc def");
        urldecode(ib);
        assert_equal(ib, "abc def");

        std::string ic("+%20");
        urldecode(ic);
        assert_equal(ic, "  ");

        std::string id("%41+string+ending+in+%");
        urldecode(id);
        assert_equal(id, "A string ending in %");
    }

    void test_querystring()
    {
        std::string happy("foo=bar&baz=%20");
        std::map<std::string, std::string> m;
        int n = querystring(happy, m);
        assert_equal(n, 2);
        assert_equal(m.size(), 2);
        assert_equal(m["foo"], std::string("bar"));
        assert_equal(m["baz"], std::string(" "));

        std::string sad("&");
        std::map<std::string, std::string> l;
        n = querystring(sad, l);
        assert_equal(n, 0);
        assert_equal(l.size(), 0);
    }

    void test_str_pat_match()
    {
        //  exact matches
        assert_equal(true, str_pat_match("", ""));
        assert_equal(false, str_pat_match("x", ""));
        assert_equal(false, str_pat_match("", "x"));
        assert_equal(true, str_pat_match("foo.bar.baz", "foo.bar.baz"));
        assert_equal(false, str_pat_match("foo.bar", "foo.bar.baz"));
        assert_equal(false, str_pat_match("foo.bar.baz", "foo.bar"));

        //  star matches
        assert_equal(true, str_pat_match("foo bar", "*"));
        assert_equal(false, str_pat_match("*", "foo bar"));
        assert_equal(true, str_pat_match("foo.bar", "foo*"));
        assert_equal(true, str_pat_match("foo.bar", "*bar"));
        assert_equal(false, str_pat_match("foo.bar", "f*a*bar"));
        assert_equal(true, str_pat_match("foo.bar", "foo.*"));
        assert_equal(true, str_pat_match("foo.foo.bar", "f*oo*foo.bar"));
        assert_equal(true, str_pat_match("foo.foo.bar", "f*oo.bar"));
        assert_equal(true, str_pat_match("foo", "f***oo"));

        //  huh matches
        assert_equal(true, str_pat_match("", "?"));
        assert_equal(false, str_pat_match("?", ""));
        assert_equal(true, str_pat_match("foo.bar", "?.bar"));
        assert_equal(true, str_pat_match("foo.bar", "foo.?"));
        assert_equal(true, str_pat_match("foooo", "f?o"));
        assert_equal(false, str_pat_match("foo.bar.baz", "foo.?"));
        assert_equal(false, str_pat_match("foo.bar.baz", "?.baz"));
        assert_equal(true, str_pat_match("foo.bar.baz", "foo.?.baz"));
        assert_equal(true, str_pat_match("foo.bar.baz", "foo.b?.baz"));
        assert_equal(false, str_pat_match("foo.bar.baz", "foo.b?r?baz"));
        assert_equal(true, str_pat_match("foo.bar.baz", "foo.bar?.baz"));
        assert_equal(true, str_pat_match("foo.bar.baz", "foo.bar.baz?"));
        assert_equal(true, str_pat_match("foo.bar.baz", "?foo.bar.baz"));
    }

    void test_munge()
    {
        std::string a("foo bar");
        munge(a);
        assert_equal(a, "foo_bar");

        std::string b("/FOO.bar (\"quote\')");
        munge(b);
        assert_equal(b, "_foo.bar_(_quote_)");

        std::string c("");
        munge(c);
        assert_equal(c, "");
    }

    void test_counter_filename()
    {
        assert_equal("foo/bar", counter_filename("foo.bar"));
        assert_equal("foo_bar", counter_filename("foo_bar"));
        assert_equal("", counter_filename(""));
        assert_equal("foo/bar/baz\'", counter_filename("foo.bar.baz\'"));
    }

    void test_stripext()
    {
        std::string sex("...");
        bool b = stripext(sex);
        assert_true(b);
        assert_equal(sex, "..");
        b = stripext(sex);
        assert_true(b);
        assert_equal(sex, ".");
        b = stripext(sex);
        assert_true(b);
        assert_equal(sex, "");
        b = stripext(sex);
        assert_equal(sex, "");
        assert_false(b);

        sex = "foo.bar.baz";
        stripext(sex);
        assert_equal(sex, "foo.bar");
        stripext(sex);
        assert_equal(sex, "foo");
        stripext(sex);
        assert_equal(sex, "foo");
    }

    void test_extract()
    {
        std::vector<std::string> out;

        out.clear();
        extract_ctrs("foo.bar", out);
        assert_equal(out.size(), 1);
        assert_equal(out[0], "foo.bar");

        out.clear();
        extract_ctrs("", out);
        assert_equal(out.size(), 0);

        out.clear();
        extract_ctrs("^", out);
        assert_equal(out.size(), 0);

        out.clear();
        extract_ctrs("^xy", out);
        assert_equal(out.size(), 1);
        assert_equal(out[0], "xy");

        out.clear();
        extract_ctrs("xy^", out);
        assert_equal(out.size(), 1);
        assert_equal(out[0], "xy");

        out.clear();
        extract_ctrs("x^y", out);
        assert_equal(out.size(), 1);
        assert_equal(out[0], "x.y");

        out.clear();
        extract_ctrs("x^y^z", out);
        assert_equal(out.size(), 2);
        assert_equal(out[0], "x.y");
        assert_equal(out[1], "x.z");

        out.clear();
        extract_ctrs("x^y^z^", out);
        assert_equal(out.size(), 3);
        assert_equal(out[0], "x.y");
        assert_equal(out[1], "x.z");
        assert_equal(out[2], "x");

        out.clear();
        extract_ctrs("cpu.idle^host.af1234^class.dbserver^class.memcache^role.db-shard-10^role.memcache-13^role.memcache-14", out);
        assert_equal(out.size(), 6);
        assert_equal(out[0], "cpu.idle.host.af1234");
        assert_equal(out[1], "cpu.idle.class.dbserver");
        assert_equal(out[2], "cpu.idle.class.memcache");
        assert_equal(out[3], "cpu.idle.role.db-shard-10");
        assert_equal(out[4], "cpu.idle.role.memcache-13");
        assert_equal(out[5], "cpu.idle.role.memcache-14");
    }

    void test_explode()
    {
        std::vector<std::string> out;

        out.clear();
        explode("THIS IS A TEST",' ',out);
        assert_equal(4, out.size());
        assert_equal(out[0], "THIS");
        assert_equal(out[1], "IS");
        assert_equal(out[2], "A");
        assert_equal(out[3], "TEST");

        out.clear();
        explode("rawr@grr@",'@',out);
        assert_equal(2, out.size());
        assert_equal(out[0], "rawr");
        assert_equal(out[1], "grr");

        out.clear();
        explode("rawr",'!',out);
        assert_equal(1, out.size());
        assert_equal(out[0], "rawr");

        out.clear();
        explode("",' ',out);
        assert_equal(0, out.size());
    }


    void test_combine()
    {
        assert_equal(combine_paths("", ""), "");
        assert_equal(combine_paths("a", "b"), "a/b");
        assert_equal(combine_paths("./foo", "/bar"), "/bar");
        assert_equal(combine_paths("/foo/bar", "./baz"), "/foo/bar/./baz");
        assert_equal(combine_paths("xyzzy", ""), "xyzzy/");
        assert_equal(combine_paths("", "xyzzy"), "xyzzy");
    }

    void test_sql_quote()
    {
        assert_equal(sql_quote("a perfectly innocuous string"), "a perfectly innocuous string");
        assert_equal(sql_quote("\\foo;"), "\\\\foo;");
        assert_equal(sql_quote("'"), "\\'");
        assert_equal(sql_quote("%_blas\r\n'foo\"\\''"), "\\%\\_blas\\r\\n\\'foo\\\"\\\\\\'\\'");
        assert_equal(sql_quote(""), "");
        assert_equal(sql_quote("1234567890"), "1234567890");
        assert_equal(sql_quote("\r\n\b\t\032_%"), "\\r\\n\\b\\t\\Z\\_\\%");

        assert_equal("a perfectly innocuous string", sql_unquote("a perfectly innocuous string"));
        assert_equal("\\foo;", sql_unquote("\\\\foo;"));
        assert_equal("'", sql_unquote("\\'"));
        assert_equal("%_blas\r\n'foo\"\\''", sql_unquote("\\%\\_blas\\r\\n\\'foo\\\"\\\\\\'\\'"));
        assert_equal("", sql_unquote(""));
        assert_equal("1234567890", sql_unquote("1234567890"));
        assert_equal("\r\n\b\t\032_%", sql_unquote("\\r\\n\\b\\t\\Z\\_\\%"));
    }

    void test_js_quote()
    {
        assert_equal(js_quote("a perfectly innocuous string"), "a perfectly innocuous string");
        assert_equal(js_quote("\\foo;"), "\\\\foo;");
        assert_equal(js_quote("'"), "\\'");
        assert_equal(js_quote("%_blas\r\n'foo\"\\''"), "%_blas\\r\\n\\'foo\\\"\\\\\\'\\'");
        assert_equal(js_quote(""), "");
        assert_equal(js_quote("1234567890"), "1234567890");
        assert_equal(js_quote("\r\n\b\t\032_%"), "\\r\\n\\b\\t\032_%");

        assert_equal("a perfectly innocuous string", js_unquote("a perfectly innocuous string"));
        assert_equal("\\foo;", js_unquote("\\\\foo;"));
        assert_equal("'", js_unquote("\\'"));
        assert_equal("%_blas\r\n'foo\"\\''", js_unquote("\\%\\_blas\\r\\n\\'foo\\\"\\\\\\'\\'"));
        assert_equal("", js_unquote(""));
        assert_equal("1234567890", js_unquote("1234567890"));
        assert_equal("\r\n\b\tZ_%", js_unquote("\\r\\n\\b\\t\\Z\\_\\%"));
    }

    void test_interval()
    {
        bool thrown = false;
        time_t val = 0;
        try {
            val = interval_to_seconds("");
        }
        catch (std::runtime_error const &re)
        {
            thrown = true;
        }
        assert_true(thrown);
        assert_equal(val, 0);
        thrown = false;
        try {
            val = interval_to_seconds("600x");
        }
        catch (std::runtime_error const &re)
        {
            thrown = true;
        }
        assert_equal(val, 0);
        assert_true(thrown);
        thrown = false;
        try {
            val = interval_to_seconds("600s15");
        }
        catch (std::runtime_error const &re)
        {
            thrown = true;
        }
        assert_true(thrown);
        assert_equal(val, 0);
        thrown = false;
        try {
            val = interval_to_seconds("y");
        }
        catch (std::runtime_error const &re)
        {
            thrown = true;
        }
        assert_true(thrown);
        assert_equal(val, 0);
        thrown = false;
        try {
            val = interval_to_seconds("y600s");
        }
        catch (std::runtime_error const &re)
        {
            thrown = true;
        }
        assert_true(thrown);
        assert_equal(val, 0);

        assert_equal(1, interval_to_seconds("1s"));
        assert_equal(60, interval_to_seconds("1m"));
        assert_equal(3600, interval_to_seconds("1h"));
        assert_equal(86400, interval_to_seconds("1d"));
        assert_equal(86400*365, interval_to_seconds("1y"));
    }

    void test_iso_8601_datetime()
    {
        std::string result = iso_8601_datetime(1320962690);
        assert_equal(result, "2011-11-10T22:04:50Z");
    }

    void test_is_valid_settings_name()
    {
        assert_true(is_valid_settings_name("some.test"));
        assert_true(is_valid_settings_name("abc-def_g"));
        assert_true(is_valid_settings_name("test123"));
        assert_false(is_valid_settings_name("../store/istat/huh"));
        assert_false(is_valid_settings_name("spaces are not allowed"));
        assert_false(is_valid_settings_name("^%$"));
    }

    void test_prom_munge()
    {
        std::string a("foo bar");
        prom_munge(a);
        assert_equal(a, "foo_bar");

        std::string b("/FOO.bar (\"quote\')");
        prom_munge(b);
        assert_equal(b, "_foo_bar___quote__");

        std::string c("");
        prom_munge(c);
        assert_equal(c, "");

        std::string d("foo-bar");
        prom_munge(d);
        assert_equal(d, "foo_bar");

        std::string e("059.aez-AxZ{");
        prom_munge(e);
        assert_equal(e, "059_aez_axz_");
    }

    void func()
    {
        test_splitn();
        test_interval();
        test_combine();
        test_extract();
        test_split();
        test_trim();
        test_urldecode();
        test_querystring();
        test_str_pat_match();
        test_munge();
        test_counter_filename();
        test_stripext();
        test_explode();
        test_sql_quote();
        test_js_quote();
        test_iso_8601_datetime();
        test_is_valid_settings_name();
        test_prom_munge();
    }
}

int main(int argc, char const *argv[])
{
    return istat::test(istat::func, argc, argv);
}


// eek!
