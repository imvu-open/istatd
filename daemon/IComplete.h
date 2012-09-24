
#if !defined(daemon_IComplete_h)
#define daemon_IComplete_h

class IComplete
{
public:
    virtual void on_complete() = 0;
};

template<typename Cls, void (Cls::*Func)()>
class CompleteFunc : public IComplete
{
public:
    CompleteFunc(Cls *cls) : cls_(cls) {}
    Cls *cls_;
    virtual void on_complete()
    {
        (cls_->*Func)();
    }
    IComplete *set(Cls *cls)
    {
        cls_ = cls;
        return this;
    }
};

template<typename Cls, void (Cls::*Func)()>
class HeapCompleteFunc : public CompleteFunc<Cls, Func>
{
public:
    HeapCompleteFunc(Cls *cls) : CompleteFunc<Cls, Func>(cls) {}
    virtual void on_complete()
    {
        CompleteFunc<Cls, Func>::on_complete();
        delete this;
    }
};

template<typename Cls, void (Cls::*Func)()>
static inline IComplete *heap_complete(Cls *cls)
{
    return new HeapCompleteFunc<Cls, Func>(cls);
}


#endif  //  daemon_IComplete_h

