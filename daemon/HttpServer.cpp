
#include "HttpServer.h"
#include "StatServer.h"
#include "istat/strfunc.h"
#include "Logs.h"
#include "Debug.h"


#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <ctype.h>
#include <iostream>


using namespace istat;
using boost::asio::ip::tcp;
using namespace boost::asio;


DebugOption debugHttp("http");

HttpServer::HttpServer(int port, boost::asio::io_service &svc, std::string listen_addr) :
    port_(port),
    svc_(svc),
    acceptor_(svc),
    timer_(svc)
{
    if (port_ != 0)
    {
        sInfo_.port = port_;
        tcp::resolver resolver(svc);
        acceptor_.open(tcp::v4());
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        if (listen_addr.length() > 0) {
            acceptor_.bind(tcp::endpoint(ip::address::from_string(listen_addr.c_str()), port));
        } else {
            acceptor_.bind(tcp::endpoint(tcp::v4(), port));
        }
        acceptor_.listen();
        acceptOne();
    }
}

HttpServer::~HttpServer()
{
}

void HttpServer::getInfo(HttpServerInfo &oInfo)
{
    oInfo = sInfo_;
}

void HttpServer::acceptOne()
{
    LogSpam << "HttpService::acceptOne()";
    boost::shared_ptr<IHttpRequest> sptr = boost::make_shared<HttpRequest>(boost::ref(svc()), this);
    HttpRequestHolder request(sptr);
    acceptor_.async_accept(request->socket(),
        boost::bind(&HttpServer::handleAccept, this, placeholders::error, request));
}

void HttpServer::handleAccept(boost::system::error_code const &e, HttpRequestHolder const &req)
{
    LogSpam << "HttpService::handleAccept()";
    ++sInfo_.numRequests;
    if (!e)
    {
        if (debugHttp.enabled())
        {
            LogDebug << "http request";
        }
        ++sInfo_.current;
        sInfo_.currentGauge.value((int32_t)sInfo_.current);
        onRequest_(req);
        req->readHeaders();
        acceptOne();
        return;
    }
    ++sInfo_.numErrors;
    LogError << "Error accepting a HTTP request: " << e;
    timer_.expires_from_now(boost::posix_time::seconds(1));
    timer_.async_wait(boost::bind(&HttpServer::acceptOne, this));
}

HttpRequest::HttpRequest(boost::asio::io_service &svc, HttpServer *hs) :
    socket_(svc),
    hs_(hs)
{
}

HttpRequest::~HttpRequest()
{
}

void HttpRequest::readHeaders()
{
    LogSpam << "HttpRequest::readHeaders()";
    if (headerData_.size() > 0)
    {
        throw std::runtime_error("Can't readHeaders() twice!");
    }
    boost::asio::async_read_until(socket_, header_, "\r\n\r\n", 
        boost::bind(&HttpRequest::on_header, HttpRequestHolder(shared_from_this()),
        placeholders::error, placeholders::bytes_transferred));
}

void HttpRequest::readBody()
{
    LogSpam << "HttpRequest::readBody()";
    std::string const *cl = header("Content-Length");
    if (!cl)
    {
        throw std::runtime_error("Can't read body without content-length");
    }
    size_t len = boost::lexical_cast<size_t>(*cl);
    bodyData_.resize(len);
    bodyRead_ = 0;
    if (headerSize_ < headerData_.size() && len > 0)
    {
        size_t toCopy = headerData_.size() - headerSize_;
        if (toCopy > len)
        {
            toCopy = len;
        }
        if (debugHttp.enabled())
        {
            LogDebug << "http toCopy" << toCopy;
        }
        std::copy(&headerData_[headerSize_], &headerData_[headerSize_] + toCopy, &bodyData_[0]);
        bodyRead_ = toCopy;
    }
    if (debugHttp.enabled())
    {
        LogDebug << "http readBody" << len << "bytes";
    }
    try
    {
        size_t toRead = bodyData_.size() - bodyRead_;
        if (toRead == 0)
        {
            if (debugHttp.enabled())
            {
                LogDebug << "http toRead complete";
            }
            on_body(boost::system::errc::make_error_code(boost::system::errc::success), toRead);
            return;
        }
        if (!socket_.is_open())
        {
            throw std::runtime_error("Socket is closed inside readBody()");
        }
        if (debugHttp.enabled())
        {
            LogDebug << "http queue read" << toRead;
        }
        boost::asio::async_read(socket_, boost::asio::buffer(&bodyData_[bodyRead_], toRead), boost::asio::transfer_at_least(toRead),
            boost::bind(&HttpRequest::on_body, HttpRequestHolder(shared_from_this()), placeholders::error, placeholders::bytes_transferred));
    }
    catch (std::exception const &x)
    {
        LogWarning << "exception calling async_read() in readBody():" << x.what();
        throw x;
    }
}

