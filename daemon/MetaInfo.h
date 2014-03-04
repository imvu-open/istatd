
#if !defined(daemon_MetaInfo_h)
#define daemon_MetaInfo_h

#include <boost/asio/ip/tcp.hpp>
#include <tr1/unordered_map>

struct MetaInfo
{
    typedef std::tr1::unordered_map<std::string, std::string> MetaInfoDataMap;
    bool                    online_;
    time_t                  connected_;
    time_t                  activity_;
    MetaInfoDataMap         info_;
};

#endif  //  daemon_MetaInfo_h

