#include "RequestInFlight.h"
#include "Debug.h"
#include "Logs.h"
#include "Settings.h"
#include "IComplete.h"
#include <istat/strfunc.h>
#include <istat/StatFile.h>
#include <istat/Atomic.h>
#include <istat/istattime.h>
#include <json/json.h>

#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/asio/strand.hpp>
#include <boost/foreach.hpp>


using namespace istat;

extern DebugOption debugHttp;

RequestInFlight::RequestInFlight(boost::shared_ptr<IHttpRequest> const &req,
        StatServer *ss, std::string const &filesDir) :
    req_(req), isComplete_(false), ss_(ss), svc_(ss->service()), filesDir_(filesDir)
{
    init();
}

RequestInFlight::RequestInFlight(boost::shared_ptr<IHttpRequest> const &req,
        boost::asio::io_service &svc, std::string const &filesDir) :
    req_(req), isComplete_(false), ss_(0), svc_(svc), filesDir_(filesDir)
{
    init();
}

RequestInFlight::~RequestInFlight() {
}

void RequestInFlight::init()
{
    strm_.precision(4);
}

void RequestInFlight::reportError(std::string const &error_description,
        int error_code = 400)
{
    Json::Value root;
    root["error"] = error_description;
    strm_ << Json::FastWriter().write(root);
    complete(error_code);
}

void RequestInFlight::go()
{
    LogDebug << "RequestInFlight:go()";
    try {
        std::string const &url(req_->url());
        boost::shared_ptr<IStatStore> storePtr(ss_->store());
        if (!storePtr) {
            LogWarning << "Store not initialized";
            reportError("Store not initialized", 503);
            return;
        }
        std::map<std::string, std::string> params;

        std::string left, right;
        split(url, '?', left, right);

        if (left.empty()) {
            reportError("Malformed url", 400);
            return;
        }

        left = left.substr(1, std::string::npos);
        LogSpam << "left: " << left;

        querystring(right, params);

        if (req_->method() == "GET")
        {
            do_GET(url, params, left);
        }
        else if (req_->method() == "POST")
        {
            do_POST(url, params);
        }
        else
        {
            do_UNKNOWN(req_->method());
        }
    }
    catch (std::runtime_error const &re) {
        LogDebug << std::string("HTTP exception: ") + re.what(); 
        reportError(std::string("HTTP exception: ") + re.what());
    }
}

void RequestInFlight::do_UNKNOWN(std::string const &method)
{
    if (debugHttp.enabled())
    {
        LogNotice << "http do_UNKNOWN" << method;
    }
    reportError("Bad method", 405);
}

void RequestInFlight::do_GET(std::string const &url, std::map<std::string, std::string> &params, std::string const &left)
{
    boost::shared_ptr<IStatStore> storePtr(ss_->store());
    std::map<std::string, std::string>::iterator param_val;

    if (debugHttp.enabled()) 
    {
        LogNotice << "http do_GET" << url;
    }

    if (url == "/") {
        serveFile("index.html");
    }
    else if (url.substr(0, 7) == "/files/") {
        serveFile(url.substr(7));
    }
    else if ((params.size() == 0) && url == "favicon.ico") {
        serveFile("/files/favicon.ico");
    }
    else if ((param_val = params.find("f")) != params.end()) {
        serveFile(url.substr(4));
    }
    else if ((param_val = params.find("a")) != params.end()) {
        listAgentsMatching(param_val->second);
    }
    else if ((param_val = params.find("q")) != params.end()) {
        listCountersMatching(param_val->second, storePtr);
    }
    else if ((param_val = params.find("s")) != params.end()) {
        std::string setting(param_val->second);
        std::string key = "*";
        param_val = params.find("sk");
        if (param_val != params.end())
        {
            key = param_val->second;
        }
        getSettings(setting, key);
    }
    else {
        generateCounterData(left, params, storePtr);
    }
}