void HttpRequest::on_header(boost::system::error_code const &err, size_t xfer)
{
    LogSpam << "HttpRequest::on_header()";
    if (!!err)
    {
        LogWarning << "HttpRequest::on_header(): " << err;
        error();
        return;
    }
    headerSize_ = xfer;
    std::ostringstream is;
    is << &header_;
    headerData_ = is.str();
    //  parse header
    bool first = true;
    size_t b = 0;
    for (size_t p = 3, n = headerData_.size(); p != n-2; ++p)
    {
        if (headerData_[p] == '\n' && headerData_[p-1] == '\r' && headerData_[p+1] != ' ')
        {
            if (headerData_[p+1] == '\r' && headerData_[p+2] == '\n')
            {
                //found end of headers ... record the final header if it exists
                if (p > b) {
                    parseHeader(std::string(&headerData_[b], &headerData_[p-1]));
                }
                headerSize_ = p + 3;
                break;
            }
            else if (first)
            {
                parseMethod(std::string(&headerData_[b], &headerData_[p-1]));
                first = false;
            }
            else
            {
                parseHeader(std::string(&headerData_[b], &headerData_[p-1]));
            }
            b = p+1;
        }
    }
    if (!version_.size() || !headers_.size() || !headerSize_)
    {
        LogWarning << "Mal-formed HTTP request: method" << method_ << "url" << url_;
        error();
        return;
    }
    //  fire the event
    onHeader_();
    disconnect_all_slots(onHeader_);
}

void HttpRequest::on_body(boost::system::error_code const &err, size_t xfer)
{
    LogSpam << "HttpRequest::on_body()";
    if (!!err)
    {
        LogWarning << "HttpRequest::on_body(): " << err;
        if ((xfer > 0) &&
            (err.category() != boost::asio::error::get_misc_category() ||
                err.value() != boost::asio::error::eof))
        {
            error();
            return;
        }
    }
    if (debugHttp.enabled())
    {
        LogDebug << "http on_body" << xfer << "bytes";
    }
    //  got the body!
    onBody_();
    disconnect_all_slots(onBody_);
    disconnect_all_slots(onError_);
}

boost::shared_ptr<std::list<std::string> > HttpRequest::headers() const
{
    boost::shared_ptr<std::list<std::string> > ret = boost::make_shared<std::list<std::string> >();
    for (std::map<std::string, std::string>::const_iterator ptr(headers_.begin()), end(headers_.end());
        ptr != end; ++ptr)
    {
        ret->push_back((*ptr).first);
    }
    return ret;
}

std::string const *HttpRequest::header(std::string const &key) const
{
    std::string copy(key);
    munge(copy);
    std::transform(copy.begin(), copy.end(), copy.begin(), ::tolower);
    std::map<std::string, std::string>::const_iterator ptr(headers_.find(copy));
    if (ptr == headers_.end())
    {
        return 0;
    }
    return &(*ptr).second;
}

void HttpRequest::parseMethod(std::string const &data)
{
    std::string temp;
    split(data, ' ', method_, temp);
    trim(temp);
    split(temp, ' ', url_, version_);
    if (debugHttp.enabled())
    {
        LogDebug << "http method" << method_ << "url" << url_ << "version" << version_;
    }
}

void HttpRequest::parseHeader(std::string const &data)
{
    std::string left, right;
    split(data, ':', left, right);
    munge(left);
    trim(right);
    std::transform(left.begin(), left.end(), left.begin(), ::tolower);
    headers_[left] += right;
    LogSpam << "HttpRequest::parseHeader Appending to header " << left << "with data" << right;
}

void HttpRequest::error()
{
    LogSpam << "HttpRequest::error()";
    ++hs_->sInfo_.numErrors;
    onError_();
    disconnect_all_slots(onError_);
    disconnect_all_slots(onBody_);
    disconnect_all_slots(onHeader_);
}

void HttpRequest::appendReply(char const *data, size_t size)
{
    reply_.insert(reply_.end(), data, data+size);
}

