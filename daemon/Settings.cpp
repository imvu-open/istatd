
#include "Settings.h"
#include "IComplete.h"
#include "LoopbackCounter.h"
#include "Logs.h"
#include "threadfunc.h"
#include "istat/strfunc.h"
#include "Debug.h"

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>

#include <map>
#include <set>
#include <iostream>
#include <fstream>

#include <sys/types.h>
#include <dirent.h>

LoopbackCounter countGets("settings.get", TypeEvent);
LoopbackCounter countSets("settings.set", TypeEvent);
LoopbackCounter countMatches("settings.match", TypeEvent);
LoopbackCounter countSaves("settings.save", TypeEvent);

DebugOption debugSettings("settings");


class RealSettingsFactory;

class RealSettings : public ISettings
{
public:
    RealSettings(RealSettingsFactory *rsf, std::string const &name);

    RealSettingsFactory *fac_;
    std::string name_;
    lock lock_;
    boost::asio::deadline_timer saveTimer_;
    std::map<std::string, std::string> settings_;
    bool saveQueued_;
    bool dirty_;

    void match(std::string const &pattern, std::vector<std::string> &oKeys);
    void get(std::string const &key, std::string &oValue, bool &wasSet, std::string const &dflt);
    void set(std::string const &key, std::string const &value);

    void queueSave();
    void save(boost::shared_ptr<ISettings> const &me);
};


class RealSettingsFactory : public ISettingsFactory
{
public:
    RealSettingsFactory(boost::asio::io_service &svc, std::string const &path);
    void scandir();
    void flush(IComplete *complete);
    void flush_one(std::string const &domain, IComplete *complete);
    void dispose();
    void loadSettingsPath(std::string const &path, std::string const &name);
    void reloadSettings(std::string const &domain, IComplete *complete);
    void open(std::string const &domain, bool allowCreate,
        boost::shared_ptr<ISettings> &settings, IComplete *complete);
    void get(std::string const &domain, boost::shared_ptr<ISettings> &settings);

    boost::asio::io_service &svc_;
    std::string path_;
    lock lock_;
    std::map<std::string, boost::shared_ptr<ISettings> > files_;

};

RealSettingsFactory::RealSettingsFactory(boost::asio::io_service &svc, std::string const &path) :
    svc_(svc),
    path_(path)
{
    LogDebug << "RealSettingsFactory::RealSettingsFactory(" << path << ")";
    try
    {
        boost::filesystem::create_directories(path);
    }
    catch (std::exception const &x)
    {
        LogWarning << "create_directories(" << path << ")" << x.what();
    }
    scandir();
}

void RealSettingsFactory::scandir()
{
    DIR *d = opendir(path_.c_str());
    if (!d)
    {
        throw std::runtime_error("settings: cannot open: " + path_);
    }
    try
    {
        (debugSettings ? LogNotice : LogDebug) << "settings scanning" << path_;
        while (struct dirent *dent = readdir(d))
        {
            if (dent->d_name[0] == '.')
            {
                continue;
            }
            char const *dot = strrchr(dent->d_name, '.');
            if (dot && !strcmp(dot, ".set"))
            {
                loadSettingsPath(path_ + "/" + dent->d_name, std::string(dent->d_name, dot-dent->d_name));
            }
        }
    }
    catch (...)
    {
        closedir(d);
        throw;
    }
    closedir(d);
}

void RealSettingsFactory::flush(IComplete *complete)
{
    (debugSettings ? LogNotice : LogDebug) << "RealSettingsFactory::flush()";
    //  no need to asynchronize the actual work more than this
    //  because file I/O is not async
    grab aholdof(lock_);
    for (std::map<std::string, boost::shared_ptr<ISettings> >::iterator
        ptr(files_.begin()), end(files_.end());
        ptr != end;
        ++ptr)
    {
        static_cast<RealSettings *>((*ptr).second.get())->save((*ptr).second);
    }
    //  this allows all settings to be re-loaded if edited
    files_.clear();
    svc_.post(boost::bind(&IComplete::on_complete, complete));
}

