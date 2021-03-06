/*
 * Copyright 2015 Formation Data Systems, Inc.
 */

#include <DataMgr.h>
#include <DmMigrationBase.h>

namespace fds {

DmMigrationBase::DmMigrationBase(int64_t migrationId, DataMgr &_dataMgr)
: dataMgr(_dataMgr)
{
    requestMgr = dataMgr.getModuleProvider()->getSvcMgr()->getSvcRequestMgr();
    this->migrationId = migrationId;
    std::stringstream ss;
    ss << " migrationid: " << migrationId << " ";
    logStr = ss.str();
}

std::string DmMigrationBase::logString() const
{
    return logStr;
}

void
DmMigrationBase::dmMigrationCheckResp(std::function<void()> abortFunc,
										std::function<void()> passFunc,
										EPSvcRequest *req,
										const Error& error,
										boost::shared_ptr<std::string> payload)
{
	LOGMIGRATE << logString()
        << "Request : " << req << " Received response with error: " << error;
	if (!error.ok()) {
		abortFunc();
	} else {
		passFunc();
	}
}

void DmMigrationBase::asyncMsgPassed()
{
    LOGDEBUG << logString() << " trackIO count-- is now: " << trackIOReqs.debugCount();
    dataMgr.counters->numberOfOutstandingIOs.decr(1);

    // do this last, as object may be deallocated immediately after
    trackIOReqs.finishTrackIOReqs();
}

void DmMigrationBase::asyncMsgFailed()
{
    LOGDEBUG << logString() << " trackIO count-- is now: " << trackIOReqs.debugCount();
    LOGERROR << logString() << " Async migration message failed, aborting";
    routeAbortMigration();
    dataMgr.counters->numberOfOutstandingIOs.decr(1);

    // do this last, as object may be deallocated immediately after
    trackIOReqs.finishTrackIOReqs();
}

void DmMigrationBase::asyncMsgIssued()
{
    trackIOReqs.startTrackIOReqs();
    LOGDEBUG << logString() << " trackIO count++ is now: " << trackIOReqs.debugCount();
    dataMgr.counters->numberOfOutstandingIOs.incr(1);
}

void DmMigrationBase::waitForAsyncMsgs()
{
    trackIOReqs.waitForTrackIOReqs();
}
} // namespace fds
