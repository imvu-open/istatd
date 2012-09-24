

#include "../daemon/PduProtocol.h"
#include "../daemon/ReplicaPdus.h"

#include <istat/test.h>


PduConnect const *pduConnect;
bool gotPduConnect;

void got_Connect(PduConnect const &c)
{
    pduConnect = &c;
    gotPduConnect = true;
}

PduRequest const *pduRequest;
bool gotPduRequest;

void got_Request(PduRequest const &r)
{
    pduRequest = &r;
    gotPduRequest = true;
}

void test_Protocol_server()
{
    //  set up the protocol
    PduProtocol server("server-start");
    PduProtocolState *serverStart = server.state("server-start");
    PduProtocolState *serverHasClient = server.state("server-hasclient");
    serverStart->bindPdu<PduConnect>(got_Connect, serverHasClient);
    serverHasClient->bindPdu<PduRequest>(got_Request);

    assert_equal(server.curState(), serverStart);

    //  run it through some PDUs
    char buf[1024];
    size_t oPos = 0;
    PduConnect *pdu1 = PduConnect::make(buf, 1024, oPos, ReplicateProtocolVersion1, UniqueId::make());
    size_t oPosConnect = oPos;
    assert_false(gotPduConnect);
    server.onPdu(PduConnect::Id, oPos, buf);
    assert_true(gotPduConnect);
    assert_equal(pduConnect->ServerId.str(), pdu1->ServerId.str());
    assert_equal(server.curState(), serverHasClient);

    oPos = 0;
    PduRequest *pdu2 = PduRequest::make(buf, 1024, oPos, 1000000, 1024, "some.name");
    assert_false(gotPduRequest);
    server.onPdu(PduRequest::Id, oPos, buf);
    assert_true(gotPduRequest);
    assert_equal(pduRequest->getName(), pdu2->getName());

    //  now, make sure we throw
    std::string thrown;
    try
    {
        server.onPdu(PduConnect::Id, oPosConnect, buf);
    }
    catch (PduProtocolException const &ppx)
    {
        thrown = ppx.what();
    }
    assert_contains(thrown, "Unhandled PDU");
    assert_equal(server.curState(), serverHasClient);
}

void test_Protocol()
{
    test_Protocol_server();
}


int main(int argc, char const *argv[])
{
    return istat::test(&test_Protocol, argc, argv);
}