void RealSettingsFactory::flush_one(std::string const &name, IComplete *complete)
{
    svc_.post(boost::bind(&IComplete::on_complete, complete));
    (debugSettings ? LogNotice : LogDebug) << "RealSettingsFactory::flush()";
    //  no need to asynchronize the actual work more than this
    //  because file I/O is not async
    grab aholdof(lock_);
    std::map<std::string, boost::shared_ptr<ISettings> >::iterator ptr(files_.find(name));
    if (ptr != files_.end()) {
        static_cast<RealSettings *>((*ptr).second.get())->save((*ptr).second);
        files_.erase(ptr);
    }
    svc_.post(boost::bind(&IComplete::on_complete, complete));
}

void RealSettingsFactory::dispose()
{
    (debugSettings ? LogNotice : LogDebug) << "RealSettingsFactory::flush()";
    delete this;
}

void RealSettingsFactory::reloadSettings(std::string const &domain, IComplete *complete)
{
    (debugSettings ? LogNotice : LogDebug) << "RealSettingsFactory::reloadSettings( " << domain << " )";
    std::string fullPath(path_ + "/" + domain + ".set");
    grab aholdof(lock_);
    try
    {
        loadSettingsPath(fullPath, domain);
    }
    catch (std::exception const &x)
    {
        LogError << "settings: cannot load:" << domain << ":" << x.what();
    }
    svc_.post(boost::bind(&IComplete::on_complete, complete));
}

void RealSettingsFactory::loadSettingsPath(std::string const &path, std::string const &name)
{
    (debugSettings ? LogNotice : LogDebug) << "RealSettingsFactory::loadSettingsPath(" << path << "," << name << ")";
    int i = ::open(path.c_str(), O_RDONLY);
    if (i == -1)
    {
        throw std::runtime_error("settings: Could not open file: " + path);
    }
    int64_t size = ::lseek(i, 0, 2);
    if (size > 1024*1024)
    {
        ::close(i);
        //  we're not trying to emulate Redis here!
        throw std::runtime_error("settings: file is too big: " + path);
    }
    ::lseek(i, 0, 0);
    std::vector<char> buf;
    buf.resize(size + 1);
    if (size != ::read(i, &buf[0], size))
    {
        ::close(i);
        throw std::runtime_error("settings: cannot read file: " + path);
    }
    ::close(i);
    buf[size] = '\n';
    if (debugSettings)
    {
        LogDebug << size << "bytes";
    }
    char *base = &buf[0];
    char *end = &buf[size];
    boost::shared_ptr<ISettings> set;
    std::map<std::string, boost::shared_ptr<ISettings> >::iterator ptr(files_.find(name));
    if (ptr == files_.end())
    {
        set = boost::shared_ptr<ISettings>(new RealSettings(this, name));
        files_[name] = set;
    }
    else
    {
        set = (*ptr).second;
    }
    RealSettings *rs = static_cast<RealSettings *>(set.get());
    grab aholdof(rs->lock_);
    for (char *cur = base; cur <= end; ++cur)
    {
        if (*cur == '\n')
        {
            //  only non-empty, non-comment lines need apply
            if (*base != '#' && cur > base)
            {
                std::string right(base, cur);
                std::string left;
                if (2 == istat::split(right, '=', left, right))
                {
                    istat::trim(left);
                    right = istat::sql_unquote(right);
                    istat::trim(right);
                    (debugSettings ? LogDebug : LogSpam) << left << "=" << right;
                    rs->settings_[left] = right;
                }
                else if (debugSettings)
                {
                    LogDebug << "could not split line:" << std::string(base, cur);
                }
            }
            base = cur + 1;
        }
    }
}

void RealSettingsFactory::get(std::string const &domain, boost::shared_ptr<ISettings> &settings)
{
    (debugSettings ? LogNotice : LogDebug) << "RealSettingsFactory::get( " << domain << " )";
    settings = boost::shared_ptr<ISettings>((ISettings *)0);
    std::map<std::string, boost::shared_ptr<ISettings> >::iterator ptr(files_.find(domain));
    if (ptr != files_.end())
    {
        settings = (*ptr).second;
    }
}

