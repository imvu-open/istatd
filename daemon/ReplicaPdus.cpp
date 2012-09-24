
#include "ReplicaPdus.h"
#include "EagerConnection.h"

#include <boost/bind.hpp>
#include <assert.h>


PduReaderActor::PduReaderActor(boost::shared_ptr<ConnectionInfo> const &conn) :
    conn_(conn),
    pduComplete_(true)
{
    conn_->onDisconnect_.connect(boost::bind(&PduReaderActor::on_disconnect, this));
    conn_->onData_.connect(boost::bind(&PduReaderActor::on_data, this));
}

PduReaderActor::~PduReaderActor()
{
}

bool PduReaderActor::nextPdu(PduHeader &pdu)
{
    grab aholdof(lock_);
    if (!pending_.size() || (pending_.size() == 1 && !pduComplete_))
    {
        return false;
    }
    //  avoid a memcpy
    PduHeader &f(pending_.front());
    pdu.payload.swap(f.payload);
    pdu.type = f.type;
    pdu.size = f.size;
    pending_.pop_front();
    return true;
}

void PduReaderActor::on_disconnect()
{
    conn_->close();
    onDeleted_();
}

void PduReaderActor::on_data()
{
again:
    if (pduComplete_)
    {
        //  previous PDU was complete, so make sure we can start a new PDU!
        if (conn_->pendingIn() < 8)
        {
            //  not enough for a header
            return;
        }
        //  make a header for the new PDU
        pending_.push_back(PduHeader());
        PduHeader &h = pending_.back();
        size_t n = conn_->readIn(&h.type, 4);
        //  this should always be true, because we already checked available size
        assert(n == 4);
        n = conn_->readIn(&h.size, 4);
        assert(n == 4);
        pduComplete_ = false;
    }
    //  fill an existing PDU
    {
        //  scope to show that this reference dies when going back to the beginning
        PduHeader &i = pending_.front();
        size_t got = i.payload.size();
        size_t needed = i.size - got;
        size_t avail = conn_->pendingIn();
        if (avail > needed)
        {
            avail = needed;
        }
        i.payload.resize(got + avail);
        //  read what's available, up to as much as we need
        size_t nn = conn_->readIn(&i.payload[got], avail);
        //  it should already be there!
        assert(nn == avail);
        if (i.payload.size() == i.size)
        {
            //  if we completed, mark it complete, and check for another one
            pduComplete_ = true;
            onPdu_();
            goto again;
        }
    }
}


PduWriter::PduWriter(void *buf, size_t sz, size_t &oPos) :
    buf_(buf),
    sz_(sz),
    pos_(oPos)
{
    if (sz_ <= pos_)
    {
        throw PduOverflowException("No space in buffer in PduWriter::PduWriter()");
    }
}

PduWriter &PduWriter::write(void const *src, size_t sz)
{
    //  test this way to avoid overflows
    if ((sz > sz_) || ((sz_ - sz) < pos_))
    {
        throw PduOverflowException("Overflow writing to PDU");
    }
    assert(pos_ + sz <= sz_);
    memcpy((char *)buf_ + pos_, src, sz);
    pos_ += sz;
    return *this;
}

