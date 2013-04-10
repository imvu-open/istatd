
#include "AdminServer.h"
#include "HttpServer.h"
#include "StatServer.h"
#include "IStatStore.h"
#include "EagerConnectionFactory.h"
#include "EagerConnection.h"
#include "Logs.h"
#include "IComplete.h"
#include "ReplicaServer.h"
#include "ReplicaOf.h"
#include "Debug.h"
#include "Settings.h"

#include <istat/strfunc.h>
#include <istat/istattime.h>

#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

DebugOption debugAdmin("admin");

class AdminConnection : public boost::noncopyable
{
public:
    AdminConnection(boost::shared_ptr<ConnectionInfo> const &ec, AdminServer *as);

    void go();
    void on_disconnect();
    void on_data();
    void store_flush_complete();
    void settings_flush_complete();
    void flush_complete();
    void sync_complete();

    void doCmd(std::string const &cmd);
    void huh();
    void cmdArgs(std::string const &cmd, std::vector<std::string> const &args);

    boost::shared_ptr<ConnectionInfo> ec_;
    AdminServer *as_;
};


AdminServer::AdminServer(unsigned int port, std::string listen_address, boost::asio::io_service &svc, IHttpServerInfo *hsp, StatServer *ssp, ReplicaServer *rs, ReplicaOf *ro) :
    svc_(svc),
    hsp_(hsp),
    ssp_(ssp),
    rs_(rs),
    ro_(ro),
    numAdminConnections_("admin.connections", TypeEvent),
    numAdminCommands_("admin.commands", TypeEvent),
    fac_(new EagerConnectionFactory(svc))
{
    LogNotice << "AdminServer(" << port << ")";
    if (port == 0 || port > 65535)
    {
        throw std::runtime_error("Bad port number in AdminServer: " + boost::lexical_cast<std::string>(port));
    }
    fac_->onConnection_.connect(boost::bind(&AdminServer::on_connection, this));
    fac_->listen(port, listen_address);
}

AdminServer::~AdminServer()
{
}

void AdminServer::on_connection()
{
    while (true)
    {
        boost::shared_ptr<ConnectionInfo> conn(fac_->nextConn());
        if (!conn)
        {
            break;
        }
        ++numAdminConnections_;
        (new AdminConnection(conn, this))->go();
    }
}


AdminConnection::AdminConnection(boost::shared_ptr<ConnectionInfo> const &ec, AdminServer *as) :
    ec_(ec),
    as_(as)
{
    LogDebug << "AdminConnection::AdminConnection()";
}

void AdminConnection::go()
{
    LogNotice << "AdminConnection::go()";
    ec_->onDisconnect_.connect(boost::bind(&AdminConnection::on_disconnect, this));
    ec_->onData_.connect(boost::bind(&AdminConnection::on_data, this));
    ec_->asEagerConnection()->startRead();
}

void AdminConnection::on_disconnect()
{
    ec_->close();
    LogNotice << "AdminConnection::on_disconnect()";
    delete this;
}

void AdminConnection::on_data()
{
    char buf[1026];
again:
    size_t sz = ec_->peekIn(buf, 1024);
    if (sz == 1024)
    {
        buf[1024] = '\n';
        buf[0] = 0;
    }
    else
    {
        buf[sz] = 0;
    }
    for (size_t i = 0; i != sz; ++i)
    {
        if (buf[i] == '\n')
        {
            ec_->consume(i + 1);
            doCmd(std::string(&buf[0], &buf[i]));
            goto again;
        }
    }
}

void AdminConnection::doCmd(std::string const &cmd)
{
    (debugAdmin.enabled() ? LogNotice : LogDebug) << "AdminConnection::doCmd(" << istat::sql_quote(cmd) << ")";
    ++as_->numAdminCommands_;
    std::string left, right;
    switch (istat::split(cmd, ' ', left, right))
    {
        case 0:
            huh();
            break;
        case 1:
        case 2:
            {
                std::vector<std::string> args;
                istat::trim(left);
                istat::trim(right);
                istat::explode(right, ',', args);
                cmdArgs(left, args);
            }
            break;
    }
}