void RealSettingsFactory::open(std::string const &domain, bool allowCreate, boost::shared_ptr<ISettings> &settings,
    IComplete *complete)
{
    (debugSettings ? LogNotice : LogDebug) << "RealSettingsFactory::open(" << domain << "," << allowCreate << ")";
    settings = boost::shared_ptr<ISettings>((ISettings *)0);
    try
    {
        std::string fullPath(path_ + "/" + domain + ".set");
        grab aholdof(lock_);
        std::map<std::string, boost::shared_ptr<ISettings> >::iterator ptr(files_.find(domain));
        if (ptr != files_.end())
        {
            settings = (*ptr).second;
        }
        else
        {
            int flags = allowCreate ? (O_RDWR | O_CREAT) : O_RDONLY;
            int i = ::open(fullPath.c_str(), flags, 511);
            if (i == -1)
            {
                throw std::runtime_error("settings: cannot create file: " + fullPath);
            }
            long l = ::lseek(i, 0, 2);
            if (l <= 0)
            {
                char const *hdr = "# istatd settings 1\n";
                if (::write(i, hdr, strlen(hdr)) != (ssize_t)strlen(hdr))
                {
                    ::close(i);
                    throw std::runtime_error("settings: cannot initialize file: " + fullPath);
                }
            }
            ::close(i);
            loadSettingsPath(fullPath, domain);
            ptr = files_.find(domain);
            assert(ptr != files_.end());
            settings = (*ptr).second;
        }
    }
    catch (std::exception const &x)
    {
        LogError << "settings: cannot open:" << domain << ":" << x.what();
    }
    svc_.post(boost::bind(&IComplete::on_complete, complete));
}




RealSettings::RealSettings(RealSettingsFactory *rsf, std::string const &name) :
    fac_(rsf),
    name_(name),
    saveTimer_(rsf->svc_),
    saveQueued_(false)
{
    LogDebug << "RealSettings::RealSettings(" << name << ")";
}

void RealSettings::match(std::string const &pattern, std::vector<std::string> &oKeys)
{
    (debugSettings ? LogDebug : LogSpam) << "RealSettings::match(" << name_ << "," << pattern << ")";
    ++countMatches;
    grab aholdof(lock_);
    for (std::map<std::string, std::string>::iterator ptr(settings_.begin()), end(settings_.end());
        ptr != end; ++ptr)
    {
        if (istat::str_pat_match((*ptr).first, pattern))
        {
            oKeys.push_back((*ptr).first);
            if (debugSettings)
            {
                LogDebug << "matched " << (*ptr).first;
            }
            else
            {
                LogDebug << "no match " << (*ptr).first;
            }
        }
    }
}

void RealSettings::get(std::string const &key, std::string &oValue, bool &wasSet, std::string const &dflt)
{
    (debugSettings ? LogDebug : LogSpam) << "RealSettings::get(" << name_ << "," << key << ")";
    ++countGets;
    oValue = dflt;
    wasSet = false;
    grab aholdof(lock_);
    std::map<std::string, std::string>::iterator ptr(settings_.find(key));
    if (ptr != settings_.end())
    {
        oValue = (*ptr).second;
        wasSet = true;
    }
}

void RealSettings::set(std::string const &key, std::string const &value)
{
    (debugSettings ? LogNotice : LogSpam) << "RealSettings::set(" << name_ << "," << key << "," << value << ")";
    ++countSets;
    //  a valid key?
    if (is_valid_key(key))
    {
        grab aholdof(lock_);
        dirty_ = true;
        settings_[key] = value;
        if (value.size() == 0)
        {
            settings_.erase(settings_.find(key));
        }
        queueSave();
    }
}

