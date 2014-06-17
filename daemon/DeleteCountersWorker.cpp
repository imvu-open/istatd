#include "DeleteCountersWorker.h"
#include "StatServer.h"
#include "Logs.h"

void DeleteCountersWorker::go()
{
    LogNotice << "DeleteCountersWorker::go() deleting a counter: T-" << counters_.size();
    if (counters_.empty())
    {
        conn_->writeOut(std::string("ok\r\n"));
        delete this;
        return;
    }
    std::string ctr(counters_.front());
    counters_.pop_front();
    delete_one_counter(ctr);
}

void DeleteCountersWorker::on_complete()
{
    LogNotice << "DeleteCountersWorker::on_complete()";
    go();
}

void DeleteCountersWorker::delete_one_counter(std::string const &ctr)
{
    conn_->writeOut(ctr + "\r\n");
    statServer_->deleteCounter(ctr, this);
}

