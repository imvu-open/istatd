
#if !defined(daemon_RequestInFlight_h)
#define daemon_RequestInFlight_h

#include "HttpServer.h"
#include "StatServer.h"
#include "StatStore.h"
#include <istat/Bucket.h>
#include <istat/Env.h>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <vector>
#include <ostream>
#include <set>

class AcceptEncodingHeader;

class RequestInFlight : public boost::enable_shared_from_this<RequestInFlight>
{
public:
    RequestInFlight(boost::shared_ptr<IHttpRequest> const &req, StatServer *ss, std::string const &filesDir);
    RequestInFlight(boost::shared_ptr<IHttpRequest> const &req, boost::asio::io_service &svc, std::string const &filesDir);
    ~RequestInFlight();
    void go();

protected:
    virtual void complete(int code, char const *type);
    std::stringstream strm_;
    boost::iostreams::filtering_ostream strm_buffer_;
    time_t multiget_start_time_;
    time_t multiget_stop_time_;
    time_t multiget_interval_;
    std::map<std::string, std::string> hdrs_;

private:
    friend class MultiCounterWorker;
    friend class GetSettingsWorker;
    friend class SetSettingsWorker;
    friend void test_RequestInFlight();
    friend void test_choose_encoding();
    friend void func(); // unit test

    boost::shared_ptr<IHttpRequest> req_;
    bool isComplete_;
    StatServer *ss_;
    boost::asio::io_service &svc_;
    std::string filesDir_;
    AcceptEncodingHeader::EncodingSet acceptableEncodings_;


    void do_GET(std::string const &url, std::map<std::string, std::string> &params, std::string const &left);
    void do_POST(std::string const &url, std::map<std::string, std::string> &params);
    void do_UNKNOWN(std::string const &method);

    void on_multigetBody();

    void init();
    void reportError(std::string const &error_description, int error_code);
    void complete(int code);
    void serveFile(std::string const &name);

    void chooseEncoding(AcceptEncodingHeader &codecs);
    void prepareEncoding();
    void listCountersMatching(std::string const &url, boost::shared_ptr<IStatStore> storePtr);
    void createCountersMatchingResponse(std::string const &pattern, std::list<std::pair<std::string, bool> > &results);
    void generateCounterData(std::string const &left, std::map<std::string, std::string> params,
        boost::shared_ptr<IStatStore> storePtr);
    void listAgentsMatching(std::string const &a);

    void generateCounterJson(boost::shared_ptr<IStatCounter> statCounter, time_t startTime, time_t endTime, size_t sampleCount, intmax_t trailing);
    void generateCounterCSV(boost::shared_ptr<IStatCounter> statCounter, time_t startTime, time_t endTime, size_t sampleCount, intmax_t trailing);

    void getSettings(std::string const &settings, std::string const &key);
    void on_storeSettingsBody(std::string const &settings);
};


#endif

