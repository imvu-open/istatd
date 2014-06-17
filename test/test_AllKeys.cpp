#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <set>
#include <tr1/unordered_map>
#include <istat/test.h>
#include <istat/istattime.h>
#include <boost/shared_ptr.hpp>
#include <boost/assign.hpp>
#include <istat/Log.h>

#include "../daemon/AllKeys.h"

#define BUFSIZE 256

using namespace istat;

void test_allkeys_manipulation() {
    AllKeys ak;

    ak.add("a.file.that.is.nested", true);
    ak.add("a.file.that.is.non", false);
    ak.add("a.different.file", true);
    ak.add("a.other.different.file", false);

    std::string s("*");
    std::list<std::pair<std::string, CounterResponse> > oList;
    ak.match(s, oList);
    assert_equal(11, oList.size());

    ak.delete_all();

    std::list<std::pair<std::string, CounterResponse> > oList2;
    ak.match(s, oList2);
    assert_equal(0, oList2.size());
}

void func() {
    test_allkeys_manipulation();
}

int main(int argc, char const *argv[]) {
    LogConfig::setLogLevel(istat::LL_Spam);
    return istat::test(func, argc, argv);
}