void RequestInFlight::do_POST(std::string const &url, std::map<std::string, std::string> &params)
{
    if (debugHttp.enabled()) 
    {
        LogNotice << "http do_POST" << url;
    }
    std::map<std::string, std::string>::iterator pval;

    if (url == "/*")
    {
        req_->onBody_.connect(boost::bind(&RequestInFlight::on_multigetBody, shared_from_this()));
        req_->readBody();
    }
    else if ((pval = params.find("s")) != params.end())
    {
        req_->onBody_.connect(boost::bind(&RequestInFlight::on_storeSettingsBody, shared_from_this(), pval->second));
        req_->readBody();
    }
    else
    {
        if (debugHttp.enabled())
        {
            LogNotice << "http POST to bad URL" << url;
        }
        reportError("No such POST target", 404);
    }
}


class MultiCounterWorker
{
public:
    MultiCounterWorker(time_t start, time_t end, size_t maxSamples,
        boost::shared_ptr<RequestInFlight> const &req) :
        start_(start),
        end_(end),
        maxSamples_(maxSamples),
        req_(req),
        num(0)
    {
    }
    time_t start_;
    time_t end_;
    size_t maxSamples_;
    boost::shared_ptr<RequestInFlight> req_;
    std::map<boost::shared_ptr<IStatCounter>, std::pair<boost::asio::strand *, std::string> > data;
    int64_t num;
    lock strmLock;
    void add(boost::shared_ptr<IStatCounter> const &ctr, boost::asio::strand *strand, std::string const &name)
    {
        data[ctr] = std::pair<boost::asio::strand *, std::string>(strand, name);
    }
    void go()
    {
        if (!data.size())
        {
            req_->strm_ << "}";
            req_->complete(200, "application/json");
            delete this;
        }
        else
        {
            num = data.size();
            for (std::map<boost::shared_ptr<IStatCounter>, std::pair<boost::asio::strand *, std::string> >::iterator
                ptr(data.begin()), end(data.end()); ptr != end; ++ptr)
            {
                (*ptr).second.first->io_service().post((*ptr).second.first->wrap(
                    boost::bind(&MultiCounterWorker::workOne, this, (*ptr).first, (*ptr).second.second)));
            }
        }
    }
    void workOne(boost::shared_ptr<IStatCounter> &counter, std::string const &name)
    {
        {
            grab aholdOf(strmLock);
            std::vector<istat::Bucket> buckets;
            time_t normalized_start;
            time_t normalized_end;

            time_t interval;
            counter->select(start_, end_, buckets, normalized_start, normalized_end, interval, maxSamples_);

            // this is crude, but effective.
            // all these values should be the same for all counters in the group
            req_->multiget_start_time_ = normalized_start;
            req_->multiget_stop_time_ = normalized_end;
            req_->multiget_interval_ = interval;

            if (req_->strm_.str().length() > 1)
            {
                req_->strm_ << ",";
            }
            req_->strm_ << "\"" << name << "\":{" 
                        << "\"start\":" << normalized_start
                        << ",\"end\":" << normalized_end
                        << ",\"interval\":" << interval
                        << ",\"data\":[";
            bool first = true;
            for (std::vector<Bucket>::iterator ptr(buckets.begin()), end(buckets.end());
                ptr != end; ++ptr)
            {
                istat::Bucket const &b = *ptr;
                req_->strm_ << (first ? "{" : ",{");
                req_->strm_ << "\"time\":" << b.time();
                req_->strm_ << ",\"count\":" << b.count();
                req_->strm_ << ",\"min\":" << b.min();
                req_->strm_ << ",\"max\":" << b.max();
                req_->strm_ << ",\"sum\":" << b.sum();
                req_->strm_ << ",\"sumsq\":" << b.sumSq();
                req_->strm_ << ",\"avg\":" << b.avg();
                req_->strm_ << ",\"sdev\":" << b.sdev();
                req_->strm_ << "}";
                first = false;
            }
            req_->strm_ << "]}";
        }
        if (0 == istat::atomic_add(&num, -1))
        {
            if (req_->strm_.str().length() > 1)
            {
                req_->strm_ << ",";
            }
            req_->strm_ << "\"start\":" << req_->multiget_start_time_;
            req_->strm_ << ",\"stop\":"  << req_->multiget_stop_time_;
            req_->strm_ << ",\"interval\":" << req_->multiget_interval_;
            req_->strm_ << "}";
            req_->complete(200, "application/json");
            delete this;
        }
    }
};

