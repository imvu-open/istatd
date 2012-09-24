
#if !defined(daemon_PduProtocol_h)
#define daemon_PduProtocol_h

#include <boost/noncopyable.hpp>
#include <stdexcept>
#include <map>
#include <string>
#include <list>

#include <inttypes.h>
#include <assert.h>

class PduProtocolException : public std::runtime_error
{
public:
    PduProtocolException(std::string const &what) : std::runtime_error(what) {}
};

class PduProtocolState;

class IPduCallable : public boost::noncopyable
{
public:
    virtual void onPdu(uint32_t type, size_t size, void const *data) = 0;
};



/* Create a PduProtocol, with the name of the initial state.
 * Create another PduProtocolState simply by asking for it by name.
 * Add expected PDU types to the PduProtocolState, together with
 * a bound function for what to call when the PDU comes in, and 
 * an optional new state to transition to.
 */
class PduProtocol : public IPduCallable
{
public:
    PduProtocol(std::string const &firstStateName);
    PduProtocolState *state(std::string const &name);

    PduProtocolState *curState();
    void onPdu(uint32_t type, size_t size, void const *data);
    void forceToState(PduProtocolState *state);
private:
    friend class PduProtocolState;
    std::map<std::string, PduProtocolState *> states_;
    PduProtocolState *curState_;
};


template<typename PduType>
class PduCallable : public IPduCallable
{
public:
    void onPdu(uint32_t type, size_t size, void const *data)
    {
        assert(type == PduType::Id);
        (*call_)(*(PduType const *)data);
    }
    template<typename Arg> static inline PduCallable<PduType> *make(Arg const &arg)
    {
        return new PduCallable<PduType>(new ObjectInvoker<Arg>(arg));
    }
    static inline PduCallable<PduType> *make(void (*func)(PduType const &))
    {
        return new PduCallable<PduType>(new FunctionInvoker(func));
    }
private:
    struct IInvoker
    {
        virtual void operator()(PduType const &pdu) = 0;
    };
    PduCallable(IInvoker *call) :
        call_(call)
    {
    }
    IInvoker *call_;
    struct FunctionInvoker : public IInvoker
    {
        FunctionInvoker(void (*func)(PduType const &pdu)) :
            func_(func)
        {
        }
        void operator()(PduType const &pdu)
        {
            (*func_)(pdu);
        }
        void (*func_)(PduType const &pdu);
    };
    template<typename Obj>
    struct ObjectInvoker : public IInvoker
    {
        ObjectInvoker(Obj const &obj) :
            obj_(obj)
        {
        }
        void operator()(PduType const &pdu)
        {
            obj_(pdu);
        }
        Obj obj_;
    };
};

class PduProtocolState : public IPduCallable
{
public:
    template<typename PduType, typename Callable>
    void bindPdu(Callable const &c, PduProtocolState *nuState = 0)
    {
        PduRecord pdr;
        pdr.callable_ = PduCallable<PduType>::make(c);
        pdr.nextState_ = nuState;
        std::list<PduRecord> &handlerList = pdus_[PduType::Id];
        size_t sz = handlerList.size();
        //  only the last item can transition to a new state
        assert(sz == 0 || handlerList.back().nextState_ == 0);
        handlerList.push_back(pdr);
    }
    void onPdu(uint32_t type, size_t size, void const *data);
private:
    friend class PduProtocol;
    PduProtocolState(PduProtocol *proto, std::string const &name);
    struct PduRecord
    {
        PduRecord();
        IPduCallable *callable_;
        PduProtocolState *nextState_;
    };
    PduProtocol *protocol_;
    std::string name_;
    std::map<uint32_t, std::list<PduRecord> > pdus_;
};

#endif  //  daemon_PduProtocol_h

