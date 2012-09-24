
#include <istat/test.h>

#include <string>
#include <iostream>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/path.hpp>


void func()
{
    assert_equal(boost::lexical_cast<time_t>(std::string("1234567890")), 1234567890);
    assert_equal(boost::lexical_cast<double>("2.25"), 2.25);
    assert_equal(boost::lexical_cast<double>("0"), 0);
    assert_equal(boost::filesystem::path("/var/enno/sucks.png").parent_path(), "/var/enno");
    assert_equal(boost::filesystem::path("/var/enno/sucks.png").filename(), "sucks.png");
}

int main(int argc, char const *argv[])
{
    return istat::test(&func, argc, argv);
}