void RequestInFlight::on_multigetBody()
{
    boost::shared_ptr<IStatStore> statStore(ss_->store());
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(&*req_->bodyBegin(), &*req_->bodyEnd(), root, false))
    {
        reportError(reader.getFormattedErrorMessages(), 400);
        return;
    }
    try
    {
        time_t start = root["start"].asInt64();
        time_t stop = root["stop"].asInt64();
        size_t maxSamples = root["maxSamples"].asInt();
        if (start < 0) {
            reportError("Invalid 'start' parameter", 400);
            return;
        }
        if (stop < 0) {
            reportError("Invalid 'stop' parameter", 400);
            return;
        }
        if (stop < start) {
            reportError("Start must be before stop", 400);
            return;
        }
        if (maxSamples < 0) {
            reportError("Invalid maxSamples parameter", 400);
            return;
        }
        else if (maxSamples == 0) {
            maxSamples = 100;
        }

        Json::Value keys = root["keys"];
        if (keys.isNull() || !keys.isArray())
        {
            reportError("Missing 'key' array of keys", 400);
            return;
        }
        
        strm_ << "{";
        MultiCounterWorker *mcw = new MultiCounterWorker(start, stop, maxSamples, shared_from_this());
        for (int i = 0, n = keys.size(); i != n; ++i)
        {
            std::string name(keys[i].asString());
            boost::shared_ptr<IStatCounter> counter;
            boost::asio::strand *strand = 0;
            statStore->find(name, counter, strand);
            if (!counter)
            {
                LogSpam << "Counter" << name << "not found";
                if (strm_.str().length() > 1)
                {
                    strm_ << ", ";
                }

                strm_ << "\"" << name << "\":\"Not found.\"";
            }
            else
            {
                mcw->add(counter, strand, name);
            }
        }
        mcw->go();
    }
    catch (std::exception const &x)
    {
        reportError(std::string("exception: ") + x.what(), 400);
    }
}


void RequestInFlight::complete(int code, char const *type)
{
    LogDebug << "RequestInFlight::complete(" << code << type << ")";
    if (!isComplete_) {
        isComplete_ = true;
        std::string reply(strm_.str());
        std::stringstream hdrs;
        //  dumb commas in BOOST_FOREACH
        //BOOST_FOREACH(std::pair<std::string const, std::string> &kv, hdrs_)
        for (std::map<std::string, std::string>::iterator kv(hdrs_.begin()), end(hdrs_.end());
            kv != end; ++kv)
        {
            hdrs << (*kv).first;
            hdrs << ": ";
            hdrs << (*kv).second;
            hdrs << "\r\n";
        }
        std::string headers(hdrs.str());
        req_->reply(code, type, headers.begin(), headers.end(), reply.begin(), reply.end());
    } else {
        LogWarning << "Double completion of request: " << req_->url();
        assert(false);
    }
}

void RequestInFlight::complete(int code)
{
    complete(code, "application/json");
}

