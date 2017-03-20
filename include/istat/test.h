
#if !defined (istat_test_h)
#define istat_test_h

#include <cmath>
#include <sstream>
#include <boost/cstdint.hpp>
#include <boost/lexical_cast.hpp>

#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <new>


namespace istat
{
    int test(void (*func)(), int argc, char const *argv[]);
    void test_init_path(std::string const &path);

    void test_asserttrue(char const *file, int line, char const *expr, bool b);
    #define assert_true(x) istat::test_asserttrue(__FILE__, __LINE__, #x, x)
    void test_assertfalse(char const *file, int line, char const *expr, bool b);
    #define assert_false(x) istat::test_assertfalse(__FILE__, __LINE__, #x, x)
    void test_assertequal(char const *file, int line, char const *xa, char const *xb, char const *fail);
    void test_assertnotequal(char const *file, int line, char const *xa, char const *xb, char const *fail);
    void test_assertcontains(char const *file, int line, std::string const &haystack, std::string const &needle, std::string const &fail);
    #define assert_contains(h,n) istat::test_assertcontains(__FILE__, __LINE__, (h), (n), (std::string(#h) + " does not contain " + #n).c_str())
    void test_assert_does_not_contain(char const *file, int line, std::string const &haystack, std::string const &needle, std::string const &fail);
    #define assert_does_not_contain(h,n) istat::test_assert_does_not_contain(__FILE__, __LINE__, (h), (n), (std::string(#h) + " contains " + #n).c_str())
    std::string quote(std::string const &str);
    template<typename A, typename B> inline void test_assertequal(char const *file, int line, char const *xa, char const *xb, A const &a, B const &b)
    {
        std::stringstream ss;
        ss << quote(boost::lexical_cast<std::string>(a)) << " doesn't equal " <<
            quote(boost::lexical_cast<std::string>(b));
        test_assertequal(file, line, xa, xb, ss.str().c_str());
    }
    #define assert_equal(a,b) if ((a)==(b)) {} else { istat::test_assertequal(__FILE__, __LINE__, #a, #b, a, b); }
    template<typename A, typename B> inline void test_assertnotequal(char const *file, int line, char const *xa, char const *xb, A const &a, B const &b)
    {
        std::stringstream ss;
        ss << quote(boost::lexical_cast<std::string>(a)) << " equals " <<
            quote(boost::lexical_cast<std::string>(b));
        test_assertnotequal(file, line, xa, xb, ss.str().c_str());
    }
    #define assert_not_equal(a,b) if ((a)!=(b)) {} else { istat::test_assertnotequal(__FILE__, __LINE__, #a, #b, a, b); }

    void test_assertless(char const *file, int line, char const *xa, char const *xb, char const *fail);
    template<typename A, typename B> inline void test_assertless(char const *file, int line, char const *xa, char const *xb, A const &a, B const &b)
    {
        std::stringstream ss;
        ss << quote(boost::lexical_cast<std::string>(a)) << " not less than " <<
            quote(boost::lexical_cast<std::string>(b));
        test_assertless(file, line, xa, xb, ss.str().c_str());
    }
    #define assert_less_than(a,b) if ((a)<(b)) {} else { istat::test_assertless(__FILE__, __LINE__, #a, #b, a, b); }

    void test_assertgreater(char const *file, int line, char const *xa, char const *xb, char const *fail);
    template<typename A, typename B> inline void test_assertgreater(char const *file, int line, char const *xa, char const *xb, A const &a, B const &b)
    {
        std::stringstream ss;
        ss << quote(boost::lexical_cast<std::string>(a)) << " not greater than " <<
            quote(boost::lexical_cast<std::string>(b));
        test_assertgreater(file, line, xa, xb, ss.str().c_str());
    }
    #define assert_greater_than(a,b) if ((a)>(b)) {} else { istat::test_assertgreater(__FILE__, __LINE__, #a, #b, a, b); }

    void test_assertwithin(char const *file, int line, char const *xa, char const *xb, char const *fail);
    template<typename A, typename B, typename Dist> inline void test_assertwithin(char const *file, int line, char const *xa, char const *xb, const char *xdist, A const &a, B const &b, Dist const &dist)
    {
        std::stringstream ss;
        ss << quote(boost::lexical_cast<std::string>(a))
            << " not within " << dist << " of " <<
            quote(boost::lexical_cast<std::string>(b))
            << " (distance = " <<
            fabs(a - b)
            << ")";
        test_assertwithin(file, line, xa, xb, ss.str().c_str());
    }
    #define assert_within(a, b, dist) if (fabs((b) - (a)) < (dist)) {} else { istat::test_assertwithin(__FILE__, __LINE__, #a, #b, #dist, a, b, dist); }

    template<typename T>
    class CallCounter
    {
    public:
        CallCounter(T ret) : count_(0), value_(0), ret_(ret) {}
        void reset() { count_ = 0; value_ = 0; str_ = ""; }
        mutable int64_t count_;
        mutable int64_t value_;
        mutable T ret_;
        mutable std::string str_;
        T operator()() const { call(); return ret_;} 
        void call() const { ++count_; }
        T operator()(int64_t v) const { call(v); return ret_;}
        void call(int64_t v) const { ++count_; value_ += v; }
        T operator()(std::string const &str) const { call(str); return ret_;}
        void call(std::string const &str) const { ++count_; str_ += str; }
    };

    template<>
    class CallCounter<void>
    {
    public:
        CallCounter() : count_(0), value_(0) {}
        void reset() { count_ = 0; value_ = 0; str_ = ""; }
        mutable int64_t count_;
        mutable int64_t value_;
        mutable std::string str_;
        void operator()() const { call(); } 
        void call() const { ++count_; }
        void operator()(int64_t v) const { call(v); }
        void call(int64_t v) const { ++count_; value_ += v; }
        void operator()(std::string const &str) const { call(str); }
        void call(std::string const &str) const { ++count_; str_ += str; }
    
    };
}

#if !defined( BOOST_HAS_VARIADIC_TMPL )

namespace boost
{

template< class T, class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10 >
boost::shared_ptr< T > make_shared( A1 const & a1, A2 const & a2, A3 const & a3, A4 const & a4, A5 const & a5, A6 const & a6, A7 const & a7, A8 const & a8, A9 const & a9, A10 const & a10 )
{
    boost::shared_ptr< T > pt( static_cast< T* >( 0 ), boost::detail::sp_ms_deleter< T >() );

    boost::detail::sp_ms_deleter< T > * pd = boost::get_deleter< boost::detail::sp_ms_deleter< T > >( pt );

    void * pv = pd->address();

    ::new( pv ) T( a1, a2, a3, a4, a5, a6, a7, a8, a9, a10 );
    pd->set_initialized();

    T * pt2 = static_cast< T* >( pv );

    boost::detail::sp_enable_shared_from_this( &pt, pt2, pt2 );
    return boost::shared_ptr< T >( pt, pt2 );
}

}

#endif


#endif  //  istat_test_h

