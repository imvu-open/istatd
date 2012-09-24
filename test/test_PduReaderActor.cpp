
#include <istat/test.h>
#include "../daemon/FakeEagerConnection.h"
#include "../daemon/ReplicaPdus.h"

bool gotPdu_;
bool gotDeleted_;

void on_pdu()
{
    gotPdu_ = true;
}

void on_deleted()
{
    gotDeleted_ = true;
}

void func()
{
/*
    TODO: figure out how to fake EagerConnection

    FakeEagerConnection fec;
    PduReaderActor *pra = new PduReaderActor(&fec);
    pra->onPdu_.connect(on_pdu);
    pra->onDeleted_.connect(on_deleted);
    assert_false(fec.didCall_);
    assert_false(gotPdu_);
    assert_false(gotDeleted_);

    pra->poke();
    assert_true(fec.didCall_);
    assert_false(gotPdu_);
    assert_false(gotDeleted_);

    char pdu[] = {
        0x3, 0x00, 0x00, 0x00, 0x2, 0x00, 0x00, 0x00, 0x10, 0x20,
        0x4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    fec.f_addData(pdu, 10);
    assert_true(gotPdu_);
    gotPdu_ = false;
    assert_false(gotDeleted_);
    PduHeader pdh;
    bool rd = pra->nextPdu(pdh);
    assert_true(rd);
    assert_equal(pdh.type, 3);
    assert_equal(pdh.size, 2);
    assert_equal(pdh.payload.size(), 2);
    assert_equal(pdh.payload[0], 0x10);
    assert_equal(pdh.payload[1], 0x20);
    rd = pra->nextPdu(pdh);
    assert_false(rd);

    //  test partial PDU delivery
    fec.f_addData(pdu, 7);
    assert_false(gotPdu_);

    fec.f_addData(&pdu[7], 2);
    assert_false(gotPdu_);

    //  test delivery of part of next PDU while finishing current
    fec.f_addData(&pdu[9], 2);
    assert_true(gotPdu_);
    gotPdu_ = false;
    pdh.type = 0;
    pdh.size = 0;
    pdh.payload.clear();
    rd = pra->nextPdu(pdh);
    assert_true(rd);
    assert_equal(pdh.type, 3);
    assert_equal(pdh.size, 2);
    assert_equal(pdh.payload.size(), 2);
    assert_equal(pdh.payload[0], 0x10);
    assert_equal(pdh.payload[1], 0x20);
    rd = pra->nextPdu(pdh);
    assert_false(rd);

    //  and finish out the last
    fec.f_addData(&pdu[11], 7);
    assert_true(gotPdu_);
    gotPdu_ = false;
    pdh.type = 0;
    pdh.size = 0;
    pdh.payload.clear();
    rd = pra->nextPdu(pdh);
    assert_true(rd);
    assert_equal(pdh.type, 4);
    assert_equal(pdh.size, 0);
    assert_equal(pdh.payload.size(), 0);

    fec.f_disconnect();
    assert_false(gotPdu_);
    assert_true(gotDeleted_);
    */
}

int main(int argc, char const *argv[])
{
    return istat::test(&func, argc, argv);
}

