
#include <istat/test.h>
#include <istat/IRecorder.h>


void func()
{
    istat::RecordStats rs;
    istat::Stats &s(rs);
    s.statHit->record(1);
    s.statMiss->record(3);
    assert_equal(rs.nHits.stat_, 1);
    assert_equal(rs.nMisses.stat_, 3);
}

int main(int argc, char const *argv[])
{
    return istat::test(&func, argc, argv);
}