void RequestInFlight::serveFile(std::string const &name)
{
    std::string x(name);
    //  don't allow relative paths! this avoids security problems
    std::replace(x.begin(), x.end(), '/', '.');
    x = filesDir_ + "/" + x;
    struct stat st;
    if (stat(x.c_str(), &st) < 0)
    {
        LogWarning << "serveFile(" << x << "): not found";
        complete(404);
        return;
    }
    time_t modTime = st.st_mtime;
    int64_t fileSize = st.st_size;
    time_t now;
    time_t expires = now;
    bool cacheAll = false;
    time(&now);
    std::string etag(boost::lexical_cast<std::string>(modTime) + "_" + boost::lexical_cast<std::string>(fileSize));
    //  todo: maybe do some if-none-match optimization here
    hdrs_["Etag"] = etag;
    hdrs_["Date"] = http_date(now);
    hdrs_["Modified"] = http_date(modTime);

    //  figure out caching
    if (modTime >= now - 3600)
    {
        //  a file changed in the last hour will likely change again
        expires = now;
    }
    else if (modTime >= now - 24 * 3600)
    {
        //  a file changed in the last day caches for a minute
        expires = now + 60;
    }
    else
    {
        //  a file changed longer ago than that caches for an hour
        expires = now + 3600;
        cacheAll = true;
    }
    //  right now, only cache images and the jquery data file, plus files older than a day
    if (cacheAll ||
        (x.size() > 4 && x.substr(x.size() - 4) == ".png") ||
        (x.size() > 7 && x.substr(x.size() - 7) == ".min.js"))
    {
        hdrs_["Cache-Control"] = "public; max-age=3600";
        hdrs_["Expires"] = http_date(expires);
    }
    //  this is inelegant and blocking, but also not a common occurrence (one would hope)
    FILE *f = fopen(x.c_str(), "rb");
    if (!f)
    {
        LogWarning << "serveFile(" << x << "): cannot open";
        complete(404);
        return;
    }
    fseek(f, 0, 2);
    long l = ftell(f);
    if (l > 1024 * 1024) {
        fclose(f);
        LogWarning << "serveFile(" << x << "): too large";
        complete(500);
        return;
    }
    fseek(f, 0, 0);
    if (l > 0) {
        std::vector<char> ch;
        ch.resize(l);
        size_t n = fread(&ch[0], 1, l, f);
        strm_.write(&ch[0], n);
    }
    fclose(f);
    LogNotice << "serveFile(" << x << "): " << l << " bytes.";
    complete(200, static_file_type(name));
}

void RequestInFlight::listCountersMatching(std::string const &pattern,
        boost::shared_ptr<IStatStore> storePtr)
{
    LogDebug << "RequestInFlight::listCountersMatching(" << pattern << ") - start";
    std::list<std::pair<std::string, bool> > results;
    storePtr->listMatchingCounters(pattern, results);

    strm_ << "{\"pattern\":\"" << js_quote(pattern) << "\",\"matching_names\":[";
    bool comma = false;
    for (std::list<std::pair<std::string, bool> >::iterator
        ptr(results.begin()), end(results.end());
        ptr != end;
        ++ptr)
    {
        if (comma)
        {
            strm_ << ",";
        }
        else
        {
            comma = true;
        }
        strm_ << "{\"is_leaf\":" << ((*ptr).second ? "true" : "false") << 
            ",\"name\":\"" << js_quote((*ptr).first) << "\"}";
    }
    strm_ << "]}";
    LogSpam << "RequestInFlight::listCountersMatching(" << pattern << ") - done";
    complete(200, "application/json");
}


void RequestInFlight::listAgentsMatching(std::string const &pattern)
{
    std::vector<MetaInfo> agents;
    ss_->getConnected(agents);
    //  always list all agents -- ignore pattern
    strm_ << "{";
    strm_ << "\"count\":" << agents.size() << ",";
    strm_ << "\"agents\":[";
    bool comma = false;
    time_t now;
    istat::istattime(&now);
    for (std::vector<MetaInfo>::iterator ptr(agents.begin()), end(agents.end());
        ptr != end; ++ptr)
    {
        if (comma) 
        {
            strm_ << ",";
        }
        else
        {
            comma = true;
        }
        strm_ << "{";
        for (std::tr1::unordered_map<std::string, std::string>::iterator
            iptr((*ptr).info_.begin()), iend((*ptr).info_.end());
            iptr != iend;
            ++iptr)
        {
            strm_ << "\"" << js_quote((*iptr).first) << "\":\"" << js_quote((*iptr).second) << "\",";
        }
        strm_ << "\"_online\":" << ((*ptr).online_ ? "true" : "false") << ",";
        strm_ << "\"_connected\":\"" << iso_8601_datetime((*ptr).connected_) << "\",";
        strm_ << "\"_idle\":" << (now - (*ptr).activity_) << "}";
    }
    strm_ << "]}";
    complete(200, "application/json");
}

