
#if !defined(daemon_MetaInfo_h)
#define daemon_MetaInfo_h

#include <boost/asio/ip/tcp.hpp>
#include <tr1/unordered_map>

struct MetaInfo
{
    bool                    online_;
    time_t                  connected_;
    time_t                  activity_;
    std::tr1::unordered_map<std::string, std::string>
                            info_;
};

#endif  //  daemon_MetaInfo_h

