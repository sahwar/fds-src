/*
 * Copyright 2015 Formation Data Systems, Inc.
 */

#ifndef SOURCE_STOR_MGR_INCLUDE_MIGRATIONEXECUTOR_H_
#define SOURCE_STOR_MGR_INCLUDE_MIGRATIONEXECUTOR_H_

#include <memory>
#include <mutex>
#include <set>

#include <fds_types.h>
#include <SmIo.h>
#include <MigrationUtility.h>

namespace fds {

class EPSvcRequest;

/**
 * Callback to notify that migration executor is done with migration
 */
typedef std::function<void (fds_uint64_t executorId,
                            fds_token_id smToken,
                            fds_bool_t isFirstRound,
                            const Error& error)> MigrationExecutorDoneHandler;

class MigrationExecutor {
  public:
    MigrationExecutor(SmIoReqHandler *_dataStore,
                      fds_uint32_t bitsPerToken,
                      const NodeUuid& srcSmId,
                      fds_token_id smTokId,
                      fds_uint64_t id,
                      fds_uint64_t targetDltVer,
                      bool forResync,
                      MigrationExecutorDoneHandler doneHandler);
    ~MigrationExecutor();

    typedef std::unique_ptr<MigrationExecutor> unique_ptr;

    enum MigrationExecutorState {
        ME_INIT,
        ME_FIRST_PHASE_REBALANCE_START,
        ME_FIRST_PHASE_APPLYING_DELTA,
        ME_SECOND_PHASE_REBALANCE_START,
        ME_SECOND_PHASE_APPLYING_DELTA,
        ME_DONE,
        ME_ERROR
    };

    inline fds_uint64_t getId() const {
        return executorId;
    }
    inline MigrationExecutorState getState() const {
        return std::atomic_load(&state);
    }
    inline fds_bool_t isRoundDone(fds_bool_t isFirstRound) const {
        if (isFirstRound) {
            return (std::atomic_load(&state) == ME_SECOND_PHASE_REBALANCE_START);
        }
        return (std::atomic_load(&state) == ME_DONE);
    }
    inline fds_bool_t isDone() const {
        return (std::atomic_load(&state) == ME_DONE);
    }

    /**
     * Adds DLT token to the list of DLT tokens for which this
     * MigrationExecutor is responsible for
     * Can only be called before startObjectRebalance
     */
    void addDltToken(fds_token_id dltTok);

    /**
     * Start the object rebalance.  The rebalance inintiated by the
     * destination SM.
     */
    Error startObjectRebalance(leveldb::ReadOptions& options,
                               leveldb::DB *db);

    Error startSecondObjectRebalanceRound();

    /**
     * Handles message from Source SM to apply delta set to this SM
     */
    Error applyRebalanceDeltaSet(fpi::CtrlObjectRebalanceDeltaSetPtr& deltaSet);

  private:
    /**
     * Callback when apply delta set QoS message execution is completed
     */
    void objDeltaAppliedCb(const Error& error,
                           SmIoApplyObjRebalDeltaSet* req);

    /**
     * Called to finish up (abort with error or complete) first round
     * or second round of migration; finishing second round finishes
     * migration; error aborts migration
     */
    void handleMigrationRoundDone(const Error& error);

    /// callback from SL on rebalance filter set msg
    void objectRebalanceFilterSetResp(EPSvcRequest* req,
                                      const Error& error,
                                      boost::shared_ptr<std::string> payload);

    /// callback from SL on second rebalance delta set msg response
    void getSecondRebalanceDeltaResp(EPSvcRequest* req,
                                     const Error& error,
                                     boost::shared_ptr<std::string> payload);


    // send finish resync msg to source SM for corresponding client
    void sendFinishResyncToClient();

    // callback from SL on response for finish client resync message
    void finishResyncResp(EPSvcRequest* req,
                          const Error& error,
                          boost::shared_ptr<std::string> payload);

    /// Id of this executor, used for communicating with source SM
    fds_uint64_t executorId;

    /// state of this migration executor
    std::atomic<MigrationExecutorState> state;

    /// callback to notify that migration finished
    MigrationExecutorDoneHandler migrDoneHandler;

    /**
     * Object data store handler.  Set during the initialization.
     */
    SmIoReqHandler *dataStore;

    /**
     * Source SM id
     */
    NodeUuid sourceSmUuid;

    /**
     * SM Token to migration from the source SM node.
     */
    fds_token_id smTokenId;

    /**
     * Target DLT version for this executor
     */
    fds_uint64_t targetDltVersion;

    /**
     * Set of DLT tokens that needs to be migrated from source SM
     * SM token contains one or more DLT tokens
     */
    std::set<fds_token_id> dltTokens;
    fds_uint32_t bitsPerDltToken;

    /**
     * Maintain messages from the source SM, so we don't lose it.  Each async message
     * from source SM has a unique sequence number.
     */
    MigrationDoubleSeqNum seqNumDeltaSet;

    /**
     * Is this migration for a SM resync
     */
     bool forResync;

    /// true if standalone (no rpc sent)
    fds_bool_t testMode;
};

}  // namespace fds

#endif  // SOURCE_STOR_MGR_INCLUDE_MIGRATIONEXECUTOR_H_