static bool getValue(std::map<std::string, std::string> const &params,
        char const *name, intmax_t &oVal) {
    std::map<std::string, std::string>::const_iterator ptr = params.find(name);
    if (ptr == params.end()) {
        return false;
    }
    try {
        oVal = boost::lexical_cast<intmax_t>(ptr->second);
    } catch (boost::bad_lexical_cast const &) {
        throw std::runtime_error("Bad integer argument " + std::string(name)
                + ": " + ptr->second);
    }
    return true;
}

static bool getValue(std::map<std::string, std::string> const &params,
        char const *name, std::string &oVal) {
    std::map<std::string, std::string>::const_iterator ptr = params.find(name);
    if (ptr == params.end()) {
        return false;
    }
    oVal = ptr->second;
    return true;
}

void RequestInFlight::generateCounterData(
        std::string const &cname,
        std::map<std::string, std::string> params,
        boost::shared_ptr<IStatStore> storePtr)
{

    intmax_t startTime = 0;
    intmax_t endTime = 0;
    intmax_t sampleCount = 1000;
    std::string format = "json";
    getValue(params, "start", startTime);
    getValue(params, "end", endTime);
    getValue(params, "samples", sampleCount);
    getValue(params, "format", format);
    if (startTime < 0) {
        throw std::runtime_error("Invalid start parameter");
    }
    if (endTime < 0) {
        throw std::runtime_error("Invalid end parameter");
    }
    if (sampleCount < 0) {
        throw std::runtime_error("Invalid samples parameter");
    }

    if ((startTime == 0) && (endTime == 0)) {
        endTime = istat::istattime(0);
        startTime = endTime - 900;
    }
    else {
        if (endTime && !startTime) {
            startTime = endTime - 900;
        }

        if (endTime < startTime) {
            if (endTime) {
                throw std::runtime_error("end must be greater than start!");
            }
            else {
                endTime = startTime + 900;
            }
        }
    }

    boost::shared_ptr<IStatCounter> statCounter;
    boost::asio::strand *strand = 0;

    storePtr->find(cname, statCounter, strand);

    if (!statCounter) {
        LogNotice << "Unknown counter: '" << cname << "'";
        reportError("No such counter " + cname, 404);
        return;
    }
    if(format == "json") {
        svc_.post(strand->wrap(
            boost::bind(&RequestInFlight::generateCounterJson,
                shared_from_this(), statCounter, startTime, endTime, sampleCount)));
    } else if(format == "csv") {
        svc_.post(strand->wrap(
            boost::bind(&RequestInFlight::generateCounterCSV,
                shared_from_this(), statCounter, startTime, endTime, sampleCount)));
    } else {
        throw std::runtime_error("unrecognized format. must be 'json' or 'csv'.");
    }
}

