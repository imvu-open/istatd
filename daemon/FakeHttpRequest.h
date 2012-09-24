
#if !defined(daemon_FakeHttpRequest_h)
#define daemon_FakeHttpRequest_h

#include "HttpServer.h"

class FakeHttpRequest : public IHttpRequest {
public:
    FakeHttpRequest(std::string const &u) : url_(u), theCode_(-1), calls_(0) {}

    std::string url_;
    std::string const &url() const { return url_; }
    std::string method_;
    std::string const &method() const { return method_; }

    std::string theReply_;
    int theCode_;
    unsigned int calls_;

    std::string theType_;
    std::string theHdrs_;

    void appendReply(char const *data, size_t size) {
        theReply_.insert(theReply_.end(), data, data+size);
    }

    std::vector<char> theBody_;
    virtual std::vector<char>::const_iterator bodyBegin() const { return theBody_.begin(); }
    virtual std::vector<char>::const_iterator bodyEnd() const { return theBody_.end(); }

    void doReply(int code, std::string const &ctype, std::string const &xheaders) {
        theCode_ = code;
        theType_ = ctype;
        theHdrs_ = xheaders;
        ++calls_;
    }
    void readBody() {
        onBody_();
    }
};


#endif  //  daemon_FakeHttpRequest_h
