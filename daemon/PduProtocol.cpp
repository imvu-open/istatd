
#include <boost/lexical_cast.hpp>

#include "PduProtocol.h"

PduProtocolState::PduRecord::PduRecord() :
    callable_(0),
    nextState_(0)
{
}

PduProtocol::PduProtocol(std::string const &firstStateName)
{
    curState_ = state(firstStateName);
}

PduProtocolState *PduProtocol::state(std::string const &name)
{
    std::map<std::string, PduProtocolState *>::iterator ptr(states_.find(name));
    if (ptr == states_.end())
    {
        return states_[name] = new PduProtocolState(this, name);
    }
    return (*ptr).second;
}

PduProtocolState *PduProtocol::curState()
{
    return curState_;
}

void PduProtocol::onPdu(uint32_t type, size_t size, void const *data)
{
    curState_->onPdu(type, size, data);
}

void PduProtocol::forceToState(PduProtocolState *state)
{
    curState_ = state;
}

PduProtocolState::PduProtocolState(PduProtocol *proto, std::string const &name) :
    protocol_(proto),
    name_(name)
{
}

void PduProtocolState::onPdu(uint32_t type, size_t size, void const *data)
{
    std::list<PduRecord> &pduHandlers = pdus_[type];
    if (pduHandlers.size() == 0)
    {
        throw PduProtocolException("Unhandled PDU id " + boost::lexical_cast<std::string>(type) + " in " + name_);
    }
    PduProtocolState *nextState = 0;
    for (std::list<PduRecord>::iterator ptr(pduHandlers.begin()), end(pduHandlers.end());
        ptr != end; ++ptr)
    {
        (*ptr).callable_->onPdu(type, size, data);
        nextState = (*ptr).nextState_;
    }
    if (nextState != NULL)
    {
        protocol_->forceToState(nextState);
    }
}


