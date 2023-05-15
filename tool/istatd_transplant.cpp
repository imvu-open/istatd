/* Transplant data from files on one host to files on another host. 
 * This does direct file I/O, so if the files are also currently being written to, 
 * make sure that the range for which you transplant is sufficiently in the past 
 * to not be active!
 *
 * Usage:
 * - on host A, run:
 *   istatd_transplant --listen 23566 --store /var/db/istatd/whatever
 * - on host B, run:
 *   istatd_transplant --connect server-a:23566 --store /var/db/istatd/whatever --fromtime 'some date' --totime 'some date' --limitrate N
 *
 * The format of "--fromtime" and "--totime" dates should be YYYY-mm-ddTHH:MM:SS[-HH:MM] and can also be UNIX epoch as integer.
 * The option "limitrate" limits the number of files examined per second. Depending on how large the date range is, 
 * each file may generate more or less network and I/O traffic.
 *
 * host B will then connect to host A, and attempt to sync up date in all files in the store.
 * "Sync up" means, for each retention bucket in the given date interval, for each counter, for each retention interval:
 * - if host A has no samples for a bucket, and host B has samples for that bucket, host A integrates the data from host B
 * - if host A has samples for a bucket, and host B does not have samples for that bucket, host B integrates the data from host A
 * - else do nothing for this bucket
 *
 * This program will run in the foreground, until all files have been examined once, at which point it will exit
 * if it is the client, or keep waiting for more clients if it is the server.
 */

#include "../config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

#include <boost/filesystem.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>

#include "istat/StatFile.h"
#include "istat/Mmap.h"
#include "istat/IRecorder.h"
#include "../daemon/Argument.h"


#define MAX_TRANSFER_SIZE 0x4000000


using namespace boost::asio;
using namespace boost::asio::ip;

Argument<int> arg_listen("listen", 0, "Port to listen to as server. Mutually exclusive with --connect.");
Argument<std::string> arg_connect("connect", "", "Server to connect to as client. Mutually exclusive with --listen.");
Argument<std::string> arg_store("store", "", "Where the root of the istatd counter store is. Required.");
Argument<int> arg_limitrate("limitrate", 0, "Maximum number of counter files examined per second.");
Argument<std::string> arg_fromtime("fromtime", "", "Start time to examine/transplant. Required on client.");
Argument<std::string> arg_totime("totime", "", "End time to examine/transplant. Required on client.");
Argument<int> arg_progress("progress", 0, "How often to show progress. 0 to disable progress output.");
Argument<bool> arg_verbose("verbose", false, "Whether to print each file name processed.");
Argument<bool> arg_debug("debug", false, "Whether to print information useful to debugging.");
Argument<bool> arg_test("test", false, "Run the unit tests.");
Argument<std::string> arg_pidfile("pidfile", "", "File to write PID to, or fail if it already exists.");

int limitrate;
time_t fromtime;
time_t totime;
int progress;
bool verbose;
bool debug;
volatile bool quitting = false;
char *pidfilepath;
int gotsig;

io_service gSvc;
istat::Mmap *gMmap;


void usage()
{
    std::cerr << "istatd built " __DATE__ << " " << __TIME__ << std::endl;
    for (ArgumentBase::iterator ptr(ArgumentBase::begin()), end(ArgumentBase::end());
        ptr != end; ++ptr)
    {
        std::cerr << std::left << "--" << std::setw(12) << (*ptr).first << std::setw(0) << " (" <<
            std::setw(12) << (*ptr).second->str() << std::setw(0) << ")   " << (*ptr).second->help() << std::endl;
    }
    std::cerr << std::endl;
    exit(1);
}

void argument_error(char const *aname, char const *message)
{
    std::cerr << "Error in --" << aname << ": " << message << std::endl << std::endl;
    usage();
    exit(1);
}

void doquit(int s)
{
    gotsig = s;
    quitting = true;
    gSvc.stop();
}


std::string terminate(std::string const &str, std::string const &end)
{
    if (end.size() == 0)
    {
        return str;
    }
    if (str.size() < end.size() || str.substr(str.size()-end.size()) != end)
    {
        return str + end;
    }
    return str;
}

struct ProtoOffer
{
    ProtoOffer() { starttime = endtime = 0; }
    std::string filepath;
    uint64_t starttime;
    uint64_t endtime;
    std::vector<istat::FileBucketLayout> buckets;
};


class DummyRecorder : public istat::IRecorder
{
    public:
        virtual void record(int64_t value) {}
};

DummyRecorder gDummyRecorder;