void HttpRequest::doReply(int code, std::string const &ctype, std::string const &xheaders)
{
    if (debugHttp.enabled())
    {
        LogDebug << "http reply" << code << ctype;
    }
    else
    {
        LogSpam << "HttpRequest::doReply()";
    }
    if (code >= 400)
    {
        ++hs_->sInfo_.httpErrors;
    }
    std::string headers;

    headers += "HTTP/1.1 ";
    headers += boost::lexical_cast<std::string>(code);
    headers += " (that's a status code)";
    headers += "\r\n";

    headers += "Content-Type: ";
    headers += ctype;
    headers += "\r\n";

    if ((method() == "OPTIONS")
        || ((method() == "POST") &&
                ((url() == "/*") || (url() == "/%2A") || (url() == "/%2a")))
        || ((method() == "GET") &&
                (url().substr(0, 7) == "/files/"))
       ) {
        std::string const *refer = this->header("referer");
        std::string origin("*");
        if (refer) {
            int nth = 0;
            for (std::string::const_iterator ptr(refer->begin()), end(refer->end());
                    ptr != end; ++ptr) {
                //  The third slash terminates the origin
                //  https://foo.bar:port/ ...
                if (*ptr == '/') {
                    ++nth;
                    if (nth == 3) {
                        origin = std::string(refer->begin(), ptr);
                        break;
                    }
                }
            }
        }
        headers += "Access-Control-Allow-Origin: " + origin + "\r\n";
    }
    if (reply_.size()) {
        headers += "Content-Length: ";
        headers += boost::lexical_cast<std::string>(reply_.size());
        headers += "\r\n";
    }

    headers += "Connection: close";
    headers += "\r\n";

    headers += xheaders;
    if (debugHttp.enabled() && xheaders.size())
    {
        LogDebug << "http xheaders" << xheaders;
    }

    headers += "\r\n";
    reply_.insert(reply_.begin(), headers.begin(), headers.end());
    boost::asio::async_write(socket_, boost::asio::buffer(&reply_[0], reply_.size()), boost::asio::transfer_all(),
        boost::bind(&HttpRequest::on_reply, HttpRequestHolder(shared_from_this()), placeholders::error, placeholders::bytes_transferred));
    disconnect_all_slots(onHeader_);
    disconnect_all_slots(onBody_);
}

void HttpRequest::on_reply(boost::system::error_code const &err, size_t xfer)
{
    if (debugHttp.enabled())
    {
        LogDebug << "http on_reply() complete" << err;
    }
    else
    {
        LogSpam << "HttpRequest::on_reply()";
    }
    if (!!err)
    {
        ++hs_->sInfo_.numErrors;
    }
    assert(hs_->sInfo_.current > 0);
    --hs_->sInfo_.current;
    hs_->sInfo_.currentGauge.value((int32_t)hs_->sInfo_.current);
    //  this should soon go away, as the stack reference will go away!
    socket_.close();
    disconnect_all_slots(onError_);
}

bool AcceptEncodingHeader::disallow_compressed_responses = false;

AcceptEncodingHeader::AcceptEncodingHeader(EncodingSet &ae, std::string const *header) :
    acceptableEncodings_(ae),
    header_(header)
{
    if(AcceptEncodingHeader::is_complete(parseAcceptEncoding()))
    {
        performAcceptEncodingRules();
    }
}

AcceptEncodingHeader::HeaderStatus AcceptEncodingHeader::parseAcceptEncoding()
{
    LogSpam << "AcceptEncodingHeader:parseAcceptEncoding()";
    if (AcceptEncodingHeader::disallow_compressed_responses)
    {
        return updateStatusAndReturn(StatusDisabled);
    }
    if (!header_)
    {
        return updateStatusAndReturn(StatusMissing);
    }
    if (header_->empty())
    {
        return updateStatusAndReturn(StatusEmpty);
    }

    const int default_val = 100;
    std::vector<std::string> content_encodings;
    explode(*header_, ',', content_encodings);
    for(std::vector<std::string>::iterator ptr(content_encodings.begin()), end(content_encodings.end());
            ptr != end; ++ptr)
    {
        int val = default_val;
        std::string type, qvalue, blackhole;
        split(*ptr, ';', type, qvalue);
        if (!qvalue.empty())
        {
            split(qvalue, '=', blackhole, qvalue);
            trim(qvalue);
            val = (int)(atof(qvalue.c_str()) * 100);
        }
        trim(type);
        weights_.insert(EncodingWeightMap::value_type(val, type));
        seen_.insert(type);
    }
    return updateStatusAndReturn(StatusComplete);
}

void AcceptEncodingHeader::performAcceptEncodingRules()
{
    LogSpam << "AcceptEncodingHeader:performAcceptEncodingRules()";
    for(EncodingWeightMap::iterator ptr(weights_.begin()), end(weights_.end()); ptr != end; ++ptr)
    {
        if((*ptr).second == "*")
        {
            for(EncodingSet::iterator it(acceptableEncodings_.begin()), eit(acceptableEncodings_.end()); it != eit; ++it)
            {
                if (seen_.find((*it)) == seen_.end())
                {
                    (*ptr).second = (*it);
                }
            }
        }

    }
    std::pair< EncodingWeightMap::iterator, EncodingWeightMap::iterator> ret = weights_.equal_range((*weights_.rbegin()).first);
    for( EncodingWeightMap::iterator it = ret.first; it != ret.second; ++it)
    {
        if ((*it).first != 0)
        {
            codecs_.insert((*it).second);
        }
    }
}

bool AcceptEncodingHeader::should_send_gzip()
{
    return codecs_.find("gzip") != codecs_.end();
}

bool AcceptEncodingHeader::should_send_deflate()
{
    return codecs_.find("deflate") != codecs_.end();
}

