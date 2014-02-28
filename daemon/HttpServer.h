
#if !defined(daemon_HttpServer_h)
#define daemon_HttpServer_h

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/signals.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/detail/atomic_count.hpp>
#include <set>
#include "LoopbackCounter.h"

class HttpServer;
class StatServer;

class IHttpRequest : public boost::noncopyable, public boost::enable_shared_from_this<IHttpRequest>
{
public:
    virtual ~IHttpRequest() {}
    template<typename IterA, typename IterB>
    void reply(int code, std::string const &ctype, IterA hbeg, IterA hend, IterA beg, IterB end)
    {
        std::vector<char> tmp;
        tmp.insert(tmp.end(), beg, end);
        if (tmp.size()) {
            appendReply(&tmp[0], tmp.size());
        }
        tmp.clear();
        tmp.insert(tmp.end(), hbeg, hend);
        std::string xheaders(tmp.begin(), tmp.end());
        doReply(code, ctype, xheaders);
    }
    virtual void doReply(int code, std::string const &ctype, std::string const &xheaders) = 0;
    virtual std::string const &url() const = 0;
    virtual std::string const &method() const = 0;
    virtual void appendReply(char const *data, size_t sz) = 0;
    virtual std::vector<char>::const_iterator bodyBegin() const = 0;
    virtual std::vector<char>::const_iterator bodyEnd() const = 0;
    boost::signal<void ()> onBody_;
    virtual void readBody() = 0;
    virtual std::string const *header(std::string const &key) const = 0;
};

class HttpRequest : public IHttpRequest
{
public:
    HttpRequest(boost::asio::io_service &svc, HttpServer *hs);
    ~HttpRequest();
    inline boost::asio::ip::tcp::socket &socket() { return socket_; }
    void readHeaders();
    void readBody();
    boost::signal<void ()> onHeader_;
    boost::signal<void ()> onError_;
    boost::shared_ptr<std::list<std::string> > headers() const;
    std::string const *header(std::string const &key) const;
    std::string const &url() const { return url_; }
    std::string const &method() const { return method_; }
    std::string const &version() const { return version_; }
    inline std::vector<char>::const_iterator bodyBegin() const { return bodyData_.begin(); }
    inline std::vector<char>::const_iterator bodyEnd() const { return bodyData_.end(); }
private:
    void on_header(boost::system::error_code const &err, size_t xfer);
    void on_body(boost::system::error_code const &err, size_t xfer);
    void on_reply(boost::system::error_code const &err, size_t xfer);
    void parseHeader(std::string const &data);
    void parseMethod(std::string const &data);
    virtual void appendReply(char const *data, size_t size);
    virtual void doReply(int code, std::string const &ctype, std::string const &xheaders);
    void error();
    boost::asio::ip::tcp::socket socket_;
    std::map<std::string, std::string> headers_;
    std::string method_;
    std::string url_;
    std::string version_;
    boost::asio::streambuf header_;
    std::string headerData_;
    size_t headerSize_;
    size_t bodyRead_;
    std::vector<char> bodyData_;
    std::vector<char> reply_;
    HttpServer *hs_;
};

class HttpRequestHolder
{
public:
    HttpRequestHolder(boost::shared_ptr<IHttpRequest> const &p) : p_(p) {}
    boost::shared_ptr<IHttpRequest> p_;
    HttpRequest *operator->() const { return static_cast<HttpRequest *>(p_.get()); }
};

struct HttpServerInfo
{
    HttpServerInfo() :
        port(0),
        current(0),
        currentGauge("http.current", TypeGauge),
        numRequests("http.requests", TypeEvent),
        numErrors("http.errors.server", TypeEvent),
        httpErrors("http.errors.http", TypeEvent)
    {
    }
    HttpServerInfo(HttpServerInfo const &o) :
        port(0),
        current(0),
        currentGauge("http.current", TypeGauge),
        numRequests("http.requests", TypeEvent),
        numErrors("http.errors.server", TypeEvent),
        httpErrors("http.errors.http", TypeEvent)
    {
        memcpy(this, &o, sizeof(*this));
    }
    HttpServerInfo &operator=(HttpServerInfo const &o)
    {
        memcpy(this, &o, sizeof(*this));
        return *this;
    }
    unsigned int port;
    boost::detail::atomic_count current;
    LoopbackCounter currentGauge;
    LoopbackCounter numRequests;
    LoopbackCounter numErrors;
    LoopbackCounter httpErrors;
};

class IHttpServerInfo : public boost::noncopyable
{
public:
    virtual void getInfo(HttpServerInfo &oInfo) = 0;
};

class HttpServer : public IHttpServerInfo
{
public:
    HttpServer(int port, boost::asio::io_service &svc, std::string listen_addr);
    virtual ~HttpServer();
    inline boost::asio::io_service &svc() { return svc_; }
    //  You will see the request *before* headers are parsed. 
    //  Generally, sign up for onHeader_ and perhaps onError_ if you keep a reference.
    boost::signal<void (HttpRequestHolder const &)> onRequest_;
    void getInfo(HttpServerInfo &oInfo);
private:
    friend class HttpRequest;
    void acceptOne();
    void handleAccept(boost::system::error_code const &e, HttpRequestHolder const &req);
    HttpRequest *newHttpRequest();
    HttpServerInfo sInfo_;
    int port_;
    boost::asio::io_service &svc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::deadline_timer timer_;
};

class AcceptEncodingHeader
{
public:
    typedef std::multimap<int, std::string> EncodingWeightMap;
    typedef std::set<std::string> EncodingSet;
    enum HeaderStatus
    {
        StatusMissing = 0,
        StatusEmpty = 1,
        StatusComplete = 2
    };

    AcceptEncodingHeader(EncodingSet &ae, std::string const *header);
    bool should_send_gzip();
    bool should_send_deflate();
private:
    friend void test_parse_encoding();
    bool is_complete(HeaderStatus status) { return status == StatusComplete; }
    HeaderStatus parseAcceptEncoding();
    void performAcceptEncodingRules();

    HeaderStatus updateStatusAndReturn(HeaderStatus status) { return status_ = status;}

    EncodingSet acceptableEncodings_;
    EncodingSet codecs_;
    std::string const *header_;
    EncodingWeightMap weights_;
    EncodingSet seen_;
    HeaderStatus status_;
};

inline HttpRequest *get_pointer(HttpRequestHolder const &hrh) { return static_cast<HttpRequest *>(hrh.p_.get()); }

#endif  //  daemon_HttpServer_h