void RealSettings::queueSave()
{
    LogSpam << "RealSettings::queueSave(" << name_ << ")";
    grab aholdof(lock_);
    if (!saveQueued_)
    {
        saveQueued_ = true;
        //  possibly aggregate multiple updates into a single write
        saveTimer_.expires_from_now(boost::posix_time::seconds(10));
        saveTimer_.async_wait(boost::bind(&RealSettings::save, this, shared_from_this()));
    }
}

void RealSettings::save(boost::shared_ptr<ISettings> const &me)
{
    (debugSettings ? LogNotice : LogDebug) << "RealSettings::save(" << name_ << ")";
    grab aholdof(lock_);
    if (dirty_) {
        //  Only re-write if dirty. This allows a settings file to be edited on disk and re-loaded
        //  with a flush on the admin interface.
        std::string tmpName;
        std::string fileName;
        fileName = fac_->path_ + "/" + name_ + ".set";
        tmpName = fileName + ".tmp";
        {
            std::ofstream ostr(tmpName.c_str(), std::ios::binary | std::ios::out | std::ios::trunc);
            ostr << "# istatd settings 1" << std::endl;
            for (std::map<std::string, std::string>::iterator ptr(settings_.begin()), end(settings_.end());
                ptr != end; ++ptr)
            {
                if ((*ptr).second.size())
                {
                    //  empty string values are not saved -- same as "delete"
                    ostr << (*ptr).first << "=" << istat::sql_quote((*ptr).second) << std::endl;
                }
            }
        }
        //  Now, move the new file in place of the old.
        //  First, remove the old file, to generate an error if we don't have permission.
        if (boost::filesystem::exists(fileName))
        {
            boost::filesystem::remove(fileName);
        }
        boost::filesystem::rename(tmpName, fileName);
    }
    else
    {
        if (debugSettings.enabled())
        {
            LogNotice << "settings not dirty";
        }
    }
    saveQueued_ = false;
    dirty_ = false;
    ++countSaves;
}


ISettingsFactory *NewSettingsFactory(boost::asio::io_service &svc, std::string const &path)
{
    return new RealSettingsFactory(svc, path);
}



class FakeSettingsFactory;


class FakeSettings : public ISettings
{
public:
    FakeSettings(FakeSettingsFactory *fac, std::string const &fileName) :
        fac_(fac),
        fileName_(fileName)
    {
    }
    FakeSettingsFactory *fac_;
    lock lock_;
    std::string fileName_;
    std::map<std::string, std::string> settings_;
    void match(std::string const &pattern, std::vector<std::string> &oKeys);
    void get(std::string const &key, std::string &oValue, bool &wasSet, std::string const &dflt);
    void set(std::string const &key, std::string const &value);
};


class FakeSettingsFactory : public IFakeSettingsFactory
{
public:
    FakeSettingsFactory(boost::asio::io_service &svc) :
        svc_(svc)
    {
    }
    void open(
        std::string const &domain,
        bool allowCreate,
        boost::shared_ptr<ISettings> &settings,
        IComplete *complete)
    {
        grab aholdof(lock_);
        boost::shared_ptr<ISettings>((ISettings *)0).swap(settings);
        std::map<std::string, boost::shared_ptr<ISettings> >::iterator ptr(settings_.find(domain));
        std::map<std::string, boost::shared_ptr<ISettings> >::iterator saved(saved_.find(domain));
        if (ptr != settings_.end())
        {
            LogDebug << "FakeSettingsFactory::open(" << domain << ") getting from settings!";
            settings = (*ptr).second;
        }
        else if (allowCreate)
        {
            bool allowed = (allowCreate_.find(domain) != allowCreate_.end());
            if (allowed)
            {
                settings_[domain] = settings = boost::shared_ptr<ISettings>(new FakeSettings(this, domain));
            }
        }else if(saved != saved_.end())
        {
            LogDebug << "FakeSettingsFactory::open(" << domain << ") getting from saved";
            settings_[domain] = settings = (*saved).second;
        }
        svc_.post(boost::bind(&IComplete::on_complete, complete));
    }

