/*
 * Copyright 2015 Formation Data Systems, Inc.
 */

#include <DataMgr.h>
#include <DmMigrationExecutor.h>
#include <fdsp/dm_types_types.h>
#include <fdsp/dm_api_types.h>
#include <dmhandler.h>
#include <DmMigrationBase.h>

#include "fds_module_provider.h"
#include <net/SvcMgr.h>
#include <net/SvcRequestPool.h>

namespace fds {

DmMigrationExecutor::DmMigrationExecutor(DataMgr& _dataMgr,
    							 	 	 const NodeUuid& _srcDmUuid,
	 	 								 fpi::FDSP_VolumeDescType& _volDesc,
                                         int64_t migrationId,
										 const fds_bool_t& _autoIncrement,
										 DmMigrationExecutorDoneCb _callback,
                                         uint32_t _timeout)
	: DmMigrationBase(migrationId, _dataMgr),
      srcDmSvcUuid(_srcDmUuid),
      volDesc(_volDesc),
	  autoIncrement(_autoIncrement),
      migrDoneCb(_callback),
	  timerInterval(_timeout),
	  seqTimer(_dataMgr.getModuleProvider()->getTimer()),
      msgHandler(_dataMgr),
      migrationProgress(INIT),
      txStateIsMigrated(true),
      lastUpdateFromClientTsSec_(util::getTimeStampSeconds()),
      deltaBlobsSeqNum(seqTimer,timerInterval,std::bind(&fds::DmMigrationExecutor::sequenceTimeoutHandler, this)),
      deltaBlobDescsSeqNum(seqTimer,timerInterval,std::bind(&fds::DmMigrationExecutor::sequenceTimeoutHandler, this))
{
    volumeUuid = volDesc.volUUID;

    dmtVersion = dataMgr.getModuleProvider()->getSvcMgr()->getDMTVersion();

	LOGMIGRATE << logString() << "Migration executor received for volume ID " << volDesc;
}

DmMigrationExecutor::~DmMigrationExecutor()
{
}

Error
DmMigrationExecutor::startMigration()
{
	Error err(ERR_OK);

	LOGMIGRATE << logString() << "starting migration for VolDesc: " << volDesc;

    /** TODO(Sean):
     *
     * For now, assume that the volume exists on start migration.
     * Currently, OM sends two messages to DMs  :  1) notifyVolumeAdd  and 2) startMigration.
     *
     * notifyVolumeAdd is broadcasted to all DMs in the cluster, and depending on the DMT it
     * will create volumes.
     *
     * startMigration is sent only to the destination DM that needs sync volume(s) from the
     * primary DM in the redundancey group.
     *
     * So, for now, we should just check for the existence of the volume.
     */
	auto prefixStr = dataMgr.getPrefix();
    err = dataMgr.addVolume(prefixStr + std::to_string(volumeUuid.get()),
                            volumeUuid,
                            &volDesc);

    /** TODO(Sean):
     * With current OM implementation, add node will send list of volumes and start migration
     * message, which contains the list of volume descriptors to sync.  So, it's expected
     * that at this point, we should be getting ERR_DUPLICATE.  When OM changes to send
     * only one message (startMigration), we update this block of code.
     *
     * OM could have sent the volume descriptor over already
     */
    if (err.ok() || (err == ERR_DUPLICATE)) {
    	fds_scoped_lock lock(progressLock);
    	fds_assert(migrationProgress == INIT);
    	migrationProgress = STATICMIGRATION_IN_PROGRESS;

    	/**
    	 * First do a StopDequeue on the volume
    	 */
    	LOGMIGRATE << logString() << "Stopping De-queing IO for volume " << volumeUuid;
    	dataMgr.qosCtrl->stopDequeue(volumeUuid);
    	// Note: in error cases, abortMigration() gets called, as well as resumeIO().

    	/**
    	 * If the volume is successfully created with the given volume descriptor, process and generate the
    	 * initial blob filter set to be sent to the source DM.
    	 */
    	err = processInitialBlobFilterSet();
    	if (!err.ok()) {
    		LOGERROR << logString() << "processInitialBlobFilterSet failed on volume=" << volumeUuid
    				<< " with error=" << err;
    		routeAbortMigration();
    	}
    } else {
        LOGERROR << logString() << "process_add_vol failed on volume=" << volumeUuid
                 << " with error=" << err;
        if (migrDoneCb) {
        	migrDoneCb(srcDmSvcUuid, volDesc.volUUID, err);
        }
    	routeAbortMigration();
    }

    return err;
}


fds_bool_t
DmMigrationExecutor::shouldAutoExecuteNext()
{
	fds_scoped_lock lock(progressLock);
	if (migrationProgress == MIGRATION_ABORTED) {
		return false;
	} else {
		return autoIncrement;
	}
}

Error
DmMigrationExecutor::processInitialBlobFilterSet()
{
	Error err(ERR_OK);
	LOGMIGRATE << logString() << "starting migration for VolDesc: " << volDesc;

    /**
     * create and initialize message for initial filter set.
     */
    fpi::CtrlNotifyInitialBlobFilterSetMsgPtr filterSet(new fpi::CtrlNotifyInitialBlobFilterSetMsg());
    filterSet->volume_id = volumeUuid.get();
    auto volMeta = dataMgr.getVolumeMeta(volumeUuid);
    // volMeta needs to be there in the volume group context - since migration is done in vol context
    if (dataMgr.features.isVolumegroupingEnabled()) {
        fds_assert(volMeta != nullptr);
    }
    filterSet->version = volMeta ? volMeta->getVersion() : VolumeGroupConstants::VERSION_INVALID;

    LOGMIGRATE << logString() << "processing to get list of <blobid, seqnum> for volume=" << volumeUuid;
    /**
     * Get the list of <blobid, seqnum> for a volume associted with this executor.
     */
    err = dataMgr.timeVolCat_->queryIface()->getAllBlobsWithSequenceId(fds_volid_t(volumeUuid),
                                                                       filterSet->blobFilterMap,
                                                                       NULL);
    if (!err.ok()) {
        LOGERROR << logString() << "failed to generatate list of <blobid, seqnum> for volume=" << volumeUuid
                 <<" with error=" << err;
        return err;
    }

    /**
     * If successfully generated the initial filter set, send it to the source DM.
     */
    auto asyncInitialBlobSetReq = requestMgr->newEPSvcRequest(srcDmSvcUuid.toSvcUuid());
    asyncInitialBlobSetReq->setPayload(FDSP_MSG_TYPEID(fpi::CtrlNotifyInitialBlobFilterSetMsg), filterSet);
    asyncInitialBlobSetReq->setTimeoutMs(dataMgr.dmMigrationMgr->getTimeoutValue());
    // A hack because g++ doesn't like a bind within a macro that does bind
    // These asyncMsgFailed/Passed actually goes to DmMigrationBase and then re-routes
    std::function<void()> abortBind = std::bind(&DmMigrationExecutor::asyncMsgFailed, this);
    std::function<void()> passBind = std::bind(&DmMigrationExecutor::asyncMsgPassed, this);
    asyncInitialBlobSetReq->onResponseCb(
        RESPONSE_MSG_HANDLER(DmMigrationBase::dmMigrationCheckResp, abortBind, passBind));
    asyncMsgIssued();
    asyncInitialBlobSetReq->invoke();

    return err;
}

Error
DmMigrationExecutor::processDeltaBlobDescs(fpi::CtrlNotifyDeltaBlobDescMsgPtr& msg,
										   migrationCb cb)
{
	fiu_do_on("abort.dm.migration.processDeltaBlobDescs",\
        LOGNOTIFY << "abort.dm.migration processDeltaBlobDescs.fault point enabled";\
        return ERR_DM_MIGRATION_ABORTED;);

    Error err(ERR_OK);
	fds_verify(volumeUuid == fds_volid_t(msg->volume_id));
	LOGMIGRATE << logString() << "Processing incoming CtrlNotifyDeltaBlobDescMsg for volume="
               << std::hex << msg->volume_id << std::dec
               << " msgseqid=" << msg->msg_seq_id
               << " lastmsgseqid=" << msg->last_msg_seq_id
               << " numofblobdesc=" << msg->blob_desc_list.size();

    dataMgr.counters->totalSizeOfDataMigrated.incr(sizeOfData(msg));
    lastUpdateFromClientTsSec_ = util::getTimeStampSeconds();
    /**
     * Check if all blob offset is applied.  if applyBlobDescList is still
     * false, them queue them up to be applied later.
     */
    if (!deltaBlobsSeqNum.isSeqNumComplete()) {
        /**
         * add to the blob descriptor list while holding lock.
         */
        LOGMIGRATE << logString() << "Queueing incoming blob descriptor message vof volume="
                   << std::hex << msg->volume_id << std::dec
                   << " msgseqid=" << msg->msg_seq_id
                   << " lastmsgseqid=" << msg->last_msg_seq_id;
        {
        	fds_scoped_lock lock(blobDescListMutex);
        	blobDescList.emplace_back(make_pair(msg, cb));
        }
        err = ERR_NOT_READY;
    } else {
		/**
		 * No need to have lock at this point, since boolean is always set
		 * one direction from false to true.
		 */
		fds_verify(deltaBlobsSeqNum.isSeqNumComplete());

		LOGMIGRATE << logString() << "Applying blob descriptor for volume="
				   << std::hex << volumeUuid << std::dec
				   << ", sequencId=" << msg->msg_seq_id
				   << ", lastMsg=" << msg->last_msg_seq_id;
		err = applyBlobDesc(msg);
		if (err != ERR_OK) {
			LOGERROR << logString() << "Applying blob descriptor failed on volume="
					 << std::hex << msg->volume_id << std::dec
					 << " msgseqid=" << msg->msg_seq_id
					 << " lastmsgseqid=" << msg->last_msg_seq_id
					 << " numofblobdesc=" << msg->blob_desc_list.size();
		}
		cb(err);
    }
	return err;
}

Error
DmMigrationExecutor::processDeltaBlobs(fpi::CtrlNotifyDeltaBlobsMsgPtr& msg)
{
	Error err(ERR_OK);

	fiu_do_on("abort.dm.migration.processDeltaBlobs",\
        LOGNOTIFY << "abort.dm.migration processDeltaBlobs.fault point enabled";\
        return ERR_NOT_READY;);

	fds_verify(volumeUuid == fds_volid_t(msg->volume_id));
	LOGMIGRATE << logString() << "Processing incoming CtrlNotifyDeltaBlobsMsg: "
               << std::hex << volumeUuid << std::hex
               << ", msg_seq_id=" << msg->msg_seq_id
               << ", last_seq_id=" << msg->last_msg_seq_id
               << ", num obj=" << msg->blob_obj_list.size();

    lastUpdateFromClientTsSec_ = util::getTimeStampSeconds();

    /**
     * It is possible to get en empty message and last_sequence_id == true.
     */
	if (0 == msg->blob_obj_list.size()) {
		LOGMIGRATE << logString() << "For volume=" << std::hex << volumeUuid << std::dec
                   << " received empty object list with"
                   << ", msg_seq_id=" << msg->msg_seq_id
                   << " last_mst_seq_id=" << msg->last_msg_seq_id;
    } else {
    	LOGMIGRATE << logString() << "Volume " << volumeUuid << " received non-empty blob message";
        /**
         * For each blob in the blob_obj_list, apply blob offset.
         */

    	// keep stats
        dataMgr.counters->totalSizeOfDataMigrated.incr(sizeOfData(msg));

        for (auto & blobObj : msg->blob_obj_list) {
            /**
             * TODO(Sean):
             * This can potentially be big, so might have move off stack and allocate.
             */
            BlobObjList blobList(blobObj.blob_diff_list);

            LOGMIGRATE << logString() << "put object on volume="
                         << std::hex << volumeUuid << std::dec
                         << ", blob_name=" << blobObj.blob_name
                         << ", num_blobs=" << blobList.size();

            /**
             * TODO(Sean):
             * This should really be directly called, since it's not query iface.
             * Will clean it up later.
             */
            err = dataMgr.timeVolCat_->queryIface()->putObject(fds_volid_t(volumeUuid),
                                                               blobObj.blob_name,
                                                               blobList);
            if (!err.ok()) {
                LOGERROR << logString() << "putObject failed on volume="
                         << std::hex << volumeUuid << std::dec
                         << ", blob_name=" << blobObj.blob_name;
                return err;
            }

        }
    }

    {
		fds_scoped_lock lock(blobDescListMutex);
		/**
		 * Set the sequence number appropriately.
		 */
		deltaBlobsSeqNum.setSeqNum(msg->msg_seq_id, msg->last_msg_seq_id);

		/**
		 * If all the sequence numbers are present for the blobs, then send apply the queued
		 * blob descriptors in this thread context.
		 * It's possible that all desciptors have already been received.
		 * So, any descriptors in the queue should be flushed.
		 */
		if (deltaBlobsSeqNum.isSeqNumComplete()) {
			LOGMIGRATE << logString() << "blob sequence number is complete for volume="
					   << std::hex << volumeUuid << std::dec
					   << ".  Apply queued blob descriptors.";
			err = applyQueuedBlobDescs();
		}
    }

	return err;
}


Error
DmMigrationExecutor::applyBlobDesc(fpi::CtrlNotifyDeltaBlobDescMsgPtr& msg)
{
    Error err(ERR_OK);

    for (auto & desc : msg->blob_desc_list) {
        LOGMIGRATE << logString() << "Applying blob descriptor for volume="
                   << std::hex << volumeUuid << std::dec
                   << ", blob_name=" << desc.vol_blob_name;

        err = dataMgr.timeVolCat_->migrateDescriptor(fds_volid_t(volumeUuid),
                                                     desc.vol_blob_name,
                                                     desc.vol_blob_desc);
        if (!err.ok()) {
            LOGERROR << logString() << "Failed to apply blob descriptor for volume="
                       << std::hex << volumeUuid << std::dec
                       << ", blob_name=" << desc.vol_blob_name;
            return err;
        }
    }

    /**
     * Record the sequence number after the blob descriptor is applied.
     */
    {
        fds_scoped_lock lock(progressLock);
        deltaBlobDescsSeqNum.setSeqNum(msg->msg_seq_id, msg->last_msg_seq_id);
    }

    /**
     * If the blob descriptor seq number is complete, then notify the mgr that
     * the static migration is complete for this MigrationExecutor.
     */
    if (deltaBlobDescsSeqNum.isSeqNumComplete()) {
        LOGMIGRATE << logString() << "All Blob descriptors applied to volume="
                   << std::hex << volumeUuid << std::dec;
        testStaticMigrationComplete();
    }

    return err;
}

Error
DmMigrationExecutor::applyQueuedBlobDescs()
{
    Error err(ERR_OK);

    /**
     * process all queued blob descriptors.
     */
    for (auto & blobDescPair : blobDescList) {
        // update TS to reflect that we are actively processing buffered messages
        lastUpdateFromClientTsSec_ = util::getTimeStampSeconds();

    	fpi::CtrlNotifyDeltaBlobDescMsgPtr blobDescMsg = std::get<0>(blobDescPair);
    	migrationCb ackDescriptor = std::get<1>(blobDescPair);
        LOGMIGRATE << logString() << "Applying queued blob descriptor for volume="
                   << std::hex << volumeUuid << std::dec
                   << ", sequencId=" << blobDescMsg->msg_seq_id
                   << ", lastMsg=" << blobDescMsg->last_msg_seq_id;
        err = applyBlobDesc(blobDescMsg);
        ackDescriptor(err);
        if (!err.ok()) {
            LOGERROR << logString() << "Failed applying queued blob descriptor for vlume="
                     << std::hex << volumeUuid << std::dec
                     << ", sequencId=" << blobDescMsg->msg_seq_id
                     << ", lastMsg=" << blobDescMsg->last_msg_seq_id;
            break;
        }
    }

    return err;
}

Error
DmMigrationExecutor::processTxState(fpi::CtrlNotifyTxStateMsgPtr txStateMsg) {
    Error err;

    auto volmeta = dataMgr.getVolumeMeta(volumeUuid, false);
    err = volmeta->applyActiveTxState(txStateMsg->highest_op_id, txStateMsg->transactions);

    if (err.ok()) {
        {
            fds_scoped_lock lock(progressLock);
            txStateIsMigrated = true;
        }
        testStaticMigrationComplete();
    } else {
    	LOGMIGRATE << logString() << "Error trying to apply forwarded commit logs content with error " << err;
    }

    return err;
}

void
DmMigrationExecutor::sequenceTimeoutHandler()
{
	LOGERROR << logString() << "Error: blob/blobdesc sequence timed out for volume =  " << volumeUuid;
    routeAbortMigration();
}

void
DmMigrationExecutor::testStaticMigrationComplete() {
    fds_bool_t doWork = false;

    {
        fds_scoped_lock lock(progressLock);

        if (migrationProgress == STATICMIGRATION_IN_PROGRESS &&
            deltaBlobDescsSeqNum.isSeqNumComplete() && txStateIsMigrated) {
            migrationProgress = APPLYING_FORWARDS_IN_PROGRESS;
            doWork = true;
        }
    }

    if (doWork) {
        /* Send any buffered Forwarded messages to qos controller under system
           volume tag */
        for (const auto &msg : forwardedMsgs) {
            // update TS to reflect that we are actively processing buffered messages
            lastUpdateFromClientTsSec_ = util::getTimeStampSeconds();

            //The last forwarded message is a simple termination - without this check there will be deadlock with the
            // iotracker
            if (!(msg->fwdCatMsg->lastForward && msg->fwdCatMsg->blob_name.empty())) {
              asyncMsgIssued();
            }
            msgHandler.addToQueue(msg);
        }

        if (migrDoneCb) {
            migrDoneCb(srcDmSvcUuid, volDesc.volUUID, ERR_OK);
        }
    }
}

Error
DmMigrationExecutor::processForwardedCommits(DmIoFwdCat* fwdCatReq) {
	Error err(ERR_OK);
    /* Callback from QOS */
    fwdCatReq->cb = [this](const Error &e, DmRequest *dmReq) {
        if (e != ERR_OK) {
            LOGERROR << logString() << "error processing forwarded commit " << e;
            asyncMsgFailed();
            delete dmReq;
            return;
        }
        auto fwdCatReq = reinterpret_cast<DmIoFwdCat*>(dmReq);
        fds_assert((unsigned)(fwdCatReq->fwdCatMsg->volume_id) == volumeUuid.v);
        if (fwdCatReq->fwdCatMsg->lastForward) {
            /* All forwards have been applied.  At this point we don't expect anything from
             * migration source.  We can resume active IO
             */
            finishActiveMigration();
        }
        asyncMsgPassed();
        delete dmReq;
    };

    lastUpdateFromClientTsSec_ = util::getTimeStampSeconds();

    fds_scoped_lock lock(progressLock);
    switch (migrationProgress) {
    	case STATICMIGRATION_IN_PROGRESS:
    		LOGDEBUG << "Buffered " << fwdCatReq << " to be applied";
    		forwardedMsgs.push_back(fwdCatReq);
    		break;
    	case APPLYING_FORWARDS_IN_PROGRESS:
    		LOGDEBUG << "Enqueued " << fwdCatReq << " to be applied";
            //The last forwarded message is a simple termination - without this check there will be deadlock with the
            // iotracker
            if (!(fwdCatReq->fwdCatMsg->lastForward && fwdCatReq->fwdCatMsg->blob_name.empty())) {
              asyncMsgIssued();
            }
    		msgHandler.addToQueue(fwdCatReq);
    		break;
    	case MIGRATION_ABORTED:
            	LOGERROR << "Migration aborted so dropping forward request for " << fwdCatReq;
    		err = ERR_DM_MIGRATION_ABORTED;
    		break;
    	case MIGRATION_COMPLETE:
    		// Do nothing
    		break;
        case INIT:
            LOGERROR << "recieved migration forwarding request " << fwdCatReq << " while still in INIT state. dropping.";
            break;
        default:
    		fds_panic("Unexpected state encountered");
    }

    return err;
}

Error
DmMigrationExecutor::finishActiveMigration()
{
	fds_scoped_lock lock(progressLock);

    // watermark should only ever need to be checked on a resync, but good to filter regardless
    dataMgr.dmMigrationMgr->setDmtWatermark(volumeUuid, dmtVersion);

	migrationProgress = MIGRATION_COMPLETE;
    LOGMIGRATE << logString() << "Applying forwards is complete and resuming IO for volume: " << volumeUuid;
	dataMgr.qosCtrl->resumeIOs(volumeUuid);

	dataMgr.counters->totalVolumesReceivedMigration.incr(1);
	dataMgr.counters->numberOfActiveMigrExecutors.decr(1);

	return ERR_OK;
}

void
DmMigrationExecutor::routeAbortMigration()
{
    dataMgr.dmMigrationMgr->abortMigration();
}

void
DmMigrationExecutor::abortMigration()
{
	/**
	 * It's possible that the IO was stopped earlier during static migration.
	 * Prior to "unblocking" the volume, let's set the state to error because
	 * we are in an inconsistent state halfway through migration.
	 */
	auto volumeMeta = dataMgr.getVolumeMeta(volumeUuid, false);
    if (volumeMeta) {
        volumeMeta->vol_desc->setState(fpi::ResourceState::InError);
        LOGERROR << logString() << "Aborting migration: Setting volume state for " << volumeUuid
	            << " to " << fpi::ResourceState::InError;
    } else {
        LOGERROR << logString() << "Aborting migration: Executor hasn't started yet and has no volumeMetaData";
    }
    fds_scoped_lock lock(progressLock);
    if (migrationProgress != INIT) {
        // We should only resume something if we've stopped it.
        dataMgr.qosCtrl->resumeIOs(volumeUuid);
    }
    if (migrationProgress != MIGRATION_ABORTED) {
        migrationProgress = MIGRATION_ABORTED;
        if (migrDoneCb) {
            LOGMIGRATE << logString() << "Abort migration called.";
            migrDoneCb(srcDmSvcUuid, volDesc.volUUID, ERR_DM_MIGRATION_ABORTED);
        }
    }
}

bool DmMigrationExecutor::isMigrationIdle(const util::TimeStamp& curTsSec) const
{
    /* If we haven't heard from client in while while static migration is in progress
     * we abort migration
     */
    if (migrationProgress == STATICMIGRATION_IN_PROGRESS &&
        curTsSec > lastUpdateFromClientTsSec_ &&
        (curTsSec - lastUpdateFromClientTsSec_) > dataMgr.dmMigrationMgr->getIdleTimeout()) {
        return true;
    }
    return false;
}
}  // namespace fds