//  Parse a date into UTC format timestamp
//  requires "TZ" to be set to ""
//  Accepts three formats:
//  unix epoch timestamp (long integer)
//  YYYY-mm-ddTHH:MM:SS             (note the T)
//  YYYY-mm-ddTHH:MM:SS+ZZ:ZZ       (note the T, and sign can be +/- for timezone offset)
bool parse_time(std::string const &str, time_t *otime)
{
    unsigned long ltime = 0;
    char *uend = 0;
    ltime = strtoul(str.c_str(), &uend, 10);
    if (uend && !*uend && ltime != 0)
    {
        *otime = ltime;
        return true;
    }
    struct tm parsed = { 0 };
    int hoff = 0;
    int moff = 0;
    char const *end = strptime(str.c_str(), "%Y-%m-%dT%H:%M:%S", &parsed);
    if (!end)
    {
        return false;
    }
    if (*end)
    {
        if (*end != '+' && *end != '-')
        {
            return false;
        }
        int neg = 1;
        if (*end == '-')
        {
            neg = -1;
        }
        ++end;
        if (2 != sscanf(end, "%d:%d", &hoff, &moff))
        {
            return false;
        }
        if (hoff > 13 || moff > 59 || hoff < 0 || moff < 0)
        {
            return false;
        }
        hoff *= neg;
        moff *= neg;
    }
    time_t out = mktime(&parsed);
    out -= (hoff * 3600 + moff * 60);   //  if it was 10:00 in time zone -09:00, that means it's 19:00 in zulu
    *otime = out;
    return true;
}

double clocktime()
{
#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0 && defined(_POSIX_MONOTONIC_CLOCK)
    struct timespec tsp;
    clock_gettime(CLOCK_MONOTONIC, &tsp);
    return (double)tsp.tv_sec + (double)tsp.tv_nsec * 1e-9;
#else
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
#endif
}


//  it's easier to do this little-endian than big-endian!
void marshal_integral(uint64_t val, std::vector<char> &obuf)
{
again:
    obuf.push_back((unsigned char)(val & 0x7f));
    val >>= 7;
    if (val)
    {
        obuf.back() |= 0x80;
        goto again;
    }
}

uint64_t demarshal_integral(std::vector<char> const &ibuf, size_t &offset)
{
    int shift = 0;
    uint64_t ret = 0;
    while (true)
    {
        if (offset >= ibuf.size())
        {
            if (debug)
            {
                std::cerr << "demarshal_integral() failed; end of buffer at " << offset << std::endl;
            }
            //  buffer underrun error
            offset = (size_t)-1;
            return 0;
        }
        unsigned char ch = ibuf[offset];
        ++offset;
        ret |= (uint64_t)(ch & 0x7f) << shift;
        shift += 7;
        if (!(ch & 0x80))
        {
            break;
        }
        if (shift > 63)
        {
            if (debug)
            {
                std::cerr << "demarshal_integral() failed; shift too large " << shift << std::endl;
            }
            //  data format error
            offset = (size_t)-1;
            return 0;
        }
    }
    return ret;
}

void marshal_string(std::string const &str, std::vector<char> &obuf)
{
    if (str.size() > 0x1000)
    {
        marshal_integral(0, obuf);
        throw std::runtime_error("String too long in marshal_string()");
    }
    marshal_integral(str.size(), obuf);
    for (std::string::const_iterator ptr(str.begin()), end(str.end());
            ptr != end;
            ++ptr)
    {
        marshal_integral((unsigned char)*ptr, obuf);
    }
}

std::string demarshal_string(std::vector<char> const &ibuf, size_t &offset)
{
    std::string ret;
    uint64_t len = demarshal_integral(ibuf, offset);
    if (len > 0x1000)
    {
        if (debug)
        {
            std::cerr << "demarshal_string() failed, len=" << len << std::endl;
        }
        //  data error
        offset = (size_t)-1;
        return "";
    }
    ret.reserve(len);
    while (len > 0)
    {
        uint64_t val = demarshal_integral(ibuf, offset);
        if (val == 0 || val > 255)
        {
            //  data error
            if (debug)
            {
                std::cerr << "demarshal_string() failed, val=" << val << std::endl;
            }
            offset = (size_t)-1;
            return "";
        }
        ret.push_back((unsigned char)(val & 0xff));
        --len;
    }
    return ret;
}

void marshal_bucket(istat::FileBucketLayout const &b, std::vector<char> &obuf)
{
    size_t sz = obuf.size();
    obuf.resize(sz + sizeof(b));
    memcpy(&obuf[sz], &b, sizeof(b));
}

bool demarshal_bucket(std::vector<char> const &ibuf, size_t &offset, istat::Bucket &ob)
{
    if (offset >= ibuf.size() || ibuf.size() < sizeof(ob) || ibuf.size() - sizeof(ob) < offset)
    {
        offset = (size_t)-1;
        return false;
    }
    memcpy(&ob, &ibuf[offset], sizeof(ob));
    offset += sizeof(ob);
    return true;
}

void marshal_offer(ProtoOffer const &offer, std::vector<char> &buf)
{
    marshal_string(offer.filepath, buf);
    marshal_integral(offer.starttime, buf);
    marshal_integral(offer.endtime, buf);
    marshal_integral(offer.buckets.size(), buf);
    for (size_t i = 0, n = offer.buckets.size(); i != n; ++i)
    {
        marshal_bucket(offer.buckets[i], buf);
    }
}

bool demarshal_offer(ProtoOffer &offer, std::vector<char> const &buffer, size_t &offset)
{
    offer.filepath = demarshal_string(buffer, offset);
    offer.starttime = demarshal_integral(buffer, offset);
    offer.endtime = demarshal_integral(buffer, offset);
    size_t n = demarshal_integral(buffer, offset);
    offer.buckets.reserve(n);
    for (size_t i = 0; i != n; ++i)
    {
        istat::Bucket b(false);
        if (!demarshal_bucket(buffer, offset, b))
        {
            if (debug)
            {
                std::cerr << "demarshal_bucket() failed" << std::endl;
            }
            offset = (size_t)-1;
            return false;
        }
        offer.buckets.push_back((istat::FileBucketLayout const &)b);
    }
    return offset <= buffer.size();
}