void RequestInFlight::generateCounterJson(boost::shared_ptr<IStatCounter> statCounter, time_t startTime, time_t endTime, size_t sampleCount)
{
    std::vector<istat::Bucket> buckets;
    time_t intervalTime = 10;
    time_t normalizedStart;
    time_t normalizedEnd;
    
    try
    {

        statCounter->select(startTime, endTime, buckets, normalizedStart, normalizedEnd, intervalTime, sampleCount);

        Json::Value jsonRoot(Json::objectValue);
        
        jsonRoot["start"] = static_cast<Json::UInt64>(normalizedStart);
        jsonRoot["end"] = static_cast<Json::UInt64>(normalizedEnd);
        jsonRoot["interval"] = static_cast<Json::UInt64>(intervalTime);

        Json::Value &jsonBuckets = jsonRoot["buckets"] = Json::Value(Json::arrayValue);

        for (std::vector<istat::Bucket>::iterator ptr(buckets.begin()), end(buckets.end()); ptr != end; ++ptr) {
            istat::Bucket b = *ptr;

            Json::Value jsonBucket(Json::objectValue);
            jsonBucket["time"] = static_cast<Json::UInt64> (b.time()); //time_t -> uint64

            Json::Value &jsonBucketData = jsonBucket["data"] = Json::Value(Json::objectValue);

            jsonBucketData["count"] = b.count();
            jsonBucketData["min"] = b.min();
            jsonBucketData["max"] = b.max();
            jsonBucketData["sum"] = b.sum();
            jsonBucketData["sumsq"] = b.sumSq();
            jsonBucketData["avg"] = b.avg();
            jsonBucketData["sdev"] = b.sdev();

            jsonBuckets.append(jsonBucket);
        }

        strm_ << Json::FastWriter().write(jsonRoot);
        complete(200, "application/json");
    } catch (std::runtime_error &re) {
        reportError(std::string("HTTP JSON exception:") + re.what());
    }
}

void RequestInFlight::generateCounterCSV(boost::shared_ptr<IStatCounter> statCounter, time_t startTime, time_t endTime, size_t sampleCount)
{
    std::vector<istat::Bucket> buckets;
    time_t intervalTime = 10;
    time_t normalizedStart;
    time_t normalizedEnd;
    try
    {
        statCounter->select(startTime, endTime, buckets, normalizedStart, normalizedEnd, intervalTime, sampleCount);

        strm_ << "ISO Time,Unix Timestamp,Sample Count,Minimum,Maximum,Sum,Sum Squared,Average,Standard Deviation\r\n";

        for (std::vector<istat::Bucket>::iterator ptr(buckets.begin()), end(buckets.end()); ptr != end; ++ptr) {
            istat::Bucket b = *ptr;

            strm_ << b.dateStr()
                << "," << b.time()
                << "," << b.count()
                << "," << b.min()
                << "," << b.max()
                << "," << b.sum()
                << "," << b.sumSq()
                << "," << b.avg()
                << "," << b.sdev()
                << "\r\n";
        }
        complete(200, "text/csv");
    } catch (std::runtime_error &re) {
        reportError(std::string("HTTP CSV exception:") + re.what());
    }
}

class GetSettingsWorker
{
public:
    GetSettingsWorker(boost::shared_ptr<RequestInFlight> const &rif, ISettingsFactory *sfac) :
        rif_(rif),
        sfac_(sfac)
    {
    }

    boost::shared_ptr<RequestInFlight> rif_;
    ISettingsFactory *sfac_;
    std::string settings_;
    std::string key_;
    boost::shared_ptr<ISettings> set_;
    std::vector<std::string> keys_;
    std::map<std::string, std::string> ret_;
    bool wasSet_;

    void go(std::string const &settings, std::string const &key)
    {
        settings_ = settings;
        key_ = key;
        sfac_->open(settings, false, set_, heap_complete<GetSettingsWorker, &GetSettingsWorker::go_complete>(this));
    }

    void go_complete()
    {
        LogSpam << "GetSettingsWorker::go_complete()";
        if (!set_)
        {
            rif_->reportError("no such settings", 404);
            delete this;
            return;
        }
        set_->match(key_, keys_);
        match_complete();
    }

