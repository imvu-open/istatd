#include "DeletePatternsWorker.h"
#include "StatServer.h"
#include "Logs.h"

void DeletePatternsWorker::go()
{
    LogNotice << "DeletePatternsWorker::go() deleting a pattern: T-" << patterns_.size();
    if (patterns_.empty())
    {
        conn_->writeOut(std::string("ok\r\n"));
        delete this;
        return;
    }
    std::string ctr(patterns_.front());
    patterns_.pop_front();
    delete_one_pattern(ctr);
}

void DeletePatternsWorker::on_complete()
{
    LogNotice << "DeletePatternsWorker::on_complete()";
    go();
}

void DeletePatternsWorker::delete_one_pattern(std::string const &pattern)
{
    conn_->writeOut(pattern + "\r\n");
    statServer_->deletePattern(pattern, this);
}

