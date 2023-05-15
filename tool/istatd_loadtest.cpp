
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <stdlib.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/time.h>


using namespace boost::asio;
using boost::asio::ip::tcp;

io_service svc_;
tcp::resolver resolver_(svc_);
tcp::socket socket_(svc_);
deadline_timer timer_(svc_);
std::vector<std::string> counterNames_;
bool volatile running_ = true;
size_t totalCount_;


void stop_running(int)
{
    running_ = false;
    timer_.cancel();
}


class ArgBase
{
public:
    ArgBase()
    {
        next_ = first_;
        first_ = this;
    }
    virtual char const *name() = 0;
    virtual void assign(char const *value) = 0;
    virtual char const *valueStr() = 0;
    static ArgBase *first_;
    ArgBase *next_;
};

ArgBase *ArgBase::first_;

template<typename T> class Arg : public ArgBase
{
public:
    T get() const { return val_; }
    operator T() const { return val_; }
    Arg &operator=(T t) { val_ = t; return *this; }
    char const *name() { return name_; }
    void assign(char const *value) { val_ = boost::lexical_cast<T>(value); }
    char const *valueStr() { vstr_ = boost::lexical_cast<std::string>(val_); return vstr_.c_str(); }
    Arg(char const *name, T dflt) :
        val_(dflt),
        name_(name)
    {
    }
    T val_;
    char const *name_;
    std::string vstr_;
};

Arg<int> num("num", 1000);
Arg<int> interval("interval", 10);
Arg<int> counters("counters", 10);
Arg<int> seed("seed", getpid());
Arg<std::string> host("host", "localhost:8111");

void usage()
{
    std::cerr << "istatd_loadtest options:" << std::endl;
    for (ArgBase *ab = ArgBase::first_; ab; ab = ab->next_)
    {
        std::cerr << "--" << ab->name() << " " << ab->valueStr() << std::endl;
    }
    exit(1);
}

char const *counterStems[] =
{
    "zero",
    "one",
    "two",
    "three",
    "four",
    "five",
    "six",
    "seven",
    "eight",
    "nine"
};

void parse_args(int &argc, char const *argv[])
{
    while (argv[1])
    {
        std::string str(argv[1]);
        if (str.substr(0, 2) != "--")
        {
            usage();
        }
        str = str.substr(2);
        bool found = false;
        for (ArgBase *ab = ArgBase::first_; ab; ab = ab->next_)
        {
            if (str == ab->name())
            {
                if (!argv[2])
                {
                    std::cerr << "Option needs an argument: " << argv[1] << std::endl;
                    usage();
                }
                try
                {
                    ab->assign(argv[2]);
                }
                catch (...)
                {
                    std::cerr << "Bad option value: " << argv[1] << " " << argv[2] << std::endl;
                    usage();
                }
                found = true;
                break;
            }
        }
        if (!found)
        {
            std::cerr << "Unknown option: " << argv[1] << std::endl;
            usage();
        }
        argv += 2;
        argc -= 2;
    }
}

void calculate_counters()
{
    std::vector<int> numbers;
    for (int i = 1000; i < 33768; ++i)
    {
        numbers.push_back(i);
    }
    //  the shuffle is the same for all seeds, even if the number 
    //  of counters used is different for each
    for (int i = 0; i < 32768; ++i)
    {
        std::swap(numbers[i], numbers[rand() & 0x7fff]);
    }
    numbers.resize(counters.get());
    for (size_t i = 0; i < numbers.size(); ++i)
    {
        std::string s;
        int val = numbers[i];
        do
        {
            int dig = val % 10;
            if (s.size())
            {
                s += ".";
            }
            s += counterStems[dig];
            val = val / 10;
        }
        while (val > 0);
        counterNames_.push_back(s);
    }
}

void send_sample(int ctrix);

void start_timer(int ctrix)
{
    timer_.expires_from_now(boost::posix_time::milliseconds(interval.get()));
    timer_.async_wait(boost::bind(&send_sample, ctrix));
}

void send_done(boost::shared_ptr<std::string> sp, boost::system::error_code const &err, size_t xfer)
{
    if (!!err)
    {
        std::cerr << "socket write error: " << err << std::endl;
        sigval sv;
        memset(&sv, 0, sizeof(sv));
        //  tell ourselves it's time to quit
        stop_running(0);
    }
    else
    {
    }
}