    void match_complete()
    {
        LogSpam << "GetSettingsWorker::match_complete()";
        bool hasKeys = false;
        while (keys_.size())
        {
            hasKeys = true;
            std::string key(keys_.back());
            keys_.pop_back();
            set_->get(key, ret_[key], wasSet_, "");
        }
        if (debugHttp.enabled())
        {
            LogNotice << "GetSettingsWorker completing with" << ret_.size() << "keys.";
        }
        Json::Value root;
        for (std::map<std::string, std::string>::iterator ptr(ret_.begin()), end(ret_.end());
            ptr != end; ++ptr)
        {
            root[(*ptr).first] = (*ptr).second;
        }
        rif_->strm_ << Json::FastWriter().write(root);
        rif_->complete(hasKeys ? 200 : 404, "application/json");
        delete this;
    }
};

template<typename T>
inline static std::string lc(T t)
{
    return boost::lexical_cast<std::string>(t);
}

#define JSON(t,v) \
  if (val.t()) { return v; }

inline static std::string json_string(Json::Value const &val)
{
    JSON(isNull,    "");
    JSON(isBool,    val.asBool()?"true":"false");
    JSON(isInt,     lc(val.asInt64()));
    JSON(isUInt,    lc(val.asUInt64()));
    JSON(isDouble,  lc(val.asDouble()));
    JSON(isString,  val.asString());
    throw std::runtime_error("bad JSON object type");
}

#undef JSON

class SetSettingsWorker
{
public:
    SetSettingsWorker(boost::shared_ptr<RequestInFlight> const &rif, ISettingsFactory *sfac) :
        rif_(rif),
        sfac_(sfac)
    {
    }

    boost::shared_ptr<RequestInFlight> rif_;
    ISettingsFactory *sfac_;
    boost::shared_ptr<ISettings> set_;
    Json::Reader reader_;
    Json::Value root_;

    void go(std::string const &settings)
    {
        if (!reader_.parse(&*rif_->req_->bodyBegin(), &*rif_->req_->bodyEnd(), root_, false))
        {
            formatError(reader_.getFormattedErrorMessages());
            return;
        }
        if (!root_.isObject())
        {
            formatError("JSON settings must be key/value object");
            return;
        }
        sfac_->open(settings, true, set_, heap_complete<SetSettingsWorker, &SetSettingsWorker::go_complete>(this));
    }

    void formatError(std::string const &str)
    {
        rif_->reportError(str, 400);
        delete this;
    }

    void go_complete()
    {
        try
        {
            LogSpam << "SetSettingsWorker::go_complete()";
            if (!set_)
            {
                rif_->reportError("no such settings", 404);
                delete this;
                return;
            }
            Json::Value::Members members(root_.getMemberNames());
            for (Json::Value::Members::const_iterator ptr(members.begin()), end(members.end());
                ptr != end; ++ptr)
            {
                Json::Value const &val(root_[*ptr]);
                if (val.isArray() || val.isObject())
                {
                    formatError("JSON setting values cannot be compound (found " + *ptr + ")");
                    return;
                }
                std::string valStr = json_string(val);
                set_->set(*ptr, valStr);
            }
            rif_->strm_ << "{\"success\":true}";
            rif_->complete(200, "application/json");
        }
        catch (std::exception const &x)
        {
            formatError(x.what());
        }
        delete this;
    }
};

void RequestInFlight::getSettings(std::string const &settings, std::string const &key)
{
    LogSpam << "RequestInFlight::getSettings(" << settings << "," << key << ")";
    if(!istat::is_valid_settings_name(settings))
    {
        reportError("invalid settings name", 400);
        return;
    }

    ISettingsFactory *sfac = &istat::Env::get<ISettingsFactory>();
    (new GetSettingsWorker(shared_from_this(), sfac))->go(settings, key);
}

void RequestInFlight::on_storeSettingsBody(std::string const &settings)
{
    if(!istat::is_valid_settings_name(settings))
    {
        reportError("invalid settings name", 400);
        return;
    }

    ISettingsFactory *sfac = &istat::Env::get<ISettingsFactory>();
    (new SetSettingsWorker(shared_from_this(), sfac))->go(settings);
}

