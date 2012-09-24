
#if !defined(daemon_Settings_h)
#define daemon_Settings_h

#include <string>
#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/noncopyable.hpp>
#include <boost/asio/io_service.hpp>

class IComplete;

class ISettings : public boost::enable_shared_from_this<ISettings>, public boost::noncopyable
{
public:
    virtual void match(std::string const &pattern, std::vector<std::string> &oKeys) = 0;
    virtual void get(std::string const &key, std::string &oValue, bool &wasSet, std::string const &dflt) = 0;
    virtual void set(std::string const &key, std::string const &value) = 0;
    virtual ~ISettings() {}
};

//  Typically, you get the ISettings from the istat::Env

class ISettingsFactory : public boost::noncopyable
{
public:
    virtual void open(std::string const &domain, bool allowCreate, boost::shared_ptr<ISettings> &settings,
        IComplete *complete) = 0;
    virtual void flush(IComplete *complete) = 0;
    virtual void dispose() = 0;
protected:
    virtual ~ISettingsFactory() {}
};

//  This struct captures calls into the fake settings factory
struct FakeSettingInfo
{
    FakeSettingInfo(bool o, std::string const &fn, std::string const &n, std::string const &v, std::string const &d = "");
    bool opIsSet;   //  else get
    std::string fileName;
    std::string name;
    std::string value;
    std::string dflt;
};

//  IFakeSettingsFactory allows you to prepare stuff.
class IFakeSettingsFactory : public ISettingsFactory
{
public:
    virtual void allowCreate(std::string const &fileName) = 0;
    virtual void preCreate(std::string const &fileName) = 0;
    virtual void set(std::string const &fileName, std::string const &name, std::string const &value) = 0;
    virtual void get(std::vector<FakeSettingInfo> &oAction) = 0;
    virtual void clear() = 0;
};

ISettingsFactory *NewSettingsFactory(boost::asio::io_service &svc, std::string const &path);
IFakeSettingsFactory *NewFakeSettingsFactory(boost::asio::io_service &svc);
bool is_valid_key(std::string const &key);

#endif  //  daemon_Settings_h
