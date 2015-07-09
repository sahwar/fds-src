/*
 * Copyright 2015 Formation Data Systems, Inc.
 */

#ifndef SOURCE_DATA_MGR_INCLUDE_DMMIGRATIONMGR_H_
#define SOURCE_DATA_MGR_INCLUDE_DMMIGRATIONMGR_H_

#include <fds_error.h>
#include <DmMigrationExecutor.h>
#include <DmMigrationClient.h>
#include <condition_variable>

namespace fds {

// Forward declaration
class DmIoReqHandler;


class DmMigrationMgr {

	using DmMigrationExecMap = std::unordered_map<fds_volid_t, DmMigrationExecutor::unique_ptr>;
    using DmMigrationClientMap = std::unordered_map<fds_volid_t, DmMigrationClient::shared_ptr>;
    // Callbacks for migration handlers
	using OmStartMigrationCBType = std::function<void (const Error& e)>;

  public:
    explicit DmMigrationMgr(DmIoReqHandler* DmReqHandle, DataMgr& _dataMgr);
    ~DmMigrationMgr();

    /**
     * Migration State Machine related methods.
     */
    enum MigrationState {
        MIGR_IDLE,         // MigrationMgr is ready to receive work.
        MIGR_IN_PROGRESS,  // MigrationMgr has received a job and has dispatched work.
        MIGR_ABORTED       // If any of the jobs dispatched failed.
    };

    enum MigrationType {
        MIGR_DM_ADD_NODE, // If this migration is initiated by OM due to new DM node
        MIGR_DM_RESYNC	  // If this migration is peer-initiated between DMs
    };

    inline fds_bool_t isMigrationInProgress() const {
        MigrationState curState = atomic_load(&migrState);
        return (curState == MIGR_IN_PROGRESS);
    }
    inline fds_bool_t isMigrationIdle() const {
        MigrationState curState = atomic_load(&migrState);
        return (curState == MIGR_IDLE);
    }
    inline fds_bool_t isMigrationAborted() const {
        MigrationState curState = atomic_load(&migrState);
        return (curState == MIGR_ABORTED);
    }

    inline fds_uint64_t ongoingMigrationCnt() const {
    	return (executorMap.size());
    }

    /**
     * Destination side DM:
     * Method to invoke migration operations requested by the OM.
     * This is the entry point and it'll decode the migration message. For each of the
     * volume that it needs to pull, it'll spawn off executors who are in charge of
     * contacting the correct source DM, which will then do handshaking between the
     * volume diffs, and then do the sync.
     * This method will spawn off an executor and return once all the executors have
     * been spawned successfully. In the case where more volume migrations are requestsed
     * than allowed parallely, then this will block.
     * Returns ERR_OK if the migrations specified in the migrationMsg has been
     * able to be dispatched for the executors.
     */
    Error startMigrationExecutor(dmCatReq* dmRequest);

    /**
     * Source side DM:
     * Method to receive a request from a Destination DM.
     * Will spawn off a client to handle this specific request.
     * The reason why we're trying to emulate a non-blocking way of spawning off clients
     * is because we're planning ahead for the chance that we may allow 2 nodes to join
     * a cluster in one shot. If that happens, then it would be possible that a node may
     * act as a source node for multiple destination nodes. And it would be unwise to handle
     * only one client at a time.
     * TODO(Neil) - once we support 2 node additions, then we'll need to keep track of
     * multiple callback pointers, etc. For now, not doing it.
     */
    Error startMigrationClient(dmCatReq* dmRequest);

    /**
     * Routes the DmIoMigrationDeltaBlobDesc request to the right executor
     */
    Error applyDeltaBlobDescritor(DmIoMigrationDeltaBlobDesc* deltaBlobDescReq);

    typedef std::unique_ptr<DmMigrationMgr> unique_ptr;
    typedef std::shared_ptr<DmMigrationMgr> shared_ptr;

  protected:
  private:
    DmIoReqHandler* DmReqHandler;
    fds_rwlock migrExecutorLock;
    fds_rwlock migrClientLock;
    std::atomic<MigrationState> migrState;
    std::atomic<fds_bool_t> cleanUpInProgress;

    DataMgr& dataManager;

    /** check if the feature is enabled or not.
     */
    bool enableMigrationFeature;

    /**
     * check if resync feature is enabled.
     */
    bool enableResyncFeature;

    /**
     * Throttles the number of max concurrent migrations
     * Below are protected by migrExecutorLock.
     */
    fds_uint32_t maxConcurrency;
    fds_uint32_t firedMigrations;
    // Bookmark for last fired executor
    DmMigrationExecMap::iterator mit;

    /**
     * Destination side DM:
     * Create an executor instance. Does bookkeeping.
     * Returns ERR_OK if the executor instance was created successfully.
     * Uses the executorMap to store the created instances.
     */
    Error createMigrationExecutor(const NodeUuid& srcDmUuid,
							      fpi::FDSP_VolumeDescType &vol,
							      MigrationType& migrationType,
								  const fds_bool_t& autoIncrement = false);

    /**
     * Destination side DM:
     * Makes sure that the state machine is idle, and activate it.
     * Returns ERR_OK if that's the case, otherwise returns something else.
     */
    Error activateStateMachine();

   /**
     * Destination side DM:
     * Map of ongoing migration executor instances index'ed by vol ID (uniqueKey)
     */
    DmMigrationExecMap executorMap;

    /**
     * Destination side DM:
     * Wrapper around calling OmStartMigrCb
     */
    void ackMigrationComplete(const Error &status);

	/*
     * Destination side DM:
     * Ack back to DM start migration from the Destination DM to OM.
     * This is called only when the migration completes or aborts.  The error
     * stuffed in the asynchdr determines if the migration completed successfully or not.
     */
    OmStartMigrationCBType OmStartMigrCb;

    /**
     * Destination side DM:
     * Callback for migrationExecutor. Not the callback from client.
     */
    void migrationExecutorDoneCb(fds_volid_t volId, const Error &result);


    /**
     * Source side DM:
     * Create a client instance. Does book keeping.
     * Returns ERR_OK if the client instance was created successfully.
     * Uses the clientMap to store the created instances.
     */
    Error createMigrationClient(NodeUuid& srcDmUuid,
    								const NodeUuid& mySvcUuid,
									fpi::CtrlNotifyInitialBlobFilterSetMsgPtr& rvmp);

    /**
     * Source side DM:
     * Map of ongoing migration client instances index'ed by vol ID (uniqueKey)
     */
    DmMigrationClientMap clientMap;

    /**
     * Source side DM:
     * Callback for migrationClient.
     */
    void migrationClientDoneCb(fds_volid_t uniqueId, const Error &result);

};  // DmMigrationMgr

}  // namespace fds


#endif  // SOURCE_DATA_MGR_INCLUDE_DMMIGRATIONMGR_H_
