
#if !defined(daemon_ReplicaPdus_h)
#define daemon_ReplicaPdus_h

#include <inttypes.h>
#include <vector>
#include <boost/signal.hpp>
#include <boost/noncopyable.hpp>
#include <stdexcept>

#include <istat/Header.h>
#include <istat/Bucket.h>

#include "threadfunc.h"
#include "IStatStore.h"
#include "EagerConnection.h"


enum {
    ReplicateProtocolVersion1 = 1
};
//  PDU == Protocol Data Unit

struct PduHeader
{
    uint32_t type;
    uint32_t size;
    std::vector<char> payload;
};

class PduOverflowException : public std::runtime_error
{
public:
    PduOverflowException(std::string const &what) : std::runtime_error(what) {}
};

class PduWriter
{
public:
    PduWriter(void *buf, size_t sz, size_t &oPos);
    PduWriter &write(void const *src, size_t sz);

    template<typename T> inline PduWriter &operator<<(T const &t)
    {
        write(&t, sizeof(t));
        return *this;
    }

private:
    //  can't write pointers
    template<typename T> PduWriter &operator<<(T const *t);
    void *buf_;
    size_t sz_;
    size_t &pos_;
};

template<> inline PduWriter &PduWriter::operator<< <std::string>(std::string const &s)
{
    write(s.c_str(), s.size() + 1);
    return *this;
}

template<uint32_t PduId>
struct PduBase
{
public:
    enum { Id = PduId };
};

struct PduConnect : PduBase<1>
{
    uint32_t Version;
    UniqueId ServerId;
    inline static PduConnect *make(char *buf, size_t sz, size_t &oPos, uint32_t version, UniqueId const &server)
    {
        PduWriter pw(buf, sz, oPos);
        pw << version;
        pw << server;
        return (PduConnect *)buf;
    }
};

struct PduDiscover : PduBase<2>
{
    istat::Header header;
    inline static PduDiscover *make(char *buf, size_t sz, size_t &oPos, istat::Header const &h)
    {
        PduWriter pw(buf, sz, oPos);
        pw << h;
        return (PduDiscover *)buf;
    }
};

struct PduRequest : PduBase<3>
{
    int64_t fromTime;
    int64_t numBuckets;
    // char name[1];
    inline std::string getName() const { return std::string((char *)(&numBuckets+1)); }
    inline static PduRequest *make(char *buf, size_t sz, size_t &oPos, int64_t fromTime, int64_t numBuckets, std::string const &name)
    {
        PduWriter pw(buf, sz, oPos);
        pw << fromTime;
        pw << numBuckets;
        pw << name;
        return (PduRequest *)buf;
    }
};

struct PduData : PduBase<4>
{
    int64_t numBuckets;
    istat::Bucket buckets[1];
    // char name[1]; // goes at end
    inline std::string getName() const { return std::string((char *)(&buckets[numBuckets])); }
    inline static PduData *make(char *buf, size_t sz, size_t &oPos, int64_t numBuckets, istat::Bucket const *buckets, std::string const &name)
    {
        PduWriter pw(buf, sz, oPos);
        pw << numBuckets;
        for (int64_t i = 0; i != numBuckets; ++i)
        {
            pw << buckets[i];
        }
        pw << name;
        return (PduData *)buf;
    }
};

struct PduError : PduBase<5>
{
    //  char text[1];
    inline std::string getText() const { return std::string((char const *)this); }
    inline static PduError *make(char *buf, size_t sz, size_t &oPos, std::string const &text)
    {
        PduWriter pw(buf, sz, oPos);
        pw << text;
        return (PduError *)buf;
    }
};

class IEagerConnection;

class PduReaderActor : public boost::noncopyable
{
public:
    PduReaderActor(boost::shared_ptr<ConnectionInfo> const &conn);
    ~PduReaderActor();
    bool nextPdu(PduHeader &got);

    boost::signal<void()> onPdu_;
    boost::signal<void()> onDeleted_;

private:
    void on_disconnect();
    void on_data();
    boost::shared_ptr<ConnectionInfo> conn_;
    lock lock_;
    std::list<PduHeader> pending_;
    bool pduComplete_;
};

#endif  //  daemon_ReplicaPdus_h
