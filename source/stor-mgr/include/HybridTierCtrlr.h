/*
 * Copyright 2015 Formation Data Systems, Inc.
 */

#ifndef SOURCE_STOR_MGR_INCLUDE_HYBRIDTIERCTRLR_H_
#define SOURCE_STOR_MGR_INCLUDE_HYBRIDTIERCTRLR_H_

#include <set>
#include <fds_types.h>
#include <SmIo.h>
#include <object-store/SmDiskMap.h>

namespace fds {

/* Forward declarations */
class CommonModuleProviderIf;
class fds_threadpool;
class SmIoReqHandler;
class FdsTimerTask;
class fds_mutex;
typedef boost::shared_ptr<FdsTimerTask> FdsTimerTaskPtr;

/**
* @brief Class responsible for enforching hybrid tiering policy
* NOTE: This implementation is valid for beta-2 timeframe.
*/
struct HybridTierCtrlr {
    enum HTCState {
        HTC_STOPPED,
        HTC_SCHEDULED,
        HTC_READY,
        HTC_INPROGRESS
    };

    HybridTierCtrlr(SmIoReqHandler* storMgr,
                    SmDiskMap::ptr diskMap);
    void enableFeature();
    void start(bool manual=false);
    void stop();

    void run();
    void moveToNextToken();
    void snapToken();
    void constructTierMigrationList();
    void snapTokenCb(const Error& err,
                     SmIoSnapshotObjectDB* snapReq,
                     leveldb::ReadOptions& options,
                     leveldb::DB* db);
    void moveObjsToTierCb(const Error& e,
                          SmIoMoveObjsToTier *req);
 protected:
    void initMoveTierRequest_();
    void scheduleNextRun_(uint32_t nextRunInSeconds);

    static uint32_t BATCH_SZ;
    static uint32_t FREQUENCY;

    fds_mutex hybridTierLock;
    bool featureEnabled;
    HTCState state_;

    fds_threadpool *threadpool_;
    SmIoReqHandler* storMgr_;
    SmDiskMap::ptr diskMap_;
    FdsTimerTaskPtr runTask_;
    std::set<fds_token_id> tokenSet_;
    std::set<fds_token_id>::iterator nextToken_;
    std::unique_ptr<SMTokenItr> tokenItr_;
    SmIoSnapshotObjectDB snapRequest_;
    SmIoMoveObjsToTier *moveTierRequest_;
    uint64_t hybridMoveTs_;
};
} // namespace fds
#endif  // SOURCE_STOR_MGR_INCLUDE_HYBRIDTIERCTRLR_H_