std::map<int, float> ctrvals;

float counter_value(int ctrix)
{
    float i = ((rand() & 0x7fff) - 16383.5f) / 16383.5f;
    float n = 1;
    switch (ctrix & 3)
    {
    case 0:
        // nothing
        break;
    case 1:
        n = 0.001;
        break;
    case 2:
        n = 1000;
        break;
    case 3:
        n = 1000000;
        break;
    }
    std::map<int, float>::iterator ptr(ctrvals.find(ctrix));
    if (ptr == ctrvals.end())
    {
        i = (i + 10) * n;
        ctrvals[ctrix] = i;
    }
    else
    {
        i = (*ptr).second + i * n;
        if (i < 0) i += n;
        if (i > 20 * n) i -= n;
        (*ptr).second = i;
    }
    return i;
}

void send_sample(int ctrix)
{
    boost::shared_ptr<std::string> sp = boost::make_shared<std::string>(counterNames_[ctrix]);
    (*sp) += " ";
    (*sp) += boost::lexical_cast<std::string>(counter_value(ctrix));
    (*sp) += "\r\n";
    async_write(socket_, buffer(sp->c_str(), sp->size()),
        boost::bind(&send_done, sp, placeholders::error, placeholders::bytes_transferred));
        
    ++ctrix;
    if (ctrix == (int)counterNames_.size())
    {
        ctrix = 0;
    }
    ++totalCount_;

    //  make sure there's no race in ctrl-C
    sigset_t sset;
    sigemptyset(&sset);
    sigaddset(&sset, SIGINT);
    sigaddset(&sset, SIGHUP);
    sigprocmask(SIG_BLOCK, &sset, NULL);
    if (0 < num.get() - totalCount_ && running_)
    {
        start_timer(ctrix);
    }
    sigemptyset(&sset);
    sigaddset(&sset, SIGINT);
    sigaddset(&sset, SIGHUP);
    sigprocmask(SIG_UNBLOCK, &sset, NULL);
}

void connect_socket()
{
    std::string h(host.get());
    size_t colon = h.find_first_of(':');
    if (colon == std::string::npos)
    {
        std::cerr << "Bad host argument: " << h << std::endl;
        usage();
    }
    std::string port = h.substr(colon+1);
    h.resize(colon);
    tcp::resolver::query q(h, port);
    tcp::resolver::iterator eiter(resolver_.resolve(q));
    tcp::resolver::iterator end;
    while (eiter != end)
    {
        socket_.close();
        boost::system::error_code err;
        tcp::endpoint addr(*eiter);
        socket_.connect(addr, err);
        if (!err)
        {
            std::cout << "Connected to " << addr << std::endl;
            return;
        }
        ++eiter;
    }
    std::cerr << "Could not connect to " << host.get() << std::endl;
    exit(2);
}

int main(int argc, char const *argv[])
{
    parse_args(argc, argv);
    if (num < 1 || num > 10000000)
    {
        std::cerr << "Bad value for 'num': " << num << std::endl;
        usage();
    }
    if (interval < 1 || interval > 100000)
    {
        std::cerr << "Bad value for 'interval': " << interval << std::endl;
    }
    if (counters < 1 || counters > 10000)
    {
        std::cerr << "Bad value for 'counters': " << counters << std::endl;
    }
    totalCount_ = 0;
    calculate_counters();

    connect_socket();

    //  run, waiting for clean shutdown signals, single-threaded
    struct sigaction sact;
    memset(&sact, 0, sizeof(sact));
    sact.sa_handler = stop_running;
    sigaction(SIGINT, &sact, 0);
    sigaction(SIGHUP, &sact, 0);
    start_timer(0);
    struct timeval tv1, tv2;
    gettimeofday(&tv1, NULL);
    svc_.run();
    gettimeofday(&tv2, NULL);

    if (!running_)
    {
        std::cerr << "Interrupted." << std::endl;
    }

    double seconds = (tv2.tv_sec - tv1.tv_sec) + (tv2.tv_usec - tv1.tv_usec) * 0.000001;
    std::cout << "Sent " << totalCount_ << " metrics in " << seconds << " seconds." << std::endl;
    return running_ ? 0 : 1;
}