void apply_offer_to_file(ProtoOffer const &input, istat::StatFile *file, time_t minimum, time_t maximum)
{
    time_t mintime = file->firstBucketTime();
    if (mintime < minimum)
    {
        mintime = minimum;
    }
    time_t maxtime = file->firstBucketTime() + file->settings().numSamples * file->settings().intervalTime;
    if (maximum && maxtime > maximum)
    {
        maxtime = maximum;
    }
    if (debug)
    {
        std::cerr << "Applying " << input.buckets.size() << " buckets to file " << file->getPath() << std::endl;
    }
    for (size_t i = 0, n = input.buckets.size(); i != n; ++i)
    {
        if (input.buckets[i].time_ >= mintime && input.buckets[i].time_ <= maxtime)
        {
            if (debug)
            {
                std::cerr << "Updating bucket at " << input.buckets[i].time_ << std::endl;
            }
            file->rawUpdateBucket((istat::Bucket const &)input.buckets[i], istat::RAWUP_ONLY_IF_EMPTY);
        }
    }
}

/*  For the given time interval,
 *  apply the buckets if they are present in the offer
 *  else if not present, extract existing data if present in file
 *  return the offer to the other end
 */
void apply_and_extract_other_empty_buckets(ProtoOffer const &input, istat::StatFile &file, ProtoOffer &output)
{
    uint64_t mintime = file.firstBucketTime();
    uint64_t intervalTime = file.settings().intervalTime;
    uint64_t maxtime = file.firstBucketTime() + file.settings().numSamples * intervalTime;
    std::vector<istat::FileBucketLayout>::const_iterator ptr(input.buckets.begin()), end(input.buckets.end());
    if (mintime < input.starttime)
    {
        mintime = input.starttime;
    }
    if (maxtime > input.endtime)
    {
        maxtime = input.endtime;
    }
    while (mintime <= maxtime)
    {
        mintime += intervalTime;
        bool updated = false;
        while (ptr != end && (uint64_t)ptr->time_ < mintime)
        {
            updated = true;
            if (debug)
            {
                std::cerr << "Applying bucket time " << ptr->time_ << " to file " << file.getPath() << std::endl;
            }
            file.rawUpdateBucket((istat::Bucket const &)*ptr, istat::RAWUP_ONLY_IF_EMPTY);
            ++ptr;
        }
        if (!updated)
        {
            istat::Bucket b(file.bucket(file.mapTimeToBucketIndex(mintime-intervalTime)));
            if (b.count())
            {
                if (debug)
                {
                    std::cerr << "Gathering bucket " << b.time() << std::endl;
                }
                output.buckets.push_back((istat::FileBucketLayout &)b);
            }
        }
    }
}

class ServerState;


class ServerClient : public boost::enable_shared_from_this<ServerClient>
{
    public:
        ServerClient(ServerState &svr, io_service &svc) : svr_(svr), svc_(svc), socket_(svc)
        {
        }

        void go();
        void on_read(boost::system::error_code const &err, size_t nread);
        void complete_request();
        void write_buffer();
        void on_write(boost::system::error_code const &err, size_t nwritten);

        ServerState &svr_;
        io_service &svc_;
        tcp::socket socket_;
        std::vector<char> buffer_;
        size_t offset_;
};

class ServerState
{
    public:
        ServerState(unsigned short port, std::string const &root, io_service &svc) : port_(port), root_(root), svc_(svc), acceptor_(svc_)
        {
            if (debug)
            {
                std::cerr << "ServerState::ServerState(" << port << ", " << root << ", svc)" << std::endl;
            }
            numFail_ = 0;
            boost::system::error_code err;
            acceptor_.open(tcp::v4());
            acceptor_.set_option(tcp::acceptor::reuse_address(true));
            acceptor_.bind(tcp::endpoint(tcp::v4(), port), err);
            if (!!err)
            {
                std::cerr << "bind(): could not bind to port " << port << ": " << err.message() << std::endl;
                throw std::runtime_error("Can't create acceptor.");
            }
            acceptor_.listen(10);
        }

        void start();
        void on_accept(boost::system::error_code const &err, boost::shared_ptr<ServerClient> client);

        unsigned short port_;
        std::string root_;
        io_service &svc_;
        tcp::acceptor acceptor_;
        int numFail_;
        istat::Stats stats_;
};

void ServerClient::go()
{
    if (debug)
    {
        std::cerr << "ServerClient::go()" << std::endl;
    }
    offset_ = 0;
    buffer_.resize(4);
    async_read(
            socket_,
            buffer(&buffer_[0], 4), 
            boost::bind(
                &ServerClient::on_read,
                shared_from_this(),
                boost::placeholders::_1,
                boost::placeholders::_2));
}