    void reloadSettings(std::string const &domain , IComplete *complete)
    {
        std::map<std::string, boost::shared_ptr<ISettings> >::iterator saved(saved_.find(domain));
        if(saved != saved_.end())
        {
            settings_[domain] = (*saved).second;
            LogDebug << "FakeSettingsFactory::reloadSettings(" << domain << ") moving from saved to settings";
        }
        svc_.post(boost::bind(&IComplete::on_complete, complete));
    }

    void flush(IComplete *complete)
    {
        svc_.post(boost::bind(&IComplete::on_complete, complete));
    }
    void flush_one(std::string const &name, IComplete *complete)
    {
        grab aholdof(lock_);
        std::map<std::string, boost::shared_ptr<ISettings> >::iterator ptr = settings_.find(name);
        if (ptr != settings_.end())
        {
            saved_[name] = (*ptr).second;
            settings_.erase(ptr);
            LogDebug << "FakeSettingsFactory::flush_one(" << name << ") deleting old and adding to saved";
        }
        svc_.post(boost::bind(&IComplete::on_complete, complete));
    }
    void dispose()
    {
        delete this;
    }
    void allowCreate(std::string const &fileName)
    {
        allowCreate_.insert(fileName);
    }
    void preCreate(std::string const &fileName)
    {
        if (settings_.find(fileName) == settings_.end())
        {
            settings_[fileName] = boost::shared_ptr<FakeSettings>(new FakeSettings(this, fileName));
        }
    }
    void set(std::string const &fileName, std::string const &name, std::string const &value)
    {
        grab aholdof(lock_);
        preCreate(fileName);
        settings_[fileName]->set(name, value);
    }

    void get(std::string const &domain, boost::shared_ptr<ISettings> &settings)
    {
        settings = boost::shared_ptr<ISettings>((ISettings*)0);
        std::map<std::string, boost::shared_ptr<ISettings> >::iterator ptr(settings_.find(domain));
        if (ptr != settings_.end())
        {
            settings = (*ptr).second;
        }
    }

    void clear()
    {
        settings_.clear();
        saved_.clear();
        allowCreate_.clear();
    }
    boost::asio::io_service &svc_;
    lock lock_;
    std::map<std::string, boost::shared_ptr<ISettings> > settings_;
    std::map<std::string, boost::shared_ptr<ISettings> > saved_;
    std::set<std::string> allowCreate_;
};

IFakeSettingsFactory * NewFakeSettingsFactory(boost::asio::io_service &svc)
{
    return new FakeSettingsFactory(svc);
}


void FakeSettings::match(std::string const &pattern, std::vector<std::string> &oKeys)
{
    std::set<std::string> strs;
    grab aholdof(lock_);
    for (std::map<std::string, std::string>::iterator ptr(settings_.begin()), end(settings_.end());
        ptr != end; ++ptr)
    {
        if (istat::str_pat_match((*ptr).first, pattern))
        {
            strs.insert((*ptr).first);
        }
    }

    oKeys.insert(oKeys.end(), strs.begin(), strs.end());
}

void FakeSettings::get(std::string const &key, std::string &oValue, bool &wasSet, std::string const &dflt)
{
    grab aholdof(lock_);
    oValue = dflt;
    wasSet = false;

    std::map<std::string, std::string>::iterator ptr(settings_.find(key));
    if(ptr != settings_.end())
    {
        LogDebug << "FakeSettings::get ( " << key << (*ptr).second << " )";
        oValue = (*ptr).second;
        wasSet = true;
    }
}

void FakeSettings::set(std::string const &key, std::string const &value)
{
    if (is_valid_key(key))
    {
        grab aholdof(lock_);
        settings_[key] = value;
        if (value.size() == 0)
        {
            settings_.erase(settings_.find(key));
        }
    }
}


bool is_valid_key(std::string const &key)
{
    //  just ignore some particular set of characters -- they may come in handy later!
    return key.find_first_of(" \r\n\t\b\\/:;*?\"'%()[]{}~$^#=+-") == std::string::npos;
}