void AdminConnection::huh()
{
    LogDebug << "AdminConnection::huh()";
    ec_->writeOut("huh? ('help' for help)\r\n");
}

void AdminConnection::cmdArgs(std::string const &cmd, std::vector<std::string> const &args)
{
    LogDebug << "AdminConnection::cmdArgs(" << cmd << ", ...)";
    if (cmd == "help")
    {
        ec_->writeOut("Commands:\r\n");
        ec_->writeOut("help - display this help\r\n");
        ec_->writeOut("stats - display stats\r\n");
        ec_->writeOut("flush - flush all counters\r\n");
        ec_->writeOut("faketime value - update faked time (for testing)\r\n");
        ec_->writeOut("purge [maxOld[,maxAge]] - purge old connection info records\r\n");
        ec_->writeOut("debug [option[,on|off]] - display or toggle debug options\r\n");
        ec_->writeOut("loglevel [level[,stderrLevel]] - display or change logging verbosity\r\n");
		ec_->writeOut("ignore name - ignore all updates for current session\r\n");
        ec_->writeOut("quit - disconnect from stat server\r\n");
        ec_->writeOut("ok\r\n");
        return;
    }
    if (cmd == "stats")
    {
        std::stringstream ss;
        if (as_->hsp_)
        {
            HttpServerInfo hsi;
            as_->hsp_->getInfo(hsi);
            ss << "http.port=" << hsi.port << "\r\n";
            ss << "http.current=" << hsi.current << "\r\n";
        }
        if (as_->rs_)
        {
            ReplicaServerInfo rsinfo;
            as_->rs_->getInfo(rsinfo);
            ss << "replicaServer.port=" << rsinfo.port << "\r\n";
            ss << "replicaServer.current=" << rsinfo.numConnections << "\r\n";
        }
        if (as_->ro_)
        {
            ReplicaOfInfo roinfo;
            as_->ro_->getInfo(roinfo);
            ss << "replicaOf.source=" << roinfo.source << "\r\n";
            ss << "replicaOf.connected=" << roinfo.connected << "\r\n";
            ss << "replicaOf.queue=" << roinfo.queueLength << "\r\n";
        }
        if (istat::Env::has<istat::FakeTime>())
        {
            time_t t = 0;
            istat::istattime(&t);
            ss << "fakeTime.value=" << t << "\r\n";
        }
        ec_->writeOut(ss.str());
        ec_->writeOut("ok\r\n");
        return;
    }
    if (cmd == "flush")
    {
        LoopbackCounter::forceUpdates();
        if (!as_->ssp_)
        {
            ec_->writeOut("huh? no statstore to flush.\r\n");
            return;
        }
        boost::shared_ptr<IStatStore> st(as_->ssp_->store());
        if (!st)
        {
            store_flush_complete();
        }
        else
        {
            st->flushAll(heap_complete<AdminConnection, &AdminConnection::store_flush_complete>(this));
        }
        return;
    }
    if (cmd == "purge")
    {
        try
        {
            size_t maxCnt = (args.size() >= 1) ? boost::lexical_cast<size_t>(args[0]) : 0;
            time_t maxAge = (args.size() >= 2) ? boost::lexical_cast<time_t>(args[1]) : 60;
            as_->ssp_->purgeOldMetaRecords(maxCnt, maxAge);
        }
        catch (std::exception const &x)
        {
            for (std::vector<std::string>::const_iterator ptr(args.begin()), end(args.end());
                ptr != end; ++ptr)
            {
                ec_->writeOut(*ptr + ", ");
            }
            ec_->writeOut("\r\n");
            ec_->writeOut(std::string("huh? ") + x.what() + "\r\n");
            return;
        }
        ec_->writeOut("ok\r\n");
        return;
    }

	if (cmd == "ignore")
    {
        try
        {
			if (args.size() > 1) 
			{
				throw std::runtime_error("too many arguments");
			}
			std::string name = args[0];
			as_->ssp_->ignore(name);			
        }
        catch (std::exception const &x)
        {
            for (std::vector<std::string>::const_iterator ptr(args.begin()), end(args.end());
                ptr != end; ++ptr)
            {
                ec_->writeOut(*ptr + ", ");
            }
            ec_->writeOut("\r\n");
            ec_->writeOut(std::string("huh? ") + x.what() + "\r\n");
            return;
        }
        ec_->writeOut("ok\r\n");
        return;
    }

    if (cmd == "debug")
    {
        try
        {
            if (args.size() >= 3)
            {
                throw std::runtime_error("too many arguments");
            }
            DebugOption const *opt = DebugOption::first();
            while (opt)
            {
                if ((args.size() < 1) || (args[0] == opt->name()))
                {
                    if (args.size() >= 2)
                    {
                        if (args[1] == "on")
                        {
                            (const_cast<DebugOption *>(opt))->set(true);
                        }
                        else if (args[1] == "off")
                        {
                            (const_cast<DebugOption *>(opt))->set(false);
                        }
                        else
                        {
                            throw std::runtime_error("Should be 'on' or 'off': " + args[2]);
                        }
                    }
                    ec_->writeOut(opt->name() + ": " + (opt->enabled() ? "on" : "off") + "\r\n");
                }
                opt = DebugOption::next(opt);
            }
        }
        catch (std::exception const &x)
        {
            ec_->writeOut(std::string("huh? ") + x.what() + "\r\n");
            return;
        }
        ec_->writeOut("ok\r\n");
        return;
    }
    if (cmd == "faketime")
    {
        if (!istat::Env::has<istat::FakeTime>())
        {
            ec_->writeOut("huh? Not started with --fake-time.\r\n");
            return;
        }
        if (args.size() != 1)
        {
            ec_->writeOut("huh? requires one argument (timestamp)\r\n");
            return;
        }
        time_t t = boost::lexical_cast<time_t>(args[0]);
        istat::Env::get<istat::FakeTime>().set(t);
        ec_->writeOut("ok\r\n");
    }
    if (cmd == "loglevel")
    {
        if (args.size() >= 3)
        {
            ec_->writeOut("huh? too many arguments\r\n");
            return;
        }
        try
        {
            if (args.size() >= 1)
            {
                int i = boost::lexical_cast<int>(args[0]);
                if (i < 0 || i > 5)
                {
                    ec_->writeOut("huh? loglevel must be between 0 and 5\r\n");
                    return;
                }
                istat::LogConfig::setLogLevel((istat::LogLevel)i);
            }
            if (args.size() >= 2)
            {
                int i = boost::lexical_cast<int>(args[1]);
                if (i < 0 || i > 5)
                {
                    ec_->writeOut("huh? loglevel must be between 0 and 5\r\n");
                    return;
                }
                istat::LogConfig::setStderrLogLevel((istat::LogLevel)i);
            }
            istat::LogLevel a = istat::LL_Error, b = istat::LL_Error;
            istat::LogConfig::getLogLevels(a, b);
            ec_->writeOut("loglevel " + boost::lexical_cast<std::string>((int)a) + " " + 
                boost::lexical_cast<std::string>((int)b) + "\r\n");
            ec_->writeOut("ok\r\n");
        }
        catch (std::exception const &x)
        {
            ec_->writeOut(std::string("huh? ") + x.what() + "\r\n");
        }
        return;
    }
    if (cmd == "quit")
    {
        ec_->close();
        return;
    }
    ec_->writeOut("huh? ('help' for help) (received '" + cmd + "')\r\n");
}

void AdminConnection::store_flush_complete()
{
    ISettingsFactory &sfac = istat::Env::get<ISettingsFactory>();
    sfac.flush(heap_complete<AdminConnection, &AdminConnection::settings_flush_complete>(this));
}

void AdminConnection::settings_flush_complete()
{
    flush_complete();
}

void AdminConnection::flush_complete()
{
    LogDebug << "AdminConnection::flush_complete()";
    as_->ssp_->syncAgent(heap_complete<AdminConnection, &AdminConnection::sync_complete>(this));
}

void AdminConnection::sync_complete()
{
    LogDebug << "AdminConnection::sync_complete()";
    ec_->writeOut("ok\r\n");
}