void ServerClient::on_read(boost::system::error_code const &err, size_t nread)
{
    if (debug)
    {
        std::cerr << "ServerClient::on_read(" << err << ", " << nread << ")" << std::endl;
    }
    boost::system::error_code ec;
    if (!err)
    {
        if (nread == 0)
        {
            std::cerr << "remote host " << socket_.remote_endpoint(ec) << " closed connection." << std::endl;
            return;
            //  fall off the end
        }
        offset_ += nread;
        if (offset_ == buffer_.size())
        {
            if (offset_ == 4)
            {
                //  received size
                uint64_t size = 
                    ((unsigned char)buffer_[0] << 24u) |
                    ((unsigned char)buffer_[1] << 16u) |
                    ((unsigned char)buffer_[2] << 8u) |
                    ((unsigned char)buffer_[3] << 0u);
                if (size > MAX_TRANSFER_SIZE)
                {
                    std::cerr << "Remote client " << socket_.remote_endpoint(ec) << " sends too large packet." << std::endl;
                    return;
                }
                buffer_.resize(size + 4);
                goto read_more;
            }
            else
            {
                //  received request
                complete_request();
            }
        }
        else
        {
read_more:
            async_read(
                    socket_,
                    buffer(&buffer_[offset_], buffer_.size()-offset_), 
                    boost::bind(
                        &ServerClient::on_read,
                        shared_from_this(),
                        boost::placeholders::_1,
                        boost::placeholders::_2));
        }
    }
    else
    {
        std::cerr << "read error for " << socket_.remote_endpoint(ec) << ": " << err.message() << std::endl;
    }
}

void ServerClient::complete_request()
{
    if (debug)
    {
        std::cerr << "ServerClient::complete_requests()" << std::endl;
    }
    ProtoOffer offer;
    size_t offset = 4;
    demarshal_offer(offer, buffer_, offset);
    if (offset != buffer_.size())
    {
        if (debug)
        {
            std::cerr << "offset " << offset << " size " << buffer_.size() << std::endl;
        }
        boost::system::error_code ec;
        std::cerr << "client " << socket_.remote_endpoint(ec) << " sent corrupt or truncated data." << std::endl;
        return;
    }
    if (verbose)
    {
        std::cout << offer.filepath << std::endl;
    }
    ProtoOffer result;
    try
    {
        result.filepath = offer.filepath;
        istat::StatFile file(svr_.root_ + offer.filepath, svr_.stats_, gMmap, true);
        result.starttime = offer.starttime;
        result.endtime = offer.endtime;
        apply_and_extract_other_empty_buckets(offer, file, result);
    }
    catch (std::runtime_error const &err)
    {
        std::cerr << svr_.root_ << offer.filepath << ": " << err.what() << std::endl;
    }
    std::vector<char>().swap(buffer_);
    buffer_.resize(4);
    marshal_offer(result, buffer_);
    size_t sz = buffer_.size()-4;
    buffer_[0] = (sz >> 24) & 0xff;
    buffer_[1] = (sz >> 16) & 0xff;
    buffer_[2] = (sz >> 8) & 0xff;
    buffer_[3] = (sz >> 0) & 0xff;
    offset_ = 0;
    write_buffer();
}


void ServerClient::write_buffer()
{
    if (debug)
    {
        std::cerr << "ServerClient::write_buffer()" << std::endl;
    }
    if (debug && verbose)
    {
        std::cerr << "SENDING:[" << std::endl << std::hex;
        for (size_t i = offset_; i != buffer_.size(); ++i)
        {
            std::cerr << (int)buffer_[i] << " ";
        }
        std::cerr << std::dec << std::endl << "]" << std::endl;
    }
    socket_.async_send(
            buffer(&buffer_[offset_], buffer_.size()-offset_),
            boost::bind(&ServerClient::on_write, shared_from_this(), boost::placeholders::_1, boost::placeholders::_2));
}

void ServerClient::on_write(
        boost::system::error_code const &err,
        size_t nwritten)
{
    if (debug)
    {
        std::cerr << "ServerClient::on_write(" << err << ", " << nwritten << ")" << std::endl;
    }
    boost::system::error_code ec;
    if (!err)
    {
        if (nwritten == 0)
        {
            std::cerr << "client " << socket_.remote_endpoint(ec) << " closed connection." << std::endl;
            return;
            //  fall off the end for this client
        }
        offset_ += nwritten;
        if (offset_ < buffer_.size())
        {
            write_buffer();
        }
        else
        {
            go();   //  read more
        }
    }
}


void ServerState::start()
{
    if (debug)
    {
        std::cerr << "ServerState::start()" << std::endl;
    }
    boost::shared_ptr<ServerClient> client = boost::make_shared<ServerClient>(boost::ref(*this), boost::ref(svc_));
    acceptor_.async_accept(client->socket_,
            boost::bind(&ServerState::on_accept, this, boost::placeholders::_1, client));
}

void ServerState::on_accept(boost::system::error_code const &err, boost::shared_ptr<ServerClient> client)
{
    boost::system::error_code ec;
    if (debug)
    {
        std::cerr << "ServerState::on_accept(" << err << ", " << client->socket_.remote_endpoint(ec) << ")" << std::endl;
    }
    if (!err)
    {
        if (verbose)
        {
            std::cout << "Accepted connection from " << client->socket_.remote_endpoint(ec) << std::endl;
        }
        numFail_ = 0;
        client->go();
    }
    else
    {
        ++numFail_;
        std::cerr << "accept() error: " << err.message() << std::endl;
    }
    if (numFail_ < 5)
    {
        start();
        return;
    }
    std::cerr << "Five accept failures in a row -- exiting." << std::endl;
    doquit(0);
    //fall off the end of gSvc
}


void do_server()
{
    if (arg_connect.get().size())
    {
        argument_error("connect", "Cannot have both --connect and --listen.");
    }
    if (arg_fromtime.get().size())
    {
        argument_error("fromtime", "Server does not support --fromtime.");
    }
    if (arg_totime.get().size())
    {
        argument_error("totime", "Server does not support --totime.");
    }
    if (arg_progress.get())
    {
        argument_error("progress", "Server does not support --progress.");
    }
    if (arg_limitrate.get())
    {
        argument_error("limitrate", "Server does not support --limitrate.");
    }
    if (arg_store.get().size() == 0)
    {
        argument_error("store", "Store must be specified.");
    }

    //  start listening
    //  for each incoming request, scan the file and return result
    //  when client disconnects, print stats
    //  run forever
    ServerState svs(arg_listen.get(), terminate(arg_store.get(), "/"), gSvc);
    svs.start();
    gSvc.run();
}



struct FileState
{
    FileState() { sfile = 0; offset = 0; }
    ~FileState() { delete sfile; }
    std::string filepath;
    istat::StatFile *sfile;
    std::vector<char> buffer;
    size_t offset;
};


class ClientConnection
{
    public:
        ClientConnection(io_service &svc) :
            svc_(svc),
            socket_(svc_),
            numFiles_(0),
            lastFiles_(0),
            lastTime_(0)
        {
            if (debug)
            {
                std::cerr << "ClientConnection::ClientConnection()" << std::endl;
            }
            stats_.statHit = &gDummyRecorder;
            stats_.statMiss = &gDummyRecorder;
        }

        void start(std::string const &arg, std::string const &root);
        void on_connected(boost::system::error_code const &err, tcp::resolver::iterator ptr);
        void next_dir();
        void next_file();
        void do_file(std::string const &path);
        void limit_speed();
        void do_offer(ProtoOffer const &offer, boost::shared_ptr<FileState> const &fs);
        void on_write(boost::system::error_code const &err, size_t written, boost::shared_ptr<FileState> fs);
        void receive_response(boost::shared_ptr<FileState> fs);
        void on_read(boost::system::error_code const &err, size_t nread, boost::shared_ptr<FileState> buf);
        void received_offer(boost::shared_ptr<FileState> fs);

        io_service &svc_;
        tcp::socket socket_;
        std::string root_;
        std::list<std::string> dirQueue_;
        std::list<std::string> fileQueue_;
        unsigned int numFiles_;
        unsigned int lastFiles_;
        double lastTime_;
        istat::Stats stats_;
};

void ClientConnection::start(std::string const &arg, std::string const &root)
{
    if (debug)
    {
        std::cerr << "ClientConnection::start(" << arg << ", " << root << ")" << std::endl;
    }
    numFiles_ = 0;
    lastFiles_ = 0;
    lastTime_ = 0;
    root_ = root;
    //  make sure root ends with a slash
    if (root_.size() == 0 || *root_.rbegin() != '/')
    {
        root_ += "/";
    }
    dirQueue_.push_back("");
    size_t pos = arg.find_first_of(":");
    if (pos == arg.npos)
    {
        argument_error("connect", "Address not in host:port format.");
    }
    std::string hostpart(arg.substr(0, pos));
    std::string portpart(arg.substr(pos+1));
    tcp::resolver resolver(svc_);
    tcp::resolver::iterator ptr = resolver.resolve(tcp::resolver::query(tcp::v4(), hostpart, portpart));
    socket_.async_connect(ptr->endpoint(), boost::bind(&ClientConnection::on_connected, this, boost::placeholders::_1, ptr));
}

void ClientConnection::on_connected(boost::system::error_code const &err, tcp::resolver::iterator ptr)
{
    if (debug)
    {
        std::cerr << "ClientConnection::on_connected(" << err << ", " << ptr->endpoint() << ")" << std::endl;
    }
    if (err)
    {
        std::cerr << "connect to " << ptr->endpoint() << " error: " << err.message() << std::endl;
        return;
    }
    if (progress || verbose)
    {
        std::cerr << "connected to: " << ptr->endpoint() << std::endl;
    }
    //  start directory walk
    next_dir();
}

void ClientConnection::next_dir()
{
    if (debug)
    {
        std::cerr << "ClientConnection::next_dir()" << std::endl;
    }
    if (dirQueue_.empty())
    {
        if (progress || verbose)
        {
            std::cerr << "No more directories to examine." << std::endl;
        }
        return;
    }
    limit_speed();
    std::string s(dirQueue_.front());
    dirQueue_.pop_front();
    if (verbose)
    {
        std::cout << "DIR: " << s << std::endl;
    }

    DIR *d = opendir((root_ + s).c_str());
    if (!d)
    {
        std::cerr << "warning: could not opendir(" << root_ + s << ")." << std::endl;
        svc_.post(boost::bind(&ClientConnection::next_dir, this));
        return;
    }
    struct stat stbuf;
    while (struct dirent *dent = readdir(d))
    {
        if (dent->d_name[0] == '.')
        {
            continue;
        }
        std::string partpath(s + dent->d_name);
        std::string fullpath(root_ + partpath);
        memset(&stbuf, 0, sizeof(stbuf));
        if (debug)
        {
            std::cerr << "stat(" << fullpath << ")" << std::endl;
        }
        if (stat(fullpath.c_str(), &stbuf) < 0)
        {
            std::cerr << "warning: failed to stat: " << partpath << std::endl;
        }
        else if (S_ISDIR(stbuf.st_mode))
        {
            dirQueue_.push_back(partpath + "/");
        }
        else if (S_ISREG(stbuf.st_mode))
        {
            fileQueue_.push_back(partpath);
        }
    }
    closedir(d);
    svc_.post(boost::bind(&ClientConnection::next_file, this));
}

void ClientConnection::next_file()
{
    if (debug)
    {
        std::cerr << "ClientConnection::next_file()" << std::endl;
    }
    if (fileQueue_.empty())
    {
        svc_.post(boost::bind(&ClientConnection::next_dir, this));
        return;
    }
    limit_speed();
    std::string filepath(fileQueue_.front());
    fileQueue_.pop_back();
    if (verbose)
    {
        std::cout << filepath << std::endl;
    }
    do_file(filepath);
}

void ClientConnection::do_file(std::string const &path)
{
    if (debug)
    {
        std::cerr << "ClientConnection::do_file(" << path << ")" << std::endl;
    }
    //  open file
    std::string fullpath(root_ + path);
    boost::shared_ptr<FileState> fs = boost::make_shared<FileState>();
    fs->filepath = path;
    try
    {
        fs->sfile = new istat::StatFile(fullpath, stats_, gMmap, true);
    }
    catch (std::runtime_error const &err)
    {
        std::cerr << fullpath << ": " << err.what() << std::endl;
        gSvc.post(boost::bind(&ClientConnection::next_file, this));
        return;
    }
    //  find buckets in range
    time_t startTime = fs->sfile->firstBucketTime();
    time_t endTime = fs->sfile->lastBucketTime();
    if (startTime > totime || endTime < fromtime)
    {
        //  done
        gSvc.post(boost::bind(&ClientConnection::next_file, this));
        return;
    }
    //  extract empty buckets
    if (startTime < fromtime)
    {
        startTime = fromtime;
    }
    if (endTime > totime)
    {
        endTime = totime;
    }
    int64_t startIndex = fs->sfile->mapTimeToBucketIndex(startTime);
    int64_t endIndex = fs->sfile->mapTimeToBucketIndex(endTime);
    ProtoOffer offer;
    offer.filepath = path;
    offer.starttime = startTime;
    offer.endtime = endTime;
    while (startIndex <= endIndex)
    {
        istat::Bucket const &b(fs->sfile->bucket(startIndex));
        if (b.count() != 0)
        {
            offer.buckets.push_back((istat::FileBucketLayout &)b);
        }
        ++startIndex;
    }
    do_offer(offer, fs);
}

void ClientConnection::do_offer(ProtoOffer const &offer, boost::shared_ptr<FileState> const &fs)
{
    if (debug)
    {
        std::cerr << "ClientConnection::do_offer(" << offer.buckets.size() << ", " << fs->filepath << ")" << std::endl;
    }

    //  build a network packet
    std::vector<char> &buf(fs->buffer);
    char zeros[] = { 0, 0, 0, 0 };
    buf.insert(buf.end(), zeros, &zeros[4]);
    marshal_offer(offer, buf);
    size_t sz = buf.size() - 4;
    //  don't send more than 64 MB in a single transaction
    if (sz > MAX_TRANSFER_SIZE)
    {
        std::cerr << "file " << offer.filepath << " generates too much data: " << sz << " bytes. Please use a smaller time range.";
    }
    else
    {
        buf[0] = (sz >> 24) & 0xff;
        buf[1] = (sz >> 16) & 0xff;
        buf[2] = (sz >> 8) & 0xff;
        buf[3] = (sz >> 0) & 0xff;
        if (debug && verbose)
        {
            std::cerr << "SENDING:[" << std::endl << std::hex;
            for (size_t i = 0; i != buf.size(); ++i)
            {
                std::cerr << (int)buf[i] << " ";
            }
            std::cerr << std::dec << std::endl << "]" << std::endl;
        }
        socket_.async_write_some(buffer(&buf[0], buf.size()), boost::bind(&ClientConnection::on_write, this, boost::placeholders::_1, boost::placeholders::_2, fs));
    }
}

void ClientConnection::on_write(
        boost::system::error_code const &err,
        size_t written, 
        boost::shared_ptr<FileState> fs)
{
    if (debug)
    {
        std::cerr << "ClientConnection::on_write(" << err << ", " << written << ", " << fs->filepath << ")" << std::endl;
    }
    if (!err)
    {
        if (written == 0)
        {
            std::cerr << "Send error: remote end closed connection." << std::endl;
            return;
            //  fall off end of gSvc
        }
        fs->offset += written;
        if (fs->offset >= fs->buffer.size())
        {
            receive_response(fs);
        }
        else
        {
            if (debug && verbose)
            {
                std::cerr << "SENDING:[" << std::endl << std::hex;
                for (size_t i = fs->offset; i != fs->buffer.size(); ++i)
                {
                    std::cerr << (int)fs->buffer[i] << " ";
                }
                std::cerr << std::dec << std::endl << "]" << std::endl;
            }
            //  write some more
            socket_.async_write_some(buffer(&(fs->buffer)[fs->offset], fs->buffer.size()-fs->offset), 
                    boost::bind(&ClientConnection::on_write, this, boost::placeholders::_1, boost::placeholders::_2, fs));
        }
    }
    else
    {
        std::cerr << "Send error: " << err.message() << " for " << fs->filepath << std::endl;
        return;
        //  stop processing files -- will fall out of gSvc servicing
    }
}

void ClientConnection::receive_response(boost::shared_ptr<FileState> fs)
{
    if (debug)
    {
        std::cerr << "ClientConnection::receive_response(" << fs->filepath << ")" << std::endl;
    }
    //  receive remote's dump of reciprocal buckets
    //  first, read the length field
    fs->buffer.resize(4);
    fs->offset = 0;
    socket_.async_read_some(buffer(&fs->buffer[0], 4), 
            boost::bind(&ClientConnection::on_read, this, boost::placeholders::_1, boost::placeholders::_2, fs));
}

void ClientConnection::on_read(
        boost::system::error_code const &err,
        size_t nread, 
        boost::shared_ptr<FileState> fs)
{
    if (debug)
    {
        std::cerr << "ClientConnection::on_read(" << err << ", " << nread << ", " << fs->filepath << ")" << std::endl;
    }
    if (!err)
    {
        if (nread == 0)
        {
            std::cerr << "Remote end closed connection for " << fs->filepath << std::endl;
            return;
            //  fall off end of gSvc
        }
        fs->offset += nread;
        if (fs->offset == fs->buffer.size())
        {
            if (fs->buffer.size() == 4)
            {
                //  I have read the length field -- get the rest
                size_t sz = ((unsigned char)fs->buffer[0] << 24u)
                    | ((unsigned char)fs->buffer[1] << 16u)
                    | ((unsigned char)fs->buffer[2] << 8u)
                    | ((unsigned char)fs->buffer[3] << 0u);
                if (sz > MAX_TRANSFER_SIZE)
                {
                    std::cerr << "Got bad transfer size " << sz << "; "
                        << std::hex << (int)fs->buffer[0] << " " << (int)fs->buffer[1] << " "
                        << (int)fs->buffer[2] << " " << (int)fs->buffer[3] << std::dec << std::endl;
                    throw std::runtime_error("Bad data from server.");
                }
                fs->buffer.resize(sz + 4);
                if (debug)
                {
                    std::cerr << "Got length " << sz << "; read more." << std::endl;
                }
                goto read_more;
            }
            else 
            {
                if (debug)
                {
                    std::cerr << "Got complete packet." << std::endl;
                }
                //  I have read a complete packet
                received_offer(fs);
            }
        }
        else
        {
read_more:
            //  read more
            socket_.async_read_some(buffer(&fs->buffer[fs->offset], fs->buffer.size()-fs->offset),
                        boost::bind(&ClientConnection::on_read, this, boost::placeholders::_1, boost::placeholders::_2, fs));
        }
    }
    else
    {
        std::cerr << "Receive error: " << err.message() << " for " << fs->filepath << std::endl;
        //  fall off end of gSvc
    }
}

void ClientConnection::received_offer(boost::shared_ptr<FileState> fs)
{
    if (debug)
    {
        std::cerr << "ClientConnection::received_offer(" << fs->filepath << ")" << std::endl;
    }
    //  Demarshal the ProtoOffer
    ProtoOffer offer;
    size_t offset = 4;
    demarshal_offer(offer, fs->buffer, offset);
    if (offset != fs->buffer.size())
    {
        //  got error
        std::cerr << "Error: Bad format packet from server for " << fs->filepath << std::endl;
        return;
        //  fall off end of gSvc
    }
    if (offer.filepath != fs->filepath)
    {
        std::cerr << "Error: Mis-matched file paths from server; got " << offer.filepath << "; expected " << fs->filepath << std::endl;
        return;
        //  fall off end of gSvc
    }
    if (debug)
    {
        std::cerr << "Got " << offer.buckets.size() << " buckets for " << offer.filepath << std::endl;
    }
    //  Apply the data to the file
    apply_offer_to_file(offer, fs->sfile, fromtime, totime);
    //  do next file
    svc_.post(boost::bind(&ClientConnection::next_file, this));
}

void ClientConnection::limit_speed()
{
    ++numFiles_;
    if (progress > 0 && !(numFiles_ % progress) && !verbose && !debug)
    {
        std::cerr << numFiles_ << " files\r" << std::flush;
    }
    if (limitrate && ((int)(numFiles_ - lastFiles_) >= limitrate))
    {
        double now = clocktime();
        //  I'm paranoid about time!
        assert(now >= lastTime_);
        double snoz = 1.0 - (now - lastTime_);
        if (snoz > 0)
        {
            long tosleep = (long)(1000000 * snoz);
            assert(tosleep <= 1000000);
            if (tosleep > 1000000)
            {
                tosleep = 1000000;
            }
            usleep(tosleep);
        }
        lastTime_ = now; // note -- this includes the sleep and wake-up time
        lastFiles_ = numFiles_;
    }
}


void do_client()
{
    if (arg_listen.get())
    {
        argument_error("listen", "Client does not support --listen.");
    }
    if (arg_progress.get() < 0)
    {
        argument_error("progress", "Progress count must be positive.");
    }
    if (!parse_time(arg_fromtime.get(), &fromtime))
    {
        argument_error("fromtime", "Bad date format.");
    }
    if (!parse_time(arg_totime.get(), &totime))
    {
        argument_error("totime", "Bad date format.");
    }
    if (arg_store.get().size() == 0)
    {
        argument_error("store", "Store must be specified.");
    }
    progress = arg_progress.get();
    limitrate = arg_limitrate.get();
    if (limitrate < 0)
    {
        argument_error("limitrate", "Rate limit must be 0 or greater.");
    }
    verbose = arg_verbose.get();
    

    //  connect to server
    boost::shared_ptr<ClientConnection> cc = boost::make_shared<ClientConnection>(boost::ref(gSvc));
    cc->start(arg_connect.get(), terminate(arg_store.get(), "/"));
    //  run until nothing else to do
    gSvc.run();
}


void remove_pidfile()
{
    if (pidfilepath)
    {
        std::cerr << "Removing PID file: " << pidfilepath << std::endl;
        ::unlink(pidfilepath);
    }
}

void handle_pidfile()
{
    std::string pfn = arg_pidfile.get();
    if (pfn.empty())
    {
        return;
    }
    if (pfn[0] != '/') {
        pfn = arg_store.get() + "/" + pfn;
    }
    pidfilepath = ::strdup(pfn.c_str());
    int fd = ::open(pidfilepath, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
    {
        std::cerr << "Could not create pidfile " << pfn << ": exiting." << std::endl;
        exit(1);
    }
    if (arg_verbose.get())
    {
        std::cerr << "Creating PID file: " << pfn << std::endl;
    }
    ::atexit(remove_pidfile);
    char buf[20];
    sprintf(buf, "%d\n", getpid());
    if (::write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf))
    {
        std::cerr << "Error writing pidfile: " << pfn << std::endl;
        exit(1);
    }
    ::close(fd);
}


void test_marshal()
{
    std::vector<char> a, b;
    a.resize(4, 0);
    for (int i = 0; i < 256; ++i)
    {
        marshal_integral(i, a);
    }
    for (int i = 256; i < 1000000000; i = i * 2)
    {
        marshal_integral(i, a);
    }
    size_t offset = 4;
    for (int i = 0; i < 256; ++i)
    {
        int j = demarshal_integral(a, offset);
        if (i != j)
        {
            std::cerr << "error in demarshal: " << i << " != " << j << std::endl;
            exit(1);
        }
    }
    for (int i = 256; i < 1000000000; i = i * 2)
    {
        int j = demarshal_integral(a, offset);
        if (i != j)
        {
            std::cerr << "error in demarshal: " << i << " != " << j << std::endl;
            exit(1);
        }
    }
    if (offset != a.size())
    {
        std::cerr << "error in demarshal; offset " << offset << " != size " << a.size() << std::endl;
    }

    char const *strs[] = {
        "",
        "20,000 freaks!",
        "This is a \377 escaped value.",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    };
    for (size_t i = 0; i != sizeof(strs)/sizeof(strs[0]); ++i)
    {
        marshal_string(strs[i], b);
    }
    std::string dem;
    offset = 0;
    for (size_t i = 0; i != sizeof(strs)/sizeof(strs[0]); ++i)
    {
        dem = demarshal_string(b, offset);
        if (offset == (size_t)-1 || dem != strs[i])
        {
            std::cerr << "demarshal string " << i << " failed." << std::endl;
            exit(1);
        }
    }
}


int main(int argc, char const *argv[])
{
    signal(SIGQUIT, doquit);
    signal(SIGINT, doquit);

    //  needed for time math to be reasonable
    setenv("TZ", "", 1);

    int ix = 1;
    while (ArgumentBase::parseArg(argc, argv, ix))
    {
        // keep going
    }
    if (argv[ix])
    {
        std::cerr << "Command line error near " << argv[ix] << std::endl;
        usage();
    }
    verbose = arg_verbose.get();
    debug = arg_debug.get();
    if (arg_test.get())
    {
        test_marshal();
        std::cerr << "Tests passed." << std::endl;
        exit(0);
    }
    handle_pidfile();

    gMmap = istat::NewMmap();

    try
    {
        if (arg_listen.get())
        {
            do_server();
        }
        else
        {
            do_client();
        }
    }
    catch (std::exception const &x)
    {
        std::cerr << "Exception: " << x.what() << std::endl;
    }
    if (gotsig)
    {
        std::cerr << "Exited because of signal " << gotsig << std::endl;
    }
    if (arg_verbose.get() || debug)
    {
        std::cerr << "Done in main()." << std::endl;
    }

    return 0;
}

