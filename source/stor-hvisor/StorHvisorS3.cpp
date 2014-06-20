/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <iostream>
#include <chrono>
#include <cstdarg>
#include <fds_module.h>
#include <fds_timer.h>
#include "StorHvisorNet.h"
#include "StorHvisorCPP.h"
#include "hvisor_lib.h"
#include <fds_config.hpp>
#include <fds_process.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "NetSession.h"
#include <dlt.h>
#include <ObjectId.h>
#include <net/net-service.h>
#include <net/net-service-tmpl.hpp>
#include <net/RpcRequestPool.h>
#include <fdsp/DMSvc.h>
#include <fdsp/SMSvc.h>
#include <fdsp_utils.h>

extern StorHvCtrl *storHvisor;
using namespace std;
using namespace FDS_ProtocolInterface;
typedef fds::hash::Sha1 GeneratorHash;

std::atomic_uint nextIoReqId;

StorHvCtrl::TxnResponseHelper::TxnResponseHelper(StorHvCtrl* storHvisor,
                                                 fds_volid_t  volId, fds_uint32_t txnId)
        : storHvisor(storHvisor), txnId(txnId), volId(volId)
{
    vol = storHvisor->vol_table->getVolume(volId);
    vol_lock = new StorHvVolumeLock(vol);
    txn = vol->journal_tbl->get_journal_entry(txnId);
    je_lock = new StorHvJournalEntryLock(txn);

    fds_verify(txn != NULL);
    fds_verify(txn->isActive() == true);
    fds::AmQosReq   *qosReq  = TO_DERIVED(fds::AmQosReq, txn->io);
    fds_verify(qosReq != NULL);
    blobReq = qosReq->getBlobReqPtr();
    fds_verify(blobReq != NULL);
    blobReq->cb->status = ERR_OK;
}

void StorHvCtrl::TxnResponseHelper::setStatus(FDSN_Status  status) {
    blobReq->cb->status = status;
}

StorHvCtrl::TxnResponseHelper::~TxnResponseHelper() {
    storHvisor->qos_ctrl->markIODone(txn->io);
    GLOGDEBUG << "releasing txnid:" << txnId;
    txn->reset();
    vol->journal_tbl->releaseTransId(txnId);
    GLOGDEBUG << "doing callback for txnid:" << txnId;
    blobReq->cb->call();
    delete blobReq;
    delete je_lock;
    delete vol_lock;
}


StorHvCtrl::TxnRequestHelper::TxnRequestHelper(StorHvCtrl* storHvisor,
                                               AmQosReq *qosReq)
        : storHvisor(storHvisor), qosReq(qosReq) {
    blobReq = qosReq->getBlobReqPtr();
    volId = blobReq->getVolId();
    shVol = storHvisor->vol_table->getLockedVolume(volId);
    blobReq->setQueuedUsec(shVol->journal_tbl->microsecSinceCtime(
        boost::posix_time::microsec_clock::universal_time()));
}

bool StorHvCtrl::TxnRequestHelper::isValidVolume() {
    return ((shVol != NULL) && (shVol->isValidLocked()));
}

bool StorHvCtrl::TxnRequestHelper::setupTxn() {
    bool trans_in_progress = false;
    txnId = shVol->journal_tbl->get_trans_id_for_blob(blobReq->getBlobName(),
                                                      blobReq->getBlobOffset(),
                                                      trans_in_progress);

    txn = shVol->journal_tbl->get_journal_entry(txnId);
    fds_verify(txn != NULL);
    jeLock = new StorHvJournalEntryLock(txn);

    if (trans_in_progress || txn->isActive()) {
        GLOGWARN << "txn: " << txnId << " is already ACTIVE";
        return false;
    }
    txn->setActive();
    return true;
}

bool StorHvCtrl::TxnRequestHelper::getPrimaryDM(fds_uint32_t& ip, fds_uint32_t& port) {
    // Get DMT node list from dmt
    DmtColumnPtr nodeIds = storHvisor->dataPlacementTbl->getDMTNodesForVolume(volId);
    fds_verify(nodeIds->getLength() > 0);
    fds_int32_t node_state = -1;
    storHvisor->dataPlacementTbl->getNodeInfo(nodeIds->get(0).uuid_get_val(),
                                              &ip,
                                              &port,
                                              &node_state);
    return true;
}

void StorHvCtrl::TxnRequestHelper::scheduleTimer() {
    GLOGDEBUG << "scheduling timer for txnid:" << txnId;
    shVol->journal_tbl->schedule(txn->ioTimerTask, std::chrono::seconds(FDS_IO_LONG_TIME));
}

void StorHvCtrl::TxnRequestHelper::setStatus(FDSN_Status status) {
    this->status = status;
}

bool StorHvCtrl::TxnRequestHelper::hasError() {
    return ((status != ERR_OK) && (status != FDSN_StatusNOTSET));
}

StorHvCtrl::TxnRequestHelper::~TxnRequestHelper() {
    if (jeLock) delete jeLock;
    if (shVol) shVol->readUnlock();

    if (hasError()) {
        GLOGWARN << "error processing txnid:" << txnId << " : " << status;
        txn->reset();
        shVol->journal_tbl->releaseTransId(txnId);
        if (blobReq->cb.get() != NULL) {
            GLOGDEBUG << "doing callback for txnid:" << txnId;
            blobReq->cb->call(status);
        }
        delete qosReq;
        //delete blobReq;
    } else {
        scheduleTimer();
    }
}

StorHvCtrl::BlobRequestHelper::BlobRequestHelper(StorHvCtrl* storHvisor,
                                                 const std::string& volumeName)
        : storHvisor(storHvisor), volumeName(volumeName) {}

void StorHvCtrl::BlobRequestHelper::setupVolumeInfo() {
    if (storHvisor->vol_table->volumeExists(volumeName)) {
        volId = storHvisor->vol_table->getVolumeUUID(volumeName);
        fds_verify(volId != invalid_vol_id);
    }
}

fds::Error StorHvCtrl::BlobRequestHelper::processRequest() {
    if (volId != invalid_vol_id) {
        storHvisor->pushBlobReq(blobReq);
        return ERR_OK;
    } else {
        // If we don't have the volume, queue up the request
        storHvisor->vol_table->addBlobToWaitQueue(volumeName, blobReq);
    }

    // if we are here, bucket is not attached to this AM, send test bucket msg to OM
    return storHvisor->sendTestBucketToOM(volumeName, "", "");
}

fds::Error StorHvCtrl::pushBlobReq(fds::FdsBlobReq *blobReq) {
    fds_verify(blobReq->magicInUse() == true);
    fds::Error err(ERR_OK);

    /*
     * Pack the blobReq in to a qosReq to pass to QoS
     */
    fds_uint32_t reqId = atomic_fetch_add(&nextIoReqId, (fds_uint32_t)1);
    AmQosReq *qosReq  = new AmQosReq(blobReq, reqId);
    fds_volid_t volId = blobReq->getVolId();

    fds::StorHvVolume *shVol = storHvisor->vol_table->getLockedVolume(volId);
    if ((shVol == NULL) || (shVol->volQueue == NULL)) {
        if (shVol)
            shVol->readUnlock();
        LOGERROR << "Volume and queueus are NOT setup for volume " << volId;
        err = fds::ERR_INVALID_ARG;
        delete qosReq;
        return err;
    }
    /*
     * TODO: We should handle some sort of success/failure here?
     */
    qos_ctrl->enqueueIO(volId, qosReq);
    shVol->readUnlock();

    LOGDEBUG << "Queued IO for vol " << volId;

    return err;
}

/*
 * TODO: Actually calculate the host's IP
 */
#define SRC_IP           0x0a010a65

/**
 * Dispatches async FDSP messages to DMs for a blob update.
 * Note, assumes the journal entry lock is held already
 * and that the messages are ready to be dispatched.
 * If send_uuid is specified, put is targeted only to send_uuid.
 */
fds::Error
StorHvCtrl::dispatchDmUpdMsg(StorHvJournalEntry *journEntry,
                             const NodeUuid &send_uuid) {
    fds::Error err(ERR_OK);

    fds_verify(journEntry != NULL);

    FDSP_MsgHdrTypePtr dmMsgHdr = journEntry->dm_msg;
    fds_verify(dmMsgHdr != NULL);
    FDSP_UpdateCatalogTypePtr updCatMsg = journEntry->updCatMsg;
    fds_verify(updCatMsg != NULL);

    fds_volid_t volId = dmMsgHdr->glob_volume_id;

    // Get DMT node list from dmt
    DmtColumnPtr nodeIds = dataPlacementTbl->getDMTNodesForVolume(volId);
    fds_verify(nodeIds->getLength() > 0);

    // Issue a blob update for each DM in the DMT
    fds_uint32_t errcount = 0;
    for (fds_uint32_t i = 0; i < nodeIds->getLength(); i++) {
        fds_uint32_t node_ip   = 0;
        fds_uint32_t node_port = 0;
        fds_int32_t node_state = -1;
        dataPlacementTbl->getNodeInfo(nodeIds->get(i).uuid_get_val(),
                                      &node_ip,
                                      &node_port,
                                      &node_state);
        journEntry->dm_ack[i].ipAddr        = node_ip;
        journEntry->dm_ack[i].port          = node_port;
        // TODO(Andrew): This seems like a race...
        // It's the same ptr that we modify in a loop
        dmMsgHdr->dst_ip_lo_addr            = node_ip;
        dmMsgHdr->dst_port                  = node_port;
        journEntry->dm_ack[i].ack_status    = FDS_CLS_ACK;
        journEntry->dm_ack[i].commit_status = FDS_CLS_ACK;
        journEntry->num_dm_nodes            = nodeIds->getLength();
    
        // Call Update Catalog RPC call to DM
        try {
            netMetaDataPathClientSession *sessionCtx =
                    storHvisor->rpcSessionTbl->\
                    getClientSession<netMetaDataPathClientSession>(node_ip, node_port);
            fds_verify(sessionCtx != NULL);

            boost::shared_ptr<FDSP_MetaDataPathReqClient> client = sessionCtx->getClient();
            dmMsgHdr->session_uuid = sessionCtx->getSessionId();
            journEntry->session_uuid = dmMsgHdr->session_uuid;
            client->UpdateCatalogObject(dmMsgHdr, updCatMsg);

            LOGDEBUG << "For transaction " << journEntry->trans_id
                     << " sent async UP_CAT_REQ to DM ip "
                     << node_ip << " port " << node_port;
        } catch (att::TTransportException& e) {
            LOGERROR << "error during network call : " << e.what() ;
            errcount++;
        }
    }

    return err;
}

/**
 * Dispatches async FDSP messages to SMs for a put msg.
 * Note, assumes the journal entry lock is held already
 * and that the messages are ready to be dispatched.
 * If send_uuid is specified, put is targeted only to send_uuid.
 */
fds::Error
StorHvCtrl::dispatchSmPutMsg(StorHvJournalEntry *journEntry,
                             const NodeUuid &send_uuid) {
    fds::Error err(ERR_OK);

    fds_verify(journEntry != NULL);

    FDSP_MsgHdrTypePtr smMsgHdr = journEntry->sm_msg;
    fds_verify(smMsgHdr != NULL);
    FDSP_PutObjTypePtr putMsg = journEntry->putMsg;
    fds_verify(putMsg != NULL);

    ObjectID objId(putMsg->data_obj_id.digest);

    // Get DLT node list from dlt
    DltTokenGroupPtr dltPtr;
    dltPtr = dataPlacementTbl->getDLTNodesForDoidKey(objId);
    fds_verify(dltPtr != NULL);

    fds_uint32_t numNodes = dltPtr->getLength();
    fds_verify(numNodes > 0);
    
    putMsg->dlt_version = om_client->getDltVersion();
    fds_verify(putMsg->dlt_version != DLT_VER_INVALID);

    // checksum calculation for putObj  class and the payload.  we may haveto bundle this
    // into a function .
    storHvisor->chksumPtr->checksum_update(putMsg->data_obj_id.digest);
    storHvisor->chksumPtr->checksum_update(putMsg->data_obj_len);
    storHvisor->chksumPtr->checksum_update(putMsg->volume_offset);
    storHvisor->chksumPtr->checksum_update(putMsg->dlt_version);
    storHvisor->chksumPtr->checksum_update(putMsg->data_obj);
    storHvisor->chksumPtr->get_checksum(smMsgHdr->payload_chksum);
    LOGDEBUG << "RPC Checksum: " << smMsgHdr->payload_chksum;

    uint errcount = 0;
    // Issue a put for each SM in the DLT list
    for (fds_uint32_t i = 0; i < numNodes; i++) {
        fds_uint32_t node_ip   = 0;
        fds_uint32_t node_port = 0;
        fds_int32_t node_state = -1;
        NodeUuid target_uuid = dltPtr->get(i);

        // Get specific SM's info
        dataPlacementTbl->getNodeInfo(target_uuid.uuid_get_val(),
                                      &node_ip,
                                      &node_port,
                                      &node_state);
        if (send_uuid != INVALID_RESOURCE_UUID && send_uuid != target_uuid) {
            continue;
        }

        journEntry->sm_ack[i].ipAddr = node_ip;
        journEntry->sm_ack[i].port   = node_port;
        smMsgHdr->dst_ip_lo_addr     = node_ip;
        smMsgHdr->dst_port           = node_port;
        journEntry->sm_ack[i].ack_status = FDS_CLS_ACK;
        journEntry->num_sm_nodes     = numNodes;

        try {
            // Call Put object RPC to SM
            netDataPathClientSession *sessionCtx =
                    rpcSessionTbl->\
                    getClientSession<netDataPathClientSession>(node_ip, node_port);
            fds_verify(sessionCtx != NULL);

            // Set session UUID in journal and msg
            boost::shared_ptr<FDSP_DataPathReqClient> client = sessionCtx->getClient();
            smMsgHdr->session_uuid = sessionCtx->getSessionId();
            journEntry->session_uuid = smMsgHdr->session_uuid;

            client->PutObject(smMsgHdr, putMsg);
            LOGNORMAL << "For transaction " << journEntry->trans_id
                      << " sent async PUT_OBJ_REQ to SM ip "
                      << node_ip << " port " << node_port;
        } catch (att::TTransportException& e) {
            LOGERROR << "error during network call : " << e.what() ;
            errcount++;
        }

        if (send_uuid != INVALID_RESOURCE_UUID) {
            LOGDEBUG << " Peformed a targeted send to: " << send_uuid;
            break;
        }
    }

    if (errcount >= int(ceil(numNodes*0.5))) {
        LOGCRITICAL << "Too many network errors : " << errcount ;
        return ERR_NETWORK_TRANSPORT;
    }

    return err;
}

/**
 * Dispatches async FDSP messages to SMs for a get msg.
 * Note, assumes the journal entry lock is held already
 * and that the messages are ready to be dispatched.
 */
fds::Error
StorHvCtrl::dispatchSmGetMsg(StorHvJournalEntry *journEntry) {
    fds::Error err(ERR_OK);

    fds_verify(journEntry != NULL);

    FDSP_MsgHdrTypePtr smMsgHdr = journEntry->sm_msg;
    fds_verify(smMsgHdr != NULL);
    FDSP_GetObjTypePtr getMsg = journEntry->getMsg;
    fds_verify(getMsg != NULL);

    ObjectID objId(getMsg->data_obj_id.digest);

    fds_volid_t   volId =smMsgHdr->glob_volume_id;
    StorHvVolume *shVol = storHvisor->vol_table->getLockedVolume(volId);

    // Look up primary SM from DLT entries
    boost::shared_ptr<DltTokenGroup> dltPtr;
    dltPtr = dataPlacementTbl->getDLTNodesForDoidKey(objId);
    fds_verify(dltPtr != NULL);

    fds_int32_t numNodes = dltPtr->getLength();
    fds_verify(numNodes > 0);

    fds_uint32_t node_ip   = 0;
    fds_uint32_t node_port = 0;
    fds_int32_t node_state = -1;

    // Get primary SM's node info
    // TODO: We're just assuming it's the first in the list!
    // We should be verifying this somehow.
    dataPlacementTbl->getNodeInfo(dltPtr->get(journEntry->nodeSeq).uuid_get_val(),
                                  &node_ip,
                                  &node_port,
                                  &node_state);
    smMsgHdr->dst_ip_lo_addr = node_ip;
    smMsgHdr->dst_port       = node_port;

    journEntry->sm_ack[journEntry->nodeSeq].ipAddr = node_ip;
    journEntry->sm_ack[journEntry->nodeSeq].port   = node_port;
    journEntry->sm_ack[journEntry->nodeSeq].ack_status = FDS_CLS_ACK;
    journEntry->num_sm_nodes     = numNodes;

    LOGNORMAL << "For transaction: " << journEntry->trans_id << "node_ip:" << node_ip << "node_port" << node_port;
    getMsg->dlt_version = om_client->getDltVersion();
    fds_verify(getMsg->dlt_version != DLT_VER_INVALID);

    try {

        netDataPathClientSession *sessionCtx =
                rpcSessionTbl->\
                getClientSession<netDataPathClientSession>(node_ip, node_port);
        fds_verify(sessionCtx != NULL);

        boost::shared_ptr<FDSP_DataPathReqClient> client = sessionCtx->getClient();
        smMsgHdr->session_uuid = sessionCtx->getSessionId();
        journEntry->session_uuid = smMsgHdr->session_uuid;

        // Schedule a timer here to track the responses and the original request
        shVol->journal_tbl->schedule(journEntry->ioTimerTask, std::chrono::seconds(FDS_IO_LONG_TIME));

        // RPC getObject to StorMgr
        client->GetObject(smMsgHdr, getMsg);

        LOGNORMAL << "For trans " << journEntry->trans_id
                  << " sent async GetObj req to SM";
    } catch (att::TTransportException& e) {
        LOGERROR << "error during network call : " << e.what() ;
    }

    return err;
}

/**
 * Handles initial processing for putting objects to SMs
 */
fds::Error
StorHvCtrl::processSmPutObj(PutBlobReq *putBlobReq,
                            StorHvJournalEntry *journEntry) {
    fds::Error err(ERR_OK);

    fds_volid_t  volId   = putBlobReq->getVolId();
    ObjectID     objId   = putBlobReq->getObjId();
    fds_uint32_t transId = journEntry->trans_id;

    // Setup message header
    FDSP_MsgHdrTypePtr msgHdrSm(new FDSP_MsgHdrType);
    InitSmMsgHdr(msgHdrSm);
    msgHdrSm->glob_volume_id   = volId;
    msgHdrSm->origin_timestamp = fds::util::getTimeStampMillis();
    msgHdrSm->req_cookie       = transId;

    // Setup operation specific message
    FDSP_PutObjTypePtr putObjReq(new FDSP_PutObjType);
    putObjReq->data_obj = std::string((const char *)putBlobReq->getDataBuf(),
                                      (size_t )putBlobReq->getDataLen());
    putObjReq->data_obj_len = putBlobReq->getDataLen();
    putObjReq->data_obj_id.digest =
            std::string((const char *)objId.GetId(), (size_t)objId.GetLen());

    // Update the SM-related journal fields
    journEntry->sm_msg = msgHdrSm;
    journEntry->putMsg = putObjReq;

    LOGNOTIFY << "Putting object " << objId << " for blob "
              << putBlobReq->getBlobName() << " at offset "
              << putBlobReq->getBlobOffset() << " with length "
              << putBlobReq->getDataLen() << " in trans "
              << transId;

    // Dispatch SM put object requests
    bool fZeroSize = (putBlobReq->getDataLen() == 0);
    if (!fZeroSize) {
        err = dispatchSmPutMsg(journEntry, INVALID_RESOURCE_UUID);
        fds_verify(err == ERR_OK);
    } else {
        // special case for zero size
        // trick the state machine to think that it 
        // has gotten responses from SMs since there's
        // no actual data to write
        DltTokenGroupPtr dltPtr;
        dltPtr = dataPlacementTbl->getDLTNodesForDoidKey(objId);
        fds_verify(dltPtr != NULL);

        fds_uint32_t numNodes = dltPtr->getLength();
        fds_verify(numNodes > 0);
        journEntry->sm_ack_cnt = numNodes;
        LOGWARN << "not sending put request to sm as obj is zero size - ["
                << " id:" << objId
                << " objkey:" << putBlobReq->ObjKey
                << " name:" << putBlobReq->getBlobName()
                << "]";
    }

    return err;
}

/**
 * Handles initial processing for blob catalog updates to DMs
 */
fds::Error
StorHvCtrl::processDmUpdateBlob(PutBlobReq *putBlobReq,
                                StorHvJournalEntry *journEntry) {
    fds::Error err(ERR_OK);

    fds_volid_t  volId   = putBlobReq->getVolId();
    ObjectID     objId   = putBlobReq->getObjId();
    fds_uint32_t transId = journEntry->trans_id;

    // Setup message header
    FDSP_MsgHdrTypePtr msgHdrDm(new FDSP_MsgHdrType);
    InitDmMsgHdr(msgHdrDm);
    msgHdrDm->glob_volume_id   = volId;
    msgHdrDm->origin_timestamp = fds::util::getTimeStampMillis();
    msgHdrDm->req_cookie       = transId;

    // Setup operation specific message
    FDSP_UpdateCatalogTypePtr updCatReq(new FDSP_UpdateCatalogType);
    updCatReq->txDesc.txId = putBlobReq->getTxId()->getValue();
    updCatReq->blob_name   = putBlobReq->getBlobName();
    // TODO(Andrew): These can be removed when real transactions work
    updCatReq->dm_transaction_id = 1;
    updCatReq->dm_operation      = FDS_DMGR_TXN_STATUS_OPEN;

    // Setup blob offset updates
    // TODO(Andrew): Today we only expect one offset update
    // TODO(Andrew): Remove lastBuf when we have real transactions
    updCatReq->obj_list.clear();
    FDS_ProtocolInterface::FDSP_BlobObjectInfo updBlobInfo;
    updBlobInfo.offset   = putBlobReq->getBlobOffset();
    updBlobInfo.size     = putBlobReq->getDataLen();
    updBlobInfo.blob_end = putBlobReq->isLastBuf();
    updBlobInfo.data_obj_id.digest =
            std::string((const char *)objId.GetId(), (size_t)objId.GetLen());
    // Add the offset info to the DM message
    updCatReq->obj_list.push_back(updBlobInfo);

    // Setup blob metadata updates
    updCatReq->meta_list.clear();

    // Update the DM-related journal fields
    journEntry->trans_state   = FDS_TRANS_OPEN;
    journEntry->dm_msg        = msgHdrDm;
    journEntry->updCatMsg     = updCatReq;
    journEntry->dm_ack_cnt    = 0;
    journEntry->dm_commit_cnt = 0;

    // Dispatch DM update blob requests
    err = dispatchDmUpdMsg(journEntry, INVALID_RESOURCE_UUID);
    fds_verify(err == ERR_OK);

    return err;
}

/**
 * Handles processing putBlob requests from a previously
 * constructed journal entry. The journal entry is expected
 * to already be locked be the caller.
 * TODO(Andrew): This code currently only handles putting a
 * single object into a blob and assumes it's offset aligned.
 * We should remove these assumptions.
 */
fds::Error
StorHvCtrl::resumePutBlob(StorHvJournalEntry *journEntry) {
    fds::Error err(ERR_OK);

    LOGDEBUG << "Processing putBlob journal entry "
             << journEntry->trans_id << " to put request";

    AmQosReq *qosReq = static_cast<fds::AmQosReq*>(journEntry->io);
    PutBlobReq *blobReq = static_cast<PutBlobReq *>(qosReq->getBlobReqPtr());

    // Get the object ID and set it in the request
    bool fZeroSize = (blobReq->getDataLen() == 0);
    ObjectID objId;
    if (fZeroSize) {
        LOGWARN << "zero size object - "
                << " [objkey:" << blobReq->ObjKey <<"]";
    } else {
        objId = ObjIdGen::genObjectId(blobReq->getDataBuf(),
                                      blobReq->getDataLen());
    }
    blobReq->setObjId(objId);

    // Initialize the journal's transaction state
    journEntry->trans_state = FDS_TRANS_OPEN;

    // Process SM object put messages
    err = processSmPutObj(blobReq, journEntry);
    fds_verify(err == ERR_OK);

    // Process DM blob update messages
    err = processDmUpdateBlob(blobReq, journEntry);
    fds_verify(err == ERR_OK);

    return err;
}

/**
 * Updates data in a blob.
 */
fds::Error
StorHvCtrl::putBlob(fds::AmQosReq *qosReq) {
    fds::Error err(ERR_OK);
    
    // Pull out the blob request     
    PutBlobReq *blobReq = static_cast<PutBlobReq *>(qosReq->getBlobReqPtr());
    bool fZeroSize = (blobReq->getDataLen() == 0);
    fds_verify(blobReq->magicInUse() == true);

    // Get the volume context structure
    fds_volid_t   volId = blobReq->getVolId();
    StorHvVolume *shVol = storHvisor->vol_table->getLockedVolume(volId);
    fds_verify(shVol != NULL);
    fds_verify(shVol->isValidLocked() == true);

    // TODO(Andrew): Here we're turning the offset aligned
    // blobOffset back into an absolute blob offset (i.e.,
    // not aligned to the maximum object size). This allows
    // the rest of the putBlob routines to still expect an
    // absolute offset in case we need it
    fds_uint32_t maxObjSize = shVol->voldesc->maxObjSizeInBytes;
    blobReq->setBlobOffset(blobReq->getBlobOffset() * maxObjSize);

    // Track how long the request was queued before put() dispatch
    // TODO(Andrew): Consider moving to the QoS request
    blobReq->setQueuedUsec(shVol->journal_tbl->microsecSinceCtime(
        boost::posix_time::microsec_clock::universal_time()));

    // Get/lock a journal entry for the request.
    bool trans_in_progress = false;
    fds_uint32_t transId = shVol->journal_tbl->get_trans_id_for_blob(blobReq->getBlobName(),
                                                                     blobReq->getBlobOffset(),
                                                                     trans_in_progress);
    StorHvJournalEntry *journEntry = shVol->journal_tbl->get_journal_entry(transId);
    fds_verify(journEntry != NULL);
    StorHvJournalEntryLock jeLock(journEntry);

    // Check if the entry is already active.
    // TODO(Andrew): Don't need in_progress and isActive
    if ((trans_in_progress == true) || (journEntry->isActive() == true)) {
        fds_uint32_t newTransId = shVol->journal_tbl->create_trans_id(
            blobReq->getBlobName(), blobReq->getBlobOffset());

        // Set up a new journal entry to represent the waiting operation
        LOGNOTIFY << "Journal operation " << transId << " is already ACTIVE"
                  << " at offset " << blobReq->getBlobOffset()
                  << " barriering this op " << newTransId;

        StorHvJournalEntry *newJournEntry = shVol->journal_tbl->get_journal_entry(newTransId);

        newJournEntry->trans_state = FDS_TRANS_PENDING_WAIT;
        // newJournEntry->dm_msg = NULL;
        // newJournEntry->sm_ack_cnt = 0;
        // newJournEntry->dm_ack_cnt = 0;
        newJournEntry->op = FDS_IO_WRITE;
        // newJournEntry->data_obj_len = blobReq->getDataLen();

        // Stash the qos request in the journal entry so
        // that we can pull out the request context later
        newJournEntry->io = qosReq;
        // Set this entry as active
        newJournEntry->setActive();
        journEntry->pendingTransactions.push_back(newJournEntry);

        return err;
    }
    journEntry->setActive();
    journEntry->io = qosReq;
    LOGDEBUG << "Assigning transaction ID " << transId
             << " to put request";

    err = resumePutBlob(journEntry);
    fds_verify(err == ERR_OK);

    // Schedule a timer here to track the responses and the original request
    shVol->journal_tbl->schedule(journEntry->ioTimerTask, std::chrono::seconds(FDS_IO_LONG_TIME));

    // Release the vol read lock
    shVol->readUnlock();

    // Note je_lock destructor will unlock the journal entry automatically
    return err;
}

void
StorHvCtrl::procNewDlt(fds_uint64_t newDltVer) {
    // Check that this version has already been
    // written to the omclient
    fds_verify(newDltVer == om_client->getDltVersion());

    StorHvVolVec volIds = vol_table->getVolumeIds();
    for (StorHvVolVec::const_iterator it = volIds.cbegin();
         it != volIds.cend();
         it++) {
        fds_volid_t volId = (*it);
        StorHvVolume *vol = vol_table->getVolume(volId);

        // TODO(Andrew): It's possible that a volume gets deleted
        // while we're iterating this list. We should find a way
        // to iterate the table while it's locked.
        fds_verify(vol != NULL);

        // Get all of the requests for this volume that are waiting on the DLT
        PendingDltQueue pendingTrans = vol->journal_tbl->popAllDltTrans();

        // Iterate each transaction and re-issue
        while (pendingTrans.empty() == false) {
            fds_uint32_t transId = pendingTrans.front();
            pendingTrans.pop();

            // Grab the journal entry that was waiting for a DLT
            StorHvJournalEntry *journEntry =
                    vol->journal_tbl->get_journal_entry(transId);
            fds_verify(journEntry != NULL);
            fds_verify(journEntry->trans_state == FDS_TRANS_PENDING_DLT);

            // Acquire RAII lock
            StorHvJournalEntryLock je_lock(journEntry);

            // Re-dispatch the SM requests.
            if (journEntry->op == FDS_PUT_BLOB) {
                dispatchSmPutMsg(journEntry, INVALID_RESOURCE_UUID);
            } else if (journEntry->op == FDS_GET_BLOB) {
                dispatchSmGetMsg(journEntry);
            } else {
                fds_panic("Trying to dispatch unknown pending command");
            }
        }
    }
}

/**
 * Handles requests that are pending a new DLT version.
 * This error case means an SM contacted has committed a
 * new DLT version that is different than the one used
 * in our original request. Token ownership may have
 * changed in the new version and we need to resend the
 * request to the new token owner.
 * Note, assumes the journal entry lock is already held
 * and the volume lock is already held.
 */
void
StorHvCtrl::handleDltMismatch(StorHvVolume *vol,
                              StorHvJournalEntry *journEntry) {
    fds_verify(vol != NULL);
    fds_verify(journEntry != NULL);

    // Mark the entry as pending a newer DLT
    journEntry->trans_state = FDS_TRANS_PENDING_DLT;

    // Queue up the transaction to get processed when we
    // receive a newer DLT
    vol->journal_tbl->pushPendingDltTrans(journEntry->trans_id);
}

fds::Error StorHvCtrl::putObjResp(const FDSP_MsgHdrTypePtr& rxMsg,
                                  const FDSP_PutObjTypePtr& putObjRsp) {
    fds::Error  err(ERR_OK);
    fds_int32_t result = 0;
    fds_verify(rxMsg->msg_code == FDSP_MSG_PUT_OBJ_RSP);

    fds_uint32_t transId = rxMsg->req_cookie; 
    fds_volid_t  volId   = rxMsg->glob_volume_id;
    StorHvVolume *vol    =  vol_table->getVolume(volId);
    fds_verify(vol != NULL);  // We should not receive resp for unknown vol

    StorHvVolumeLock vol_lock(vol);
    fds_verify(vol->isValidLocked() == true);  // We should not receive resp for unknown vol

    StorHvJournalEntry *txn = vol->journal_tbl->get_journal_entry(transId);
    fds_verify(txn != NULL);

    StorHvJournalEntryLock je_lock(txn); 

    // Verify transaction is valid and matches cookie from resp message
    fds_verify(txn->isActive() == true);

    // Check response code
    Error msgRespErr(rxMsg->err_code);
    if (msgRespErr == ERR_IO_DLT_MISMATCH) {
        ObjectID objId(putObjRsp->data_obj_id.digest);
        // Note: We're expecting the server to specify the version
        // expected in the response field.
        LOGERROR << "For transaction " << transId
                 << " received DLT version mismatch, have "
                 << om_client->getDltVersion()
                 << ", but SM service expected "
                 << putObjRsp->dlt_version;
        // response message  has the  latest  DLT , update the local copy 
        // and update the DLT version 

        storHvisor->om_client->updateDlt(true, putObjRsp->dlt_data);
        // find the replica index
        int idx = storHvisor->om_client->\
                getCurrentDLT()->getIndex(objId, NodeUuid(rxMsg->src_service_uuid.uuid));
        fds_verify(idx != -1);

        // resend to the same replica index in the new dlt
        storHvisor->dispatchSmPutMsg(txn,
                storHvisor->om_client->getCurrentDLT()->getNode(objId, idx));
        // Return here since we haven't received successful acks to
        // move the state machine forward.
        return err;
    }
    fds_verify(msgRespErr == ERR_OK);

    result = txn->fds_set_smack_status(rxMsg->src_ip_lo_addr,
                                       rxMsg->src_port);
    fds_verify(result == 0);
    LOGNORMAL << "For transaction " << transId
              << " recvd PUT_OBJ_RESP from SM ip "
              << rxMsg->src_ip_lo_addr
              << " port " << rxMsg->src_port;
    result = fds_move_wr_req_state_machine(rxMsg);
    fds_verify(result == 0);

    return err;
}

void
StorHvCtrl::startBlobTxResp(const FDSP_MsgHdrTypePtr rxMsg) {
    Error err(ERR_OK);

    // TODO(Andrew): We don't really need this...
    fds_verify(rxMsg->msg_code == FDSP_START_BLOB_TX);

    fds_uint32_t transId = rxMsg->req_cookie;
    fds_volid_t  volId   = rxMsg->glob_volume_id;

    StorHvVolume* vol = vol_table->getVolume(volId);
    fds_verify(vol != NULL);  // Should not receive resp for non existant vol

    StorHvVolumeLock vol_lock(vol);
    fds_verify(vol->isValidLocked() == true);

    StorHvJournalEntry *txn = vol->journal_tbl->get_journal_entry(transId);
    fds_verify(txn != NULL);

    StorHvJournalEntryLock je_lock(txn);
    fds_verify(txn->isActive() == true);
    fds_verify(txn->trans_state == FDS_TRANS_BLOB_START);

    // Get request from transaction
    fds::AmQosReq   *qosReq  = static_cast<fds::AmQosReq *>(txn->io);
    fds_verify(qosReq != NULL);
    fds::StartBlobTxReq *blobReq = static_cast<fds::StartBlobTxReq *>(
        qosReq->getBlobReqPtr());
    fds_verify(blobReq != NULL);
    fds_verify(blobReq->getIoType() == FDS_START_BLOB_TX);

    // Return if err
    if (rxMsg->result != FDSP_ERR_OK) {
        vol->journal_tbl->releaseTransId(transId);
        txn->reset();
        qos_ctrl->markIODone(txn->io);
        blobReq->cb->call(FDSN_StatusErrorUnknown);
        delete blobReq;
        return;
    }

    // Increment the ack status
    fds_int32_t result = txn->fds_set_dmack_status(rxMsg->src_ip_lo_addr,
                                                   rxMsg->src_port);
    fds_verify(result == 0);
    LOGDEBUG << "For AM transaction " << transId
             << " recvd start blob tx response from DM ip "
             << rxMsg->src_ip_lo_addr
             << " port " << rxMsg->src_port;

    // Move the state machine, which will call the callback
    // if we received enough messages
    // TODO(Andrew): It should just manage the state, not
    // actually do the work...
    result = fds_move_wr_req_state_machine(rxMsg);

    if (result == 1) {
        StartBlobTxCallback::ptr cb = SHARED_DYN_CAST(StartBlobTxCallback,
                                                      blobReq->cb);
        txn->reset();
        vol->journal_tbl->releaseTransId(transId);
        qos_ctrl->markIODone(txn->io);
        cb->call(ERR_OK);
        delete blobReq;
    } else {
        fds_verify(result == 0);
    }
}

void
StorHvCtrl::statBlobResp(const FDSP_MsgHdrTypePtr rxMsg, 
                         const FDS_ProtocolInterface::
                         BlobDescriptorPtr blobDesc) {
    Error err(ERR_OK);

    // TODO(Andrew): We don't really need this...
    fds_verify(rxMsg->msg_code == FDSP_STAT_BLOB);

    fds_uint32_t transId = rxMsg->req_cookie;
    fds_volid_t  volId   = rxMsg->glob_volume_id;

    StorHvVolume* vol = vol_table->getVolume(volId);
    fds_verify(vol != NULL);  // Should not receive resp for non existant vol

    StorHvVolumeLock vol_lock(vol);
    fds_verify(vol->isValidLocked() == true);

    StorHvJournalEntry *txn = vol->journal_tbl->get_journal_entry(transId);
    fds_verify(txn != NULL);

    StorHvJournalEntryLock je_lock(txn);
    fds_verify(txn->isActive() == true);

    // Get request from transaction
    fds::AmQosReq   *qosReq  = static_cast<fds::AmQosReq *>(txn->io);
    fds_verify(qosReq != NULL);
    fds::StatBlobReq *blobReq = static_cast<fds::StatBlobReq *>(
        qosReq->getBlobReqPtr());
    fds_verify(blobReq != NULL);
    fds_verify(blobReq->getIoType() == FDS_STAT_BLOB);

    qos_ctrl->markIODone(txn->io);

    // Return if err
    if (rxMsg->result != FDSP_ERR_OK) {
        vol->journal_tbl->releaseTransId(transId);
        blobReq->cb->call(FDSN_StatusErrorUnknown);
        txn->reset();
        delete blobReq;
        return;
    }

    StatBlobCallback::ptr cb = SHARED_DYN_CAST(StatBlobCallback,blobReq->cb);

    cb->blobDesc.setBlobName(blobDesc->name);
    cb->blobDesc.setBlobSize(blobDesc->byteCount);
    for (auto const &it : blobDesc->metadata) {
        cb->blobDesc.addKvMeta(it.first, it.second);
    }
    txn->reset();
    vol->journal_tbl->releaseTransId(transId);
    cb->call(ERR_OK);
    delete blobReq;
}

void StorHvCtrl::handleSetBlobMetaDataResp(const FDSP_MsgHdrTypePtr rxMsg) {
    Error err(ERR_OK);

    fds_uint32_t transId = rxMsg->req_cookie;
    fds_volid_t  volId   = rxMsg->glob_volume_id;

    StorHvVolume* vol = vol_table->getVolume(volId);
    fds_verify(vol != NULL);  // Should not receive resp for non existant vol

    StorHvVolumeLock vol_lock(vol);
    fds_verify(vol->isValidLocked() == true);

    StorHvJournalEntry *txn = vol->journal_tbl->get_journal_entry(transId);
    fds_verify(txn != NULL);

    StorHvJournalEntryLock je_lock(txn);
    fds_verify(txn->isActive() == true);

    // Get request from transaction
    fds::AmQosReq   *qosReq  = static_cast<fds::AmQosReq *>(txn->io);
    fds_verify(qosReq != NULL);
    fds::SetBlobMetaDataReq *blobReq = static_cast<fds::SetBlobMetaDataReq *>(
        qosReq->getBlobReqPtr());
    fds_verify(blobReq != NULL);
    fds_verify(blobReq->getIoType() == FDS_SET_BLOB_METADATA);

    // Return if err
    if (rxMsg->result != FDSP_ERR_OK) {
        vol->journal_tbl->releaseTransId(transId);
        txn->reset();
        qos_ctrl->markIODone(txn->io);
        blobReq->cb->call(FDSN_StatusErrorUnknown);
        return;
    }
    // Increment the ack status
    fds_int32_t result = txn->fds_set_dmack_status(rxMsg->src_ip_lo_addr,
                                                   rxMsg->src_port);
    LOGDEBUG << "txn: " << transId
             << " rcvd DM response ip:"
             << rxMsg->src_ip_lo_addr
             << " port:" << rxMsg->src_port;

    result = fds_move_wr_req_state_machine(rxMsg);
    if (1 == result) {
        txn->reset();
        vol->journal_tbl->releaseTransId(transId);
        qos_ctrl->markIODone(txn->io);
        blobReq->cb->call(ERR_OK);
    } else {
        fds_verify(result == 0);
    }
}

fds::Error StorHvCtrl::upCatResp(const FDSP_MsgHdrTypePtr& rxMsg,
                                 const FDSP_UpdateCatalogTypePtr& catObjRsp) {
    fds::Error  err(ERR_OK);
    fds_int32_t result = 0;
    fds_verify(rxMsg->msg_code == FDSP_MSG_UPDATE_CAT_OBJ_RSP);

    fds_uint32_t transId = rxMsg->req_cookie;
    fds_volid_t volId    = rxMsg->glob_volume_id;

    StorHvVolume *vol = vol_table->getVolume(volId);
    fds_verify(vol != NULL);

    StorHvVolumeLock vol_lock(vol);
    fds_verify(vol->isValidLocked() == true);

    StorHvJournalEntry *txn = vol->journal_tbl->get_journal_entry(transId);
    fds_verify(txn != NULL);
    StorHvJournalEntryLock je_lock(txn);
    fds_verify(txn->isActive() == true);  // Should not get resp for inactive txns
    fds_verify(txn->trans_state != FDS_TRANS_EMPTY);  // Should not get resp for empty txns

    // Check response code
    Error msgRespErr(rxMsg->err_code);
    fds_verify(msgRespErr == ERR_OK);  
    if (catObjRsp->dm_operation == FDS_DMGR_TXN_STATUS_OPEN) {
        result = txn->fds_set_dmack_status(rxMsg->src_ip_lo_addr,
                                           rxMsg->src_port);
        fds_verify(result == 0);
        LOGNORMAL << "Recvd DM TXN_STATUS_OPEN RSP for transId "
                  << transId  << " ip " << rxMsg->src_ip_lo_addr
                  << " port " << rxMsg->src_port;
    } else {
        fds_verify(catObjRsp->dm_operation == FDS_DMGR_TXN_STATUS_COMMITED);
        result = txn->fds_set_dm_commit_status(rxMsg->src_ip_lo_addr,
                                               rxMsg->src_port);
        fds_verify(result == 0);
        LOGNORMAL << "Recvd DM TXN_STATUS_COMMITED RSP for transId "
                  << transId  << " ip " << rxMsg->src_ip_lo_addr
                  << " port " << rxMsg->src_port;
    }  
    result = fds_move_wr_req_state_machine(rxMsg);
    fds_verify(result == 0);

    return err;
}

fds::Error StorHvCtrl::deleteCatResp(const FDSP_MsgHdrTypePtr& rxMsg,
                                     const FDSP_DeleteCatalogTypePtr& delCatRsp) {
    fds::Error err(ERR_OK);
    fds_int32_t result = 0;

    fds_verify(rxMsg->msg_code == FDSP_MSG_DELETE_BLOB_RSP);

    fds_uint32_t transId = rxMsg->req_cookie;
    fds_volid_t  volId   = rxMsg->glob_volume_id;

    StorHvVolume* vol = vol_table->getVolume(volId);
    fds_verify(vol != NULL);  // Should not receive resp for non existant vol

    StorHvVolumeLock vol_lock(vol);
    fds_verify(vol->isValidLocked() == true);

    StorHvJournalEntry *txn = vol->journal_tbl->get_journal_entry(transId);
    fds_verify(txn != NULL);

    StorHvJournalEntryLock je_lock(txn);
    if (txn->isActive() != true) {
        /*
         * TODO: This is a HACK to get multi-node delete working!
         * We're going to ignore inactive transactions for now because
         * we always respond and clean up the transaction on the first
         * response, meaning the second response doesn't have a transaction
         * to update. This is a result of only partially implementing the
         * delete transaction state.
         * We don't call the callback or delete the blob request because we
         * assume it's been done already by the first response received.    
         */
        return err;
    }
    fds_verify(txn->isActive() == true);  // Should not receive resp for inactive txn
    fds_verify(txn->trans_state == FDS_TRANS_DEL_OBJ);

    /*
     * TODO: We're short cutting here and responding to the client when we get
     * only the resonse from a single DM. We need to finish this transaction by
     * tracking all of the responses from all SMs/DMs in the journal.
     */

    /*
     *  check the version of the object, return if the version is NULL
     */

    /*
     * start accumulating the ack from  DM and  check for the min ack
     */

    // Check response code
    Error msgRespErr(rxMsg->err_code);
    fds_verify(msgRespErr == ERR_OK || msgRespErr == ERR_BLOB_NOT_FOUND);

    if (rxMsg->msg_code == FDSP_MSG_DELETE_BLOB_RSP) {
        txn->fds_set_dmack_status(rxMsg->src_ip_lo_addr,
                                  rxMsg->src_port);
        LOGNORMAL << " StorHvisorRx:" << "IO-XID:" << transId
                  << " volID: 0x" << std::hex << volId << std::dec
                  << " -  Recvd DM TXN_STATUS_OPEN RSP "
                  << " ip " << rxMsg->src_ip_lo_addr
                  << " port " << rxMsg->src_port;
        result = fds_move_del_req_state_machine(rxMsg);
        fds_verify(result == 0);
        return err;
    }

    return err;
}


Error StorHvCtrl::getBlob2(fds::AmQosReq *qosReq) {
    /*
     * Pull out the blob request
     */
    Error err = ERR_OK;
    fds::GetBlobReq *blobReq = static_cast<fds::GetBlobReq *>(
        qosReq->getBlobReqPtr());
    fds_verify(blobReq->magicInUse() == true);

    fds_volid_t   volId = blobReq->getVolId();
    StorHvVolume *shVol = vol_table->getVolume(volId);
    if ((shVol == NULL) || (shVol->isValidLocked() == false)) {
        LOGCRITICAL << "getBlob failed to get volume for vol "
                    << volId;    
        blobReq->cbWithResult(ERR_INVALID);
        err = ERR_DISK_WRITE_FAILED;
        qos_ctrl->markIODone(qosReq);
        delete blobReq;
        delete qosReq;
        return err;
    }

    // TODO(Rao): Do we need to make sure other get reqs aren't in progress

    ObjectID objId;
    bool inCache = shVol->vol_catalog_cache->LookupObjectId(blobReq->getBlobName(),
                                          blobReq->getBlobOffset(),
                                          objId);
    if (true || !inCache) {
        issueQueryCatalog(blobReq->getBlobName(),
                          blobReq->getBlobOffset(),
                          volId,
                          std::bind(&StorHvCtrl::getBlobQueryCatalogResp, this, qosReq,
                                    std::placeholders::_1, std::placeholders::_2,
                                    std::placeholders::_3));
    } else {
        fds_verify(objId != NullObjectID);
        issueGetObject(volId, objId,
                       std::bind(&StorHvCtrl::getBlobGetObjectResp, this, qosReq,
                                 std::placeholders::_1, std::placeholders::_2,
                                 std::placeholders::_3));
    }
    return err;
}

void StorHvCtrl::issueQueryCatalog(const std::string& blobName,
                       const fds_uint64_t& blobOffset,
                       const fds_volid_t& volId,
                       FailoverRpcRespCb respCb)
{
    LOGDEBUG << "blob name: " << blobName << " offset: " << blobOffset << " volid: " << volId;
    /*
     * TODO(Andrew): We should eventually specify the offset in the blob
     * we want...all objects won't work well for large blobs.
     */
    fpi::QueryCatalogMsgPtr queryMsg(new fpi::QueryCatalogMsg());
    queryMsg->volume_id = volId;
    queryMsg->blob_name             = blobName;
    // We don't currently specify a version
    queryMsg->blob_version          = blob_version_invalid;
    // TODO(rao): Figure out where this is used
    queryMsg->dm_transaction_id     = 1;
    queryMsg->dm_operation          =
        FDS_ProtocolInterface::FDS_DMGR_TXN_STATUS_OPEN;
    queryMsg->obj_list.clear();
    queryMsg->meta_list.clear();

    auto asyncQueryReq = gRpcRequestPool->newFailoverRpcRequest(
        boost::make_shared<DmtVolumeIdEpProvider>(om_client->getDMTNodesForVolume(volId)));
    asyncQueryReq->setRpcFunc(
        CREATE_RPC_SPTR(fpi::DMSvcClient, queryCatalogObject, queryMsg));
    asyncQueryReq->setTimeoutMs(500);
    asyncQueryReq->onResponseCb(respCb);
    asyncQueryReq->invoke();

    LOGDEBUG << asyncQueryReq->logString() << fds::logString(*queryMsg);
}

void StorHvCtrl::issueGetObject(const fds_volid_t& volId,
                                const ObjectID& objId,
                                FailoverRpcRespCb respCb)
{
    fpi::GetObjectMsgPtr getObjMsg(boost::make_shared<fpi::GetObjectMsg>());
    getObjMsg->volume_id = volId;
    getObjMsg->data_obj_id.digest = std::string((const char *)objId.GetId(),
                                                (size_t)objId.GetLen());

    auto asyncGetReq = gRpcRequestPool->newFailoverRpcRequest(
        boost::make_shared<DltObjectIdEpProvider>(om_client->getDLTNodesForDoidKey(objId)));
    asyncGetReq->setRpcFunc(
        CREATE_RPC_SPTR(fpi::SMSvcClient, getObject, getObjMsg));
    asyncGetReq->setTimeoutMs(500);
    asyncGetReq->onResponseCb(respCb);
    asyncGetReq->invoke();

    LOGDEBUG << asyncGetReq->logString() << fds::logString(*getObjMsg);
}

void StorHvCtrl::getBlobQueryCatalogResp(fds::AmQosReq* qosReq,
                                         FailoverRpcRequest* rpcReq,
                                         const Error& error,
                                         boost::shared_ptr<std::string> payload)
{
    fds::GetBlobReq *blobReq = static_cast<fds::GetBlobReq *>(qosReq->getBlobReqPtr());
    fpi::QueryCatalogMsgPtr qryCatRsp =
        NetMgr::ep_deserialize<fpi::QueryCatalogMsg>(const_cast<Error&>(error), payload);

    if (error != ERR_OK) {
        LOGERROR << "blob name: " << blobReq->getBlobName() << "offset: "
            << blobReq->getBlobOffset() << " Error: " << error; 
        blobReq->cbWithResult(ERR_INVALID);
        qos_ctrl->markIODone(qosReq);
        delete blobReq;
        return;
    }

    LOGDEBUG << rpcReq->logString() << fds::logString(*qryCatRsp);

    Error e = updateCatalogCache(blobReq,
                                 qryCatRsp->obj_list);
    if (e != ERR_OK) {
        LOGERROR << "blob name: " << blobReq->getBlobName() << "offset: "
            << blobReq->getBlobOffset() << " Error: " << e; 
        blobReq->cbWithResult(e.GetErrno());
        qos_ctrl->markIODone(qosReq);
        delete blobReq;
        return;
    }

    /* NOTE: For now making the assumption that there is only one object id */
    fds_verify(qryCatRsp->obj_list.size() == 1);
    ObjectID objId(qryCatRsp->obj_list[0].data_obj_id.digest);
    fds_verify(objId != NullObjectID);

    issueGetObject(blobReq->getVolId(), objId,
                   std::bind(&StorHvCtrl::getBlobGetObjectResp, this, qosReq,
                             std::placeholders::_1, std::placeholders::_2,
                             std::placeholders::_3));
}

void StorHvCtrl::getBlobGetObjectResp(fds::AmQosReq* qosReq,
                                      FailoverRpcRequest* rpcReq,
                                      const Error& error,
                                      boost::shared_ptr<std::string> payload)
{
    fds::GetBlobReq *blobReq = static_cast<fds::GetBlobReq *>(qosReq->getBlobReqPtr());
    fpi::GetObjectRespPtr getObjRsp =
        NetMgr::ep_deserialize<fpi::GetObjectResp>(const_cast<Error&>(error), payload);

    if (error != ERR_OK) {
        LOGERROR << "blob name: " << blobReq->getBlobName() << "offset: "
            << blobReq->getBlobOffset() << " Error: " << error; 
        blobReq->cbWithResult(ERR_INVALID);
        qos_ctrl->markIODone(qosReq);
        delete blobReq;
        return;
    }

    LOGDEBUG << rpcReq->logString() << fds::logString(*getObjRsp);

    fds_volid_t   volId = blobReq->getVolId();
    StorHvVolume *vol = vol_table->getVolume(volId);

    fds_verify(vol != NULL);  // Should not receive resp for non existant vol

    StorHvVolumeLock vol_lock(vol);
    fds_verify(vol->isValidLocked() == true);

    qos_ctrl->markIODone(qosReq);

    /* NOTE: we are currently supporting only getting the whole blob
     * so the requester does not know about the blob length, 
     * we get the blob length in response from SM;
     * will need to revisit when we also support (don't ignore) byteCount in native api.
     * For now, just verify the existing buffer is big enough to hold
     * the data.
     */
    if (blobReq->getDataLen() >= (4 * 1024)) {
        // Check that we didn't get too much data
        // Since 4K is our min, it's OK to get more
        // when less than 4K is requested
        // TODO(Andrew): Revisit for unaligned IO
        fds_verify((uint)(getObjRsp->data_obj_len) <= (blobReq->getDataLen()));
    }
    blobReq->setDataLen(getObjRsp->data_obj_len);    
    blobReq->setDataBuf(getObjRsp->data_obj.c_str());
    blobReq->cbWithResult(ERR_OK);
    delete blobReq;
    // TODO(Rao): Ask andrew why we can't delete this qosReq
    // delete qosReq;
}

fds::Error StorHvCtrl::updateCatalogCache(GetBlobReq *blobReq,
                                          FDS_ProtocolInterface::FDSP_BlobObjectList& blobOffList )
{
    // Get the volume specific to the request
    StorHvVolume *shvol = vol_table->getVolume(blobReq->getVolId());
    StorHvVolumeLock vol_lock(shvol);    
    if (!shvol || !shvol->isValidLocked()) {
        LOGERROR << " StorHvisorRx:" << " - Volume 0x"
                 << std::hex << blobReq->getVolId()
                 << std::dec <<  " not registered";
        return ERR_VOL_NOT_FOUND;
    }
    // Insert the returned entries into the cache
    fds_verify(blobOffList.empty() == false);
    bool offsetFound  = false;
    Error err = ERR_OK;
    fds_uint64_t req_offset = blobReq->getBlobOffset();
    for (FDS_ProtocolInterface::FDSP_BlobObjectList::const_iterator it =
                 blobOffList.cbegin();
         it != blobOffList.cend();
         it++) {
        ObjectID offsetObjId(((*it).data_obj_id).digest);
        // TODO(Andrew): Need to pass in if this is the last
        // offset in the blob or not...
        err = shvol->vol_catalog_cache->Update(blobReq->getBlobName(),
                                               (fds_uint64_t)(*it).offset,
                                               (fds_uint32_t)(*it).size,
                                               offsetObjId);
        fds_verify(err == ERR_OK);

        // Check if we received the offset we queried about
        // TODO(Andrew): Today DM just gives us the entire
        // blob, so it doesn't tell us if the offset we queried
        // about was in the list or not
        if ((fds_uint64_t)(*it).offset == req_offset) {
            offsetFound = true;
        }
    }

    if (offsetFound == false) {
        return FDSN_StatusEntityDoesNotExist;
    }
    return ERR_OK;
}

Error StorHvCtrl::putBlob2(fds::AmQosReq *qosReq) {
    fds::Error err(ERR_OK);
    
    // Pull out the blob request     
    PutBlobReq *blobReq = static_cast<PutBlobReq *>(qosReq->getBlobReqPtr());
    ObjectID objId;
    bool fZeroSize = (blobReq->getDataLen() == 0);
    fds_verify(blobReq->magicInUse() == true);

    // Get the volume context structure
    fds_volid_t   volId = blobReq->getVolId();
    StorHvVolume *shVol = storHvisor->vol_table->getLockedVolume(volId);
    fds_verify(shVol != NULL);
    fds_verify(shVol->isValidLocked() == true);

    // TODO(Andrew): Here we're turning the offset aligned
    // blobOffset back into an absolute blob offset (i.e.,
    // not aligned to the maximum object size). This allows
    // the rest of the putBlob routines to still expect an
    // absolute offset in case we need it
    fds_uint32_t maxObjSize = shVol->voldesc->maxObjSizeInBytes;
    blobReq->setBlobOffset(blobReq->getBlobOffset() * maxObjSize);

    // Track how long the request was queued before put() dispatch
    // TODO(Andrew): Consider moving to the QoS request
    blobReq->setQueuedUsec(shVol->journal_tbl->microsecSinceCtime(
        boost::posix_time::microsec_clock::universal_time()));

    if (fZeroSize) {
        LOGWARN << "zero size object - "
                << " [objkey:" << blobReq->ObjKey <<"]";
    } else {
        objId = ObjIdGen::genObjectId(blobReq->getDataBuf(),
                                      blobReq->getDataLen());
    }
    blobReq->setObjId(objId);

    // Send put request to SM
    issuePutObjectMsg(blobReq->getObjId(),
                      blobReq->getDataBuf(),
                      blobReq->getDataLen(),
                      volId,
                      std::bind(&StorHvCtrl::putBlobPutObjectMsgResp,
                                this, blobReq,
                                std::placeholders::_1,
                                std::placeholders::_2,std::placeholders::_3));

    issueUpdateCatalogMsg(blobReq->getObjId(),
                          blobReq->getBlobName(),
                          blobReq->getBlobOffset(),
                          blobReq->getDataLen(),
                          blobReq->isLastBuf(),
                          volId,
                          std::bind(&StorHvCtrl::putBlobUpdateCatalogMsgResp,
                                    this, blobReq,
                                    std::placeholders::_1,
                                    std::placeholders::_2,std::placeholders::_3));
    // TODO(Rao): Check with andrew if this is the right place to unlock or
    // can we unlock before
    // TODO(Rao): Check if we can use scoped lock here
    shVol->readUnlock();
    return err;
}

void StorHvCtrl::issuePutObjectMsg(const ObjectID &objId,
                                   const char* dataBuf,
                                   const fds_uint64_t &len,
                                   const fds_volid_t& volId,
                                   FailoverRpcRespCb respCb)
{
    if (len == 0) {
        // TODO(Rao): Shortcut SM.  Issue a call back to indicate put successed
    }
    PutObjectMsgPtr putObjMsg(new PutObjectMsg);
    putObjMsg->volume_id = volId;
    putObjMsg->origin_timestamp = fds::util::getTimeStampMillis();
    putObjMsg->data_obj = std::string(dataBuf, len);
    putObjMsg->data_obj_len = len;
    putObjMsg->data_obj_id.digest = std::string((const char *)objId.GetId(), (size_t)objId.GetLen());

#if 0
    auto asyncPutReq = gRpcRequestPool->newFailoverRpcRequest(
        boost::make_shared<DltObjectIdEpProvider>(om_client->getDLTNodesForDoidKey(objId)));
    asyncPutReq->setRpcFunc(
        CREATE_RPC_SPTR(fpi::SMSvcClient, putObject, putObjMsg));
    asyncPutReq->setTimeoutMs(500);
    asyncPutReq->onResponseCb(respCb);
    asyncPutReq->invoke();

    LOGDEBUG << asyncPutReq->logString() << fds::logString(*putObjMsg);
#endif
}

void StorHvCtrl::issueUpdateCatalogMsg(const ObjectID &objId,
                                       const std::string& blobName,
                                       const fds_uint64_t& blobOffset,
                                       const fds_uint64_t &len,
                                       const bool &lastBuf,
                                       const fds_volid_t& volId,
                                       FailoverRpcRespCb respCb)
{
    UpdateCatalogMsgPtr updCatMsg(new UpdateCatalogMsg());
    updCatMsg->blob_name   = blobName;
    updCatMsg->blob_version = blob_version_invalid;
    updCatMsg->volume_id = volId;


    // Setup blob offset updates
    // TODO(Andrew): Today we only expect one offset update
    // TODO(Andrew): Remove lastBuf when we have real transactions
    updCatMsg->obj_list.clear();
    FDS_ProtocolInterface::FDSP_BlobObjectInfo updBlobInfo;
    updBlobInfo.offset   = blobOffset;
    updBlobInfo.size     = len;
    updBlobInfo.blob_end = lastBuf;
    updBlobInfo.data_obj_id.digest =
            std::string((const char *)objId.GetId(), (size_t)objId.GetLen());
    // Add the offset info to the DM message
    updCatMsg->obj_list.push_back(updBlobInfo);

#if 0
    auto asyncUpdateCatReq = gRpcRequestPool->newFailoverRpcRequest(
        boost::make_shared<DltObjectIdEpProvider>(om_client->getDMTNodesForVolume(volId)));
    asyncUpdateCatReq->setRpcFunc(
        CREATE_RPC_SPTR(fpi::DMSvcClient, updateCatalog, updCatMsg));
    asyncUpdateCatReq->setTimeoutMs(500);
    asyncUpdateCatReq->onResponseCb(respCb);
    asyncUpdateCatReq->invoke();

    LOGDEBUG << asyncUpdateCatReq->logString() << fds::logString(*updCatMsg);
#endif
}

void StorHvCtrl::putBlobPutObjectMsgResp(PutBlobReq *blobReq,
                                         FailoverRpcRequest* rpcReq,
                                         const Error& error,
                                         boost::shared_ptr<std::string> payload)
{
    fpi::PutObjectRspMsgPtr putObjRsp =
        NetMgr::ep_deserialize<fpi::PutObjectRspMsg>(const_cast<Error&>(error), payload);

    if (error != ERR_OK) {
        LOGERROR << "Obj ID: " << blobReq->getObjId()
            << " blob name: " << blobReq->getBlobName()
            << " offset: " << blobReq->getBlobOffset() << " Error: " << error; 
    } else {
        LOGDEBUG << rpcReq->logString() << fds::logString(*putObjRsp);
    }
    blobReq->notifyPutObjectComplete(error);
}

void StorHvCtrl::putBlobUpdateCatalogMsgResp(PutBlobReq *blobReq,
                                             FailoverRpcRequest* rpcReq,
                                             const Error& error,
                                             boost::shared_ptr<std::string> payload)
{
    fpi::UpdateCatalogRspMsgPtr updCatRsp =
        NetMgr::ep_deserialize<fpi::UpdateCatalogRspMsg>(const_cast<Error&>(error), payload);

    if (error != ERR_OK) {
        LOGERROR << "Obj ID: " << blobReq->getObjId()
            << " blob name: " << blobReq->getBlobName()
            << " offset: " << blobReq->getBlobOffset() << " Error: " << error; 
    } else {
        LOGDEBUG << rpcReq->logString() << fds::logString(*updCatRsp);
    }
    blobReq->notifyUpdateCatalogComplete(error);

}

fds::Error StorHvCtrl::getBlob(fds::AmQosReq *qosReq)
{
    fds::Error err(ERR_OK);
    return getBlob2(qosReq);

    LOGNORMAL << "Doing a get blob operation!";

    /*
     * Pull out the blob request
     */
    FdsBlobReq *blobReq = qosReq->getBlobReqPtr();
    fds_verify(blobReq->magicInUse() == true);

    fds_volid_t   volId = blobReq->getVolId();
    StorHvVolume *shVol = vol_table->getVolume(volId);
    if ((shVol == NULL) || (shVol->isValidLocked() == false)) {
        LOGCRITICAL << "getBlob failed to get volume for vol "
                    << volId;    
        blobReq->cbWithResult(-1);
        err = ERR_DISK_WRITE_FAILED;
        delete qosReq;
        return err;
    }

    // TODO(Andrew): Here we're turning the offset aligned
    // blobOffset back into an absolute blob offset (i.e.,
    // not aligned to the maximum object size). This allows
    // the rest of the getBlob routines to still expect an
    // absolute offset in case we need it
    fds_uint32_t maxObjSize = shVol->voldesc->maxObjSizeInBytes;
    blobReq->setBlobOffset(blobReq->getBlobOffset() * maxObjSize);

    /*
     * Track how long the request was queued before get() dispatch
     * TODO: Consider moving to the QoS request
     */
    blobReq->setQueuedUsec(shVol->journal_tbl->microsecSinceCtime(
        boost::posix_time::microsec_clock::universal_time()));

    /*
     * Get/lock a journal entry for the request.
     */
    bool trans_in_progress = false;
    fds_uint32_t transId = shVol->journal_tbl->get_trans_id_for_blob(blobReq->getBlobName(),
                                                                     blobReq->getBlobOffset(),
                                                                     trans_in_progress);

    LOGNORMAL << "Assigning transaction ID " << transId
              << " to get request";

    StorHvJournalEntry *journEntry = shVol->journal_tbl->get_journal_entry(transId);
    fds_verify(journEntry != NULL);
    StorHvJournalEntryLock jeLock(journEntry);

    /*
     * Check if the entry is already active.
     */
    if ((trans_in_progress) || (journEntry->isActive() == true)) {
        /*
         * There is an on-going transaction for this offset
         * Queue this up for later processing.
         */

        // TODO: For now, just return error :-(
        LOGCRITICAL << "Transaction " << transId << " is already ACTIVE"
                    << ", just give up and return error.";
        blobReq->cbWithResult(-2);
        err = ERR_NOT_IMPLEMENTED;
        delete qosReq;
        return err;
    }
    journEntry->setActive();

    /*
     * Setup msg header
     */
    //  FDSP_MsgHdrTypePtr msgHdr = new FDSP_MsgHdrType;
    FDS_ProtocolInterface::FDSP_MsgHdrTypePtr msgHdr(new FDSP_MsgHdrType);
    InitSmMsgHdr(msgHdr);
    msgHdr->msg_code       = FDSP_MSG_GET_OBJ_REQ;
    msgHdr->msg_id         =  1;
    msgHdr->req_cookie     = transId;
    msgHdr->glob_volume_id = volId;
    msgHdr->src_ip_lo_addr = SRC_IP;
    msgHdr->src_port       = 0;
    //  msgHdr->src_node_name  = my_node_name;
    msgHdr->src_node_name = storHvisor->myIp;

    /*
     * Setup journal entry
     */
    journEntry->trans_state = FDS_TRANS_OPEN;
    // journEntry->write_ctx = (void *)req;
    // journEntry->comp_req = comp_req;
    // journEntry->comp_arg1 = arg1; // vbd
    // journEntry->comp_arg2 = arg2; //vreq
    journEntry->sm_msg = msgHdr; 
    journEntry->dm_msg = NULL;
    journEntry->sm_ack_cnt = 0;
    journEntry->dm_ack_cnt = 0;
    // TODO(Andrew): Depricate FDS_IO_READ in favor
    // of FDS_GET_BLOB
    journEntry->op = FDS_IO_READ;
    journEntry->data_obj_id.digest.clear(); 
    journEntry->data_obj_len = blobReq->getDataLen();
    journEntry->io = qosReq;
    journEntry->trans_state = FDS_TRANS_GET_OBJ;

    FDSP_GetObjTypePtr get_obj_req(new FDSP_GetObjType);
    journEntry->getMsg = get_obj_req;

    /*
     * Get the object ID from vcc and add it to journal entry and get msg
     */
    if (disableVcc == true) {
        shVol->vol_catalog_cache->Clear();
    }
    // TODO(Andrew): Here we need to check if the offset is aligned or handle
    // unaligned offsets by returning multiple object ids and a desired length
    ObjectID objId;
    err = shVol->vol_catalog_cache->Query(blobReq->getBlobName(),
                                          blobReq->getBlobOffset(),
                                          transId,
                                          &objId);
    if (err == ERR_PENDING_RESP) {
        LOGNOTIFY << "Vol catalog cache query pending: "
                  << " for blob " << blobReq->getBlobName()
                  << " and offset " << blobReq->getBlobOffset()
                  << " with err " << err;
        journEntry->trans_state = FDS_TRANS_VCAT_QUERY_PENDING;
        err = ERR_OK;
        return err;
    }

    bool fSizeZero = (objId == NullObjectID);

    fds_verify(err == ERR_OK);
    journEntry->data_obj_id.digest = std::string((const char *)objId.GetId(), (size_t)objId.GetLen());

    get_obj_req->data_obj_id.digest = std::string((const char *)objId.GetId(), (size_t)objId.GetLen());
    get_obj_req->data_obj_len = blobReq->getDataLen();
    if (fSizeZero) {
        LOGWARN << "zero size object - not sending get request to sm -"
                << " name:" << blobReq->getBlobName()
                << " txnid:" << transId;
        
        storHvisor->qos_ctrl->markIODone(journEntry->io);
        journEntry->trans_state = FDS_TRANS_DONE;

        blobReq->setDataLen(0);
        journEntry->reset();
        shVol->journal_tbl->releaseTransId(transId);
        blobReq->cbWithResult(FDSN_StatusEntityEmpty);
    } else {
        journEntry->trans_state = FDS_TRANS_GET_OBJ;
        LOGNORMAL << "Getting object " << objId << " for blob "
                  << blobReq->getBlobName() << " and offset "
                  << blobReq->getBlobOffset() << " in trans "
                  << transId;
        journEntry->nodeSeq = 0; // Primary node
        err = dispatchSmGetMsg(journEntry);
        fds_verify(err == ERR_OK);
        // Schedule a timer here to track the responses and the original request
        shVol->journal_tbl->schedule(journEntry->ioTimerTask, std::chrono::seconds(FDS_IO_LONG_TIME));
    }
    /*
     * Note je_lock destructor will unlock the journal entry automatically
     */
    return err;
}

fds::Error StorHvCtrl::getObjResp(const FDSP_MsgHdrTypePtr& rxMsg,
                                  const FDSP_GetObjTypePtr& getObjRsp) {
    fds::Error err(ERR_OK);
    fds_verify(rxMsg->msg_code == FDSP_MSG_GET_OBJ_RSP);

    fds_uint32_t transId = rxMsg->req_cookie;
    fds_volid_t volId    = rxMsg->glob_volume_id;

    StorHvVolume* vol = vol_table->getVolume(volId);
    fds_verify(vol != NULL);  // Should not receive resp for non existant vol

    StorHvVolumeLock vol_lock(vol);
    fds_verify(vol->isValidLocked() == true);

    StorHvJournalEntry *txn = vol->journal_tbl->get_journal_entry(transId);
    fds_verify(txn != NULL);

    StorHvJournalEntryLock je_lock(txn);
    fds_verify(txn->isActive() == true);  // Should not receive resp for inactive txn
    // Check response code
    Error msgRespErr(rxMsg->err_code);
    if (msgRespErr == ERR_IO_DLT_MISMATCH) {
        // Note: We're expecting the server to specify the version
        // expected in the response field.
        LOGERROR << "For transaction " << transId
                 << " received DLT version mismatch, have "
                 << om_client->getDltVersion()
                 << ", but SM service expected "
                 << getObjRsp->dlt_version;
        //handleDltMismatch(vol, txn);

        // response message  has the  latest  DLT , update the local copy 
        // and update the DLT version 
        storHvisor->om_client->updateDlt(true, getObjRsp->dlt_data);

        txn->nodeSeq = 0; // Primary node
        // resend the IO with latest DLT
        storHvisor->dispatchSmGetMsg(txn);
        // Return here since we haven't received successful acks to
        // move the state machine forward.
        return err;
    }

    fds_verify(rxMsg->result == FDS_ProtocolInterface::FDSP_ERR_OK);
    fds_verify(txn->trans_state == FDS_TRANS_GET_OBJ);

    /*
     * how to handle the length miss-match (requested  length and  recived legth from SM differ)?
     * We will have to handle sending more data due to length difference
     */

    /*
     * Data ready, respond to callback
     */
    fds::AmQosReq   *qosReq  = static_cast<fds::AmQosReq *>(txn->io);
    fds_verify(qosReq != NULL);
    fds::GetBlobReq *blobReq = static_cast<fds::GetBlobReq *>(
        qosReq->getBlobReqPtr());
    fds_verify(blobReq != NULL);

    LOGNOTIFY << "Responding to getBlob trans " << transId
              <<" for blob " << blobReq->getBlobName()
              << " and offset " << blobReq->getBlobOffset()
              << " length " << getObjRsp->data_obj_len
              << " with result " << rxMsg->result;
    /*
     * Mark the IO complete, clean up txn, and callback
     */
    qos_ctrl->markIODone(txn->io);
    if (rxMsg->result == FDSP_ERR_OK) {
        fds_verify(blobReq->getIoType() == FDS_GET_BLOB);
        /* NOTE: we are currently supporting only getting the whole blob
         * so the requester does not know about the blob length, 
         * we get the blob length in response from SM;
         * will need to revisit when we also support (don't ignore) byteCount in native api.
         * For now, just verify the existing buffer is big enough to hold
         * the data.
         */
        if (blobReq->getDataLen() >= (4 * 1024)) {
            // Check that we didn't get too much data
            // Since 4K is our min, it's OK to get more
            // when less than 4K is requested
            // TODO(Andrew): Revisit for unaligned IO
            fds_verify((uint)(getObjRsp->data_obj_len) <= (blobReq->getDataLen()));
        }
        blobReq->setDataLen(getObjRsp->data_obj_len);
        blobReq->setDataBuf(getObjRsp->data_obj.c_str());
        txn->reset();
        vol->journal_tbl->releaseTransId(transId);
        blobReq->cbWithResult(ERR_OK);
    } else {
        /*
         * We received an error from SM. check the Error. If the Obj Not found 
         * send a get object request to secondary SM
         */
        if (txn->nodeSeq < (txn->num_sm_nodes-1)) { 
           txn->nodeSeq += 1; // secondary node 
           err = dispatchSmGetMsg(txn);
           fds_verify(err == ERR_OK);
 
       }
       else {
        vol->journal_tbl->releaseTransId(transId);
        txn->reset();
        blobReq->cbWithResult(-1);        
        delete blobReq;
        blobReq = NULL;
       }
    }

    /*
     * TODO: We're deleting the request structure. This assumes
     * that the caller got everything they needed when the callback
     * was invoked.
     */
    delete blobReq;

    return err;
}

Error
StorHvCtrl::startBlobTx(AmQosReq *qosReq) {
    fds_verify(qosReq != NULL);

    Error err(ERR_OK);
    StartBlobTxReq *blobReq = static_cast<StartBlobTxReq *>(qosReq->getBlobReqPtr());
    fds_verify(blobReq != NULL);
    fds_verify(blobReq->magicInUse() == true);
    fds_verify(blobReq->getIoType() == FDS_START_BLOB_TX);

    fds_volid_t   volId = blobReq->getVolId();
    StorHvVolume *shVol = storHvisor->vol_table->getLockedVolume(volId);
    fds_verify(shVol != NULL);
    fds_verify(shVol->isValidLocked() == true);

    // Get a journal entry
    bool txInProgress;
    fds_uint32_t transId = shVol->journal_tbl->get_trans_id_for_blob(blobReq->getBlobName(),
                                                                     blobReq->getBlobOffset(),
                                                                     txInProgress);
    StorHvJournalEntry *journEntry = shVol->journal_tbl->get_journal_entry(transId);  
    StorHvJournalEntryLock je_lock(journEntry);

    // Just bail out if there's an outstanding request
    if ((txInProgress == true) ||
        (journEntry->isActive())) {
        shVol->readUnlock();

        LOGWARN << "Transaction " << transId << " is already in ACTIVE state, giving up...";
        // There is an ongoing transaciton for this offset.
        // We should queue this up for later processing once that completes.
        // For now, return an error.
        // shVol->journal_tbl->releaseTransId(transId);
        // journEntry->reset();
        blobReq->cb->call(FDSN_StatusTxnInProgress);
        err = ERR_NOT_IMPLEMENTED;
        delete blobReq;
        return err;
    }

    // Activate the journal entry
    journEntry->setActive();

    // Setup journal state
    journEntry->sm_msg     = NULL; 
    journEntry->dm_msg     = NULL;
    journEntry->sm_ack_cnt = 0;
    journEntry->dm_ack_cnt = 0;
    journEntry->op         = FDS_START_BLOB_TX;
    journEntry->io         = qosReq;
    // journEntry->delMsg      = del_obj_req;
    journEntry->trans_state = FDS_TRANS_BLOB_START;

    // Generate a random transaction ID to use
    // Note: construction, generates a random ID
    BlobTxId txId(storHvisor->randNumGen->genNum());
    LOGDEBUG << "Starting blob transaction " << txId << " for blob "
             << blobReq->getBlobName() << " in journal trans "
             << transId << " and vol 0x" << std::hex << volId << std::dec;

    // Stash the newly created ID in the callback for later
    StartBlobTxCallback::ptr cb = SHARED_DYN_CAST(StartBlobTxCallback,
                                                  blobReq->cb);
    cb->blobTxId = txId;

    // Setup RPC request to DM
    FDS_ProtocolInterface::FDSP_MsgHdrTypePtr msgHdr(new FDSP_MsgHdrType);
    storHvisor->InitDmMsgHdr(msgHdr);
    // Set vol and trans IDs
    msgHdr->glob_volume_id = volId;
    msgHdr->req_cookie = transId;
    // TODO(Andrew): These fields really aren't useful...
    msgHdr->msg_code       = FDSP_START_BLOB_TX;
    msgHdr->src_ip_lo_addr = 0;
    msgHdr->src_node_name  = "";
    msgHdr->src_port       = 0;
    msgHdr->dst_port       = 0;

    // Set up the FDSP trans request
    FDS_ProtocolInterface::TxDescriptorPtr fdspTxId(new TxDescriptor);
    fdspTxId->txId = txId.getValue();
    boost::shared_ptr<std::string> volNamePtr(
        new std::string(blobReq->getVolumeName()));
    boost::shared_ptr<std::string> blobNamePtr(
        new std::string(blobReq->getBlobName()));

    // Get the DM nodes info to query
    DmtColumnPtr nodeIds = dataPlacementTbl->getDMTNodesForVolume(volId);
    fds_verify(nodeIds->getLength() > 0);
    journEntry->num_dm_nodes = nodeIds->getLength();

    for (fds_uint32_t i = 0; i < nodeIds->getLength(); i++) {
        fds_uint32_t node_ip;
        fds_uint32_t node_port;
        fds_int32_t  node_state;
        storHvisor->dataPlacementTbl->getNodeInfo(nodeIds->get(i).uuid_get_val(),
                                                  &node_ip,
                                                  &node_port,
                                                  &node_state);
        // Get the DM endpoint to query
        netMetaDataPathClientSession *sessionCtx =
                storHvisor->rpcSessionTbl->                             \
                getClientSession<netMetaDataPathClientSession>(node_ip, node_port);
        fds_verify(sessionCtx != NULL);
        boost::shared_ptr<FDSP_MetaDataPathReqClient> client =
                sessionCtx->getClient();
        msgHdr->session_uuid = sessionCtx->getSessionId();

        // TODO(Andrew): Do I need to do this?...
        journEntry->dm_ack[i].ipAddr        = node_ip;
        journEntry->dm_ack[i].port          = node_port;
        journEntry->dm_ack[i].ack_status    = FDS_CLS_ACK;
        journEntry->dm_ack[i].commit_status = FDS_CLS_ACK;

        // TODO(Andrew): We can remove this when we get a new session layer
        msgHdr->dst_ip_lo_addr = node_ip;
        msgHdr->dst_port       = node_port;

        // Send async RPC request
        try {
            client->StartBlobTx(msgHdr,
                                volNamePtr,
                                blobNamePtr,
                                fdspTxId);
        } catch (att::TTransportException &e) {
            LOGERROR << "error during network call: " << e.what();
        }
    }

    shVol->readUnlock();
    // je_lock destructor will unlock the journal entry

    return err;
}

Error
StorHvCtrl::StatBlob(fds::AmQosReq *qosReq) {
    fds_verify(qosReq != NULL);

    Error err(ERR_OK);
    StatBlobReq *blobReq = static_cast<StatBlobReq *>(qosReq->getBlobReqPtr());
    fds_verify(blobReq != NULL);
    fds_verify(blobReq->magicInUse() == true);

    fds_volid_t   volId = blobReq->getVolId();
    StorHvVolume *shVol = storHvisor->vol_table->getLockedVolume(volId);
    fds_verify(shVol != NULL);
    fds_verify(shVol->isValidLocked() == true);

    // Get a journal entry
    bool txInProgress;
    fds_uint32_t transId = shVol->journal_tbl->get_trans_id_for_blob(blobReq->getBlobName(),
                                                                     blobReq->getBlobOffset(),
                                                                     txInProgress);
    StorHvJournalEntry *journEntry = shVol->journal_tbl->get_journal_entry(transId);  
    StorHvJournalEntryLock je_lock(journEntry);

    // Just bail out if there's an outstanding request
    if ((txInProgress == true) ||
        (journEntry->isActive())) {
        shVol->readUnlock();

        LOGWARN << "Transaction " << transId << " is already in ACTIVE state, giving up...";
        // There is an ongoing transaction for this offset.
        // We should queue this up for later processing once that completes.
        // For now, return an error.
        blobReq->cb->call();
        err = ERR_NOT_IMPLEMENTED;
        delete blobReq;
        delete qosReq;
        return err;
    }

    // Activate the journal entry
    journEntry->setActive();

    // Setup journal state
    journEntry->sm_msg      = NULL; 
    journEntry->dm_msg      = NULL;
    journEntry->sm_ack_cnt  = 0;
    journEntry->dm_ack_cnt  = 0;
    journEntry->op          = FDS_STAT_BLOB;
    journEntry->io          = qosReq;
    // journEntry->delMsg      = del_obj_req;
    // journEntry->trans_state = FDS_TRANS_DEL_OBJ;

    LOGDEBUG << "Stating blob " << blobReq->getBlobName() << " in trans "
              << transId << " and vol 0x" << std::hex << volId << std::dec;

    // Setup RPC request to DM
    FDS_ProtocolInterface::FDSP_MsgHdrTypePtr msgHdr(new FDSP_MsgHdrType);
    storHvisor->InitDmMsgHdr(msgHdr);
    // Set vol and trans IDs
    msgHdr->glob_volume_id = volId;
    msgHdr->req_cookie = transId;
    // TODO(Andrew): These fields really aren't useful...
    msgHdr->msg_code       = FDSP_STAT_BLOB;
    msgHdr->src_ip_lo_addr = 0;
    msgHdr->src_node_name  = "";
    msgHdr->src_port       = 0;
    msgHdr->dst_port       = 0;

    // Get the DM node info to query
    DmtColumnPtr nodeIds = dataPlacementTbl->getDMTNodesForVolume(volId);
    fds_verify(nodeIds->getLength() > 0);

    fds_uint32_t node_ip;
    fds_uint32_t node_port;
    fds_int32_t  node_state;
    storHvisor->dataPlacementTbl->getNodeInfo(nodeIds->get(0).uuid_get_val(),
                                              &node_ip,
                                              &node_port,
                                              &node_state);
    // Get the DM endpoint to query
    netMetaDataPathClientSession *sessionCtx =
            storHvisor->rpcSessionTbl->                                 \
            getClientSession<netMetaDataPathClientSession>(node_ip, node_port);
    fds_verify(sessionCtx != NULL);
    boost::shared_ptr<FDSP_MetaDataPathReqClient> client =
            sessionCtx->getClient();
    msgHdr->session_uuid = sessionCtx->getSessionId();

    boost::shared_ptr<std::string> volNamePtr(
        new std::string(blobReq->getVolumeName()));
    boost::shared_ptr<std::string> blobNamePtr(
        new std::string(blobReq->getBlobName()));

    // Send async RPC request
    try {
        client->StatBlob(msgHdr,
                         volNamePtr,
                         blobNamePtr);
    } catch (att::TTransportException &e) {
            LOGERROR << "error during network call: " << e.what();
    }

    shVol->readUnlock();
    // je_lock destructor will unlock the journal entry

    return err;
}

Error StorHvCtrl::SetBlobMetaData(fds::AmQosReq *qosReq) {
    fds_verify(qosReq != NULL);
    LOGDEBUG << "processing SetBlobMetaData for vol:" << qosReq->io_vol_id;
    Error err(ERR_OK);
    SetBlobMetaDataReq *blobReq = static_cast<SetBlobMetaDataReq *>(qosReq->getBlobReqPtr());
    fds_verify(blobReq != NULL);
    fds_verify(blobReq->magicInUse() == true);

    fds_volid_t   volId = blobReq->getVolId();
    StorHvVolume *shVol = storHvisor->vol_table->getLockedVolume(volId);
    fds_verify(shVol != NULL);
    fds_verify(shVol->isValidLocked() == true);

    // Get a journal entry
    bool txInProgress;
    fds_uint32_t transId = shVol->journal_tbl->get_trans_id_for_blob(blobReq->getBlobName(),
                                                                     blobReq->getBlobOffset(),
                                                                     txInProgress);
    StorHvJournalEntry *journEntry = shVol->journal_tbl->get_journal_entry(transId);
    StorHvJournalEntryLock je_lock(journEntry);

    // Just bail out if there's an outstanding request
    if ((txInProgress == true) ||
        (journEntry->isActive())) {
        shVol->readUnlock();

        LOGWARN << "Transaction " << transId << " is already in ACTIVE state, giving up...";
        // There is an ongoing transaction for this offset.
        // We should queue this up for later processing once that completes.
        // For now, return an error.
        blobReq->cb->call();
        err = ERR_NOT_IMPLEMENTED;
        delete blobReq;
        delete qosReq;
        return err;
    }
    journEntry->trans_state = FDS_TRANS_MULTIDM;

    // Activate the journal entry
    journEntry->setActive();

    // Setup journal state
    journEntry->sm_msg      = NULL;
    journEntry->dm_msg      = NULL;
    journEntry->sm_ack_cnt  = 0;
    journEntry->dm_ack_cnt  = 0;
    journEntry->op          = FDS_SET_BLOB_METADATA;
    journEntry->io          = qosReq;
    // journEntry->delMsg      = del_obj_req;
    // journEntry->trans_state = FDS_TRANS_DEL_OBJ;

    LOGDEBUG << " setting  meta for blob: " << blobReq->getBlobName()
             << " rxnid: " << transId
             << " volume: 0x" << std::hex << volId << std::dec;

    // Setup RPC request to DM
    FDS_ProtocolInterface::FDSP_MsgHdrTypePtr msgHdr(new FDSP_MsgHdrType);
    storHvisor->InitDmMsgHdr(msgHdr);
    // Set vol and trans IDs
    msgHdr->glob_volume_id = volId;
    msgHdr->req_cookie = transId;
    // TODO(Andrew): These fields really aren't useful...
    msgHdr->msg_code       = FDSP_STAT_BLOB;
    msgHdr->src_ip_lo_addr = 0;
    msgHdr->src_node_name  = "";
    msgHdr->src_port       = 0;
    msgHdr->dst_port       = 0;
    InitDmMsgHdr(msgHdr);

    // Get DMT node list from dmt
    DmtColumnPtr nodeIds = dataPlacementTbl->getDMTNodesForVolume(volId);
    fds_verify(nodeIds->getLength() > 0);

    boost::shared_ptr<std::string> volNamePtr(
        new std::string(blobReq->volumeName));
    boost::shared_ptr<std::string> blobNamePtr(
        new std::string(blobReq->getBlobName()));

    uint errcount = 0;
    for (fds_uint32_t i = 0; i < nodeIds->getLength(); i++) {
        fds_uint32_t node_ip   = 0;
        fds_uint32_t node_port = 0;
        fds_int32_t node_state = -1;
        dataPlacementTbl->getNodeInfo(nodeIds->get(i).uuid_get_val(),
                                      &node_ip,
                                      &node_port,
                                      &node_state);
        journEntry->dm_ack[i].ipAddr        = node_ip;
        journEntry->dm_ack[i].port          = node_port;
        msgHdr->dst_ip_lo_addr              = node_ip;
        msgHdr->dst_port                    = node_port;
        journEntry->dm_ack[i].ack_status    = FDS_CLS_ACK;
        journEntry->dm_ack[i].commit_status = FDS_CLS_ACK;
        journEntry->num_dm_nodes            = nodeIds->getLength();

        // Call Update Catalog RPC call to DM
        try {
            netMetaDataPathClientSession *sessionCtx = storHvisor->rpcSessionTbl->\
                    getClientSession<netMetaDataPathClientSession>(node_ip, node_port);
            fds_verify(sessionCtx != NULL);

            boost::shared_ptr<FDSP_MetaDataPathReqClient> client = sessionCtx->getClient();
            msgHdr->session_uuid = sessionCtx->getSessionId();
            journEntry->session_uuid = msgHdr->session_uuid;

            client->SetBlobMetaData(msgHdr, volNamePtr, blobNamePtr, blobReq->metaDataList);


            LOGNORMAL << "For transaction " << transId
                      << " sent async UP_CAT_REQ to DM ip "
                      << node_ip << " port " << node_port;
        } catch (att::TTransportException& e) {
            LOGERROR << "error during network call : " << e.what() ;
            errcount++;
        }
    }

    shVol->readUnlock();
    // je_lock destructor will unlock the journal entry

    return err;
}

/**
 * Function called when volume waiting queue is drained.
 * When it's called a volume has just been attached and
 * we can call the callback to tell any waiters that the
 * volume is now ready.
 */
void
StorHvCtrl::attachVolume(AmQosReq *qosReq) {
    // Get request from qos object
    fds_verify(qosReq != NULL);
    AttachVolBlobReq *blobReq = static_cast<AttachVolBlobReq *>(
        qosReq->getBlobReqPtr());
    fds_verify(blobReq != NULL);
    fds_verify(blobReq->getIoType() == FDS_ATTACH_VOL);

    LOGDEBUG << "Attach for volume " << blobReq->getVolumeName()
             << " complete. Notifying waiters";
    blobReq->cb->call(ERR_OK);
}

/*****************************************************************************

 *****************************************************************************/
fds::Error StorHvCtrl::deleteBlob(fds::AmQosReq *qosReq) {
    netSession *endPoint = NULL;
    unsigned int node_ip = 0;
    fds_uint32_t node_port = 0;
    unsigned int doid_dlt_key=0;
    int node_state = -1;
    fds::Error err(ERR_OK);
    ObjectID oid;
    FdsBlobReq *blobReq = qosReq->getBlobReqPtr();
    fds_verify(blobReq->magicInUse() == true);
    DeleteBlobReq *del_blob_req = (DeleteBlobReq *)blobReq;

    fds_volid_t   vol_id = blobReq->getVolId();
    StorHvVolume *shVol = storHvisor->vol_table->getLockedVolume(vol_id);
    if ((shVol == NULL) || (shVol->isValidLocked() == false)) {
        shVol->readUnlock();
        LOGCRITICAL << "deleteBlob failed to get volume for vol 0x"
                    << std::hex << vol_id << std::dec;
    
        blobReq->cbWithResult(-1);
        err = ERR_DISK_WRITE_FAILED;
        delete qosReq;
        return err;
    }

    /* Check if there is an outstanding transaction for this block offset  */
    bool trans_in_progress = false;
    fds_uint32_t transId = shVol->journal_tbl->get_trans_id_for_blob(blobReq->getBlobName(),
                                                                     blobReq->getBlobOffset(),
                                                                     trans_in_progress);
    StorHvJournalEntry *journEntry = shVol->journal_tbl->get_journal_entry(transId);
  
    StorHvJournalEntryLock je_lock(journEntry);
  
    if ((trans_in_progress) || (journEntry->isActive())) {
        shVol->readUnlock();

        LOGNORMAL <<" StorHvisorTx:" << "IO-XID:" << transId << " - Transaction  is already in ACTIVE state, completing request "
                  << transId << " with ERROR(-2) ";
        // There is an ongoing transaciton for this offset.
        // We should queue this up for later processing once that completes.
    
        // For now, return an error.
        blobReq->cbWithResult(-2);
        err = ERR_NOT_IMPLEMENTED;
        delete qosReq;
        return err;
    }

    journEntry->setActive();

    FDS_ProtocolInterface::FDSP_DeleteObjTypePtr del_obj_req(new FDSP_DeleteObjType);
  
    //  journEntry->trans_state = FDS_TRANS_OPEN;
    journEntry->sm_msg = NULL; 
    journEntry->dm_msg = NULL;
    journEntry->sm_ack_cnt = 0;
    journEntry->dm_ack_cnt = 0;
    journEntry->op = FDS_DELETE_BLOB;
    journEntry->data_obj_id.digest.clear(); 
    journEntry->data_obj_len = blobReq->getDataLen();
    journEntry->io = qosReq;
    journEntry->delMsg = del_obj_req;
    journEntry->trans_state = FDS_TRANS_DEL_OBJ;

    LOGNORMAL << "Deleting blob " << blobReq->getBlobName() << " in trans "
              << transId << " and vol 0x" << std::hex << vol_id << std::dec;
    
    // SAN- check the  version of the object. If the object version NULL ( object deleted) return

    del_obj_req->data_obj_id.digest = std::string((const char *)oid.GetId(), (size_t)oid.GetLen());
    del_obj_req->data_obj_len = blobReq->getDataLen();
  
    journEntry->op = FDS_DELETE_BLOB;
    journEntry->data_obj_id.digest = std::string((const char *)oid.GetId(), (size_t)oid.GetLen());
    journEntry->trans_state = FDS_TRANS_DEL_OBJ;

    // Invalidate the local cache entry for the blob.
    // We can do this here, even before sending the messages to DM
    // since even if the delete fails, it'll just produce a later
    // cache miss that will go to DM anyways.
    err = shVol->vol_catalog_cache->clearEntry(blobReq->getBlobName());
    fds_verify(err == ERR_OK);
  
    // RPC Call DeleteCatalogObject to DataMgr
    FDS_ProtocolInterface::FDSP_DeleteCatalogTypePtr del_cat_obj_req(new FDSP_DeleteCatalogType);
    FDS_ProtocolInterface::FDSP_MsgHdrTypePtr fdsp_msg_hdr_dm(new FDSP_MsgHdrType);
    storHvisor->InitDmMsgHdr(fdsp_msg_hdr_dm);
    fdsp_msg_hdr_dm->msg_code = FDSP_MSG_DELETE_BLOB_REQ;
    fdsp_msg_hdr_dm->glob_volume_id = vol_id;
    fdsp_msg_hdr_dm->req_cookie = transId;
    fdsp_msg_hdr_dm->src_ip_lo_addr = SRC_IP;
    //  fdsp_msg_hdr_dm->src_node_name = storHvisor->my_node_name;
    fdsp_msg_hdr_dm->src_node_name = storHvisor->myIp;
    fdsp_msg_hdr_dm->src_port = 0;
    fdsp_msg_hdr_dm->dst_port = node_port;
    journEntry->dm_msg = fdsp_msg_hdr_dm;
    DmtColumnPtr node_ids = storHvisor->dataPlacementTbl->getDMTNodesForVolume(vol_id);
    uint errcount = 0;
    for (fds_uint32_t i = 0; i < node_ids->getLength(); i++) {
        node_ip = 0;
        node_port = 0;
        node_state = -1;
        storHvisor->dataPlacementTbl->getNodeInfo((node_ids->get(i)).uuid_get_val(),
                                                  &node_ip,
                                                  &node_port,
                                                  &node_state);
    
        journEntry->dm_ack[i].ipAddr = node_ip;
        journEntry->dm_ack[i].port = node_port;
        fdsp_msg_hdr_dm->dst_ip_lo_addr = node_ip;
        fdsp_msg_hdr_dm->dst_port = node_port;
        journEntry->dm_ack[i].ack_status = FDS_CLS_ACK;
        journEntry->dm_ack[i].commit_status = FDS_CLS_ACK;
        journEntry->num_dm_nodes = node_ids->getLength();
        del_cat_obj_req->blob_name = blobReq->getBlobName();
        // TODO(Andrew): Set to a specific version rather than
        // always just the current
        del_cat_obj_req->blob_version = blob_version_invalid;

        // Call Update Catalog RPC call to DM
        try {
            netMetaDataPathClientSession *sessionCtx =
                    storHvisor->rpcSessionTbl->\
                    getClientSession<netMetaDataPathClientSession>(node_ip, node_port);
            fds_verify(sessionCtx != NULL);
            boost::shared_ptr<FDSP_MetaDataPathReqClient> client =
                    sessionCtx->getClient();

            fdsp_msg_hdr_dm->session_uuid = sessionCtx->getSessionId();
            journEntry->session_uuid = fdsp_msg_hdr_dm->session_uuid;
            client->DeleteCatalogObject(fdsp_msg_hdr_dm, del_cat_obj_req);
            LOGNORMAL << " txnid:" << transId
                      << " volID:" << std::hex << vol_id << std::dec
                      << " - sent DELETE_BLOB_REQ request to DM @ "
                      <<  node_ip << ":" << node_port;
        } catch (att::TTransportException& e) {
            errcount ++;
            LOGERROR << "error during network call: ["<< errcount << "] : " << e.what() ;
        }
    }

    shVol->readUnlock();

    return ERR_OK; // je_lock destructor will unlock the journal entry
}

fds::Error StorHvCtrl::listBucket(fds::AmQosReq *qosReq) {
    fds::Error err(ERR_OK);
    netSession *endPoint = NULL;
    unsigned int node_ip = 0;
    fds_uint32_t node_port = 0;
    int node_state = -1;

    LOGNORMAL << "Doing a list bucket operation!";

    /*
     * Pull out the blob request
     */
    FdsBlobReq *blobReq = qosReq->getBlobReqPtr();
    fds_verify(blobReq->magicInUse() == true);

    fds_volid_t   volId = blobReq->getVolId();
    StorHvVolume *shVol = vol_table->getVolume(volId);
    if ((shVol == NULL) || (shVol->isValidLocked() == false)) {
        LOGCRITICAL << "listBucket failed to get volume for vol "
                    << volId;    
        blobReq->cbWithResult(-1);
        err = ERR_NOT_FOUND;
        delete qosReq;
        return err;
    }

    /*
     * Track how long the request was queued before get() dispatch
     * TODO: Consider moving to the QoS request
     */
    blobReq->setQueuedUsec(shVol->journal_tbl->microsecSinceCtime(
        boost::posix_time::microsec_clock::universal_time()));

    bool trans_in_progress = false;
    fds_uint32_t transId = shVol->journal_tbl->get_trans_id_for_blob(blobReq->getBlobName(),
                                                                     blobReq->getBlobOffset(),
                                                                     trans_in_progress);
    StorHvJournalEntry *journEntry = shVol->journal_tbl->get_journal_entry(transId);
  
    StorHvJournalEntryLock je_lock(journEntry);
  
    if ((trans_in_progress) || (journEntry->isActive())) {
        LOGERROR <<" StorHvisorTx:" << "IO-XID:" << transId 
                 << " - Transaction  is already in ACTIVE state, completing request "
                 << transId << " with ERROR(-2) ";
        // There is an ongoing transaciton for this bucket.
        // We should queue this up for later processing once that completes.
    
        // For now, return an error.
        blobReq->cbWithResult(-2);
        err = ERR_INVALID_ARG;
        delete qosReq;
        return err;
    }

    journEntry->setActive();

    LOGNORMAL << " StorHvisorTx:" << "IO-XID:" << transId << " volID: 0x"
              << std::hex << volId << std::dec << " - Activated txn for req :" << transId;

    /*
     * Setup msg header
     */

    FDSP_MsgHdrTypePtr msgHdr(new FDSP_MsgHdrType);
    InitDmMsgHdr(msgHdr);
    msgHdr->msg_code       = FDSP_MSG_GET_VOL_BLOB_LIST_REQ;
    msgHdr->msg_id         =  1;
    msgHdr->req_cookie     = transId;
    msgHdr->glob_volume_id = volId;
    msgHdr->src_ip_lo_addr = SRC_IP;
    msgHdr->src_port       = 0;
    //  msgHdr->src_node_name  = my_node_name;
    msgHdr->src_node_name = storHvisor->myIp;
    msgHdr->bucket_name    = blobReq->getBlobName(); /* ListBucketReq stores bucket name in blob name */
 
    /*
     * Setup journal entry
     */
    journEntry->trans_state = FDS_TRANS_GET_BUCKET;
    journEntry->sm_msg = NULL; 
    journEntry->dm_msg = msgHdr;
    journEntry->sm_ack_cnt = 0;
    journEntry->dm_ack_cnt = 0;
    journEntry->op = FDS_LIST_BUCKET;
    journEntry->data_obj_id.digest.clear(); 
    journEntry->data_obj_len = 0;
    journEntry->io = qosReq;
    DmtColumnPtr node_ids = dataPlacementTbl->getDMTNodesForVolume(volId);
    if (node_ids->getLength() == 0) {
        LOGERROR <<" StorHvisorTx:" << "IO-XID:" << transId << " volID: 0x" << std::hex << volId << std::dec 
                 << " -  DMT Nodes  NOT  configured. Check on OM Manager. Completing request with ERROR(-1)";
        blobReq->cbWithResult(-1);
        err = ERR_GET_DMT_FAILED;
        delete qosReq;
        return err;
    }

    /* getting from first DM in the list */
    FDS_ProtocolInterface::FDSP_GetVolumeBlobListReqTypePtr get_bucket_list_req
            (new FDS_ProtocolInterface::FDSP_GetVolumeBlobListReqType);

    node_ip = 0;
    node_port = 0;
    node_state = -1;
    dataPlacementTbl->getNodeInfo((node_ids->get(0)).uuid_get_val(),
                                  &node_ip,
                                  &node_port,
                                  &node_state);
    
    msgHdr->dst_ip_lo_addr = node_ip;
    msgHdr->dst_port = node_port;

    get_bucket_list_req->max_blobs_to_return = static_cast<ListBucketReq*>(blobReq)->maxkeys;
    get_bucket_list_req->iterator_cookie = static_cast<ListBucketReq*>(blobReq)->iter_cookie;

    // Call Get Volume Blob List to DM
    try {
        netMetaDataPathClientSession *sessionCtx =
                storHvisor->rpcSessionTbl->\
                getClientSession<netMetaDataPathClientSession>(node_ip, node_port);
        fds_verify(sessionCtx != NULL);
        boost::shared_ptr<FDSP_MetaDataPathReqClient> client = sessionCtx->getClient();

        msgHdr->session_uuid = sessionCtx->getSessionId();
        journEntry->session_uuid = msgHdr->session_uuid;
        client->GetVolumeBlobList(msgHdr, get_bucket_list_req);
        LOGNORMAL << " StorHvisorTx:" << "IO-XID:"
                  << transId << " volID:" << std::hex << volId << std::dec
                  << " - Sent async GET_VOL_BLOB_LIST_REQ request to DM at "
                  <<  node_ip << " port " << node_port;
        // Schedule a timer here to track the responses and the original request
        shVol->journal_tbl->schedule(journEntry->ioTimerTask, std::chrono::seconds(FDS_IO_LONG_TIME));
    } catch (att::TTransportException& e) {
        LOGERROR << "error during network call : " << e.what() ;
    }
    return err; // je_lock destructor will unlock the journal entry
}

fds::Error StorHvCtrl::getBucketResp(const FDSP_MsgHdrTypePtr& rxMsg,
				     const FDSP_GetVolumeBlobListRespTypePtr& blobListResp)
{
    fds::Error err(ERR_OK);
    fds_uint32_t transId = rxMsg->req_cookie;
    fds_volid_t volId    = rxMsg->glob_volume_id;

    fds_verify(rxMsg->msg_code == FDS_ProtocolInterface::FDSP_MSG_GET_VOL_BLOB_LIST_RSP);

    StorHvVolume* vol = vol_table->getVolume(volId);
    fds_verify(vol != NULL);  // Should not receive resp for non existant vol

    StorHvVolumeLock vol_lock(vol);
    fds_verify(vol->isValidLocked() == true);

    StorHvJournalEntry *txn = vol->journal_tbl->get_journal_entry(transId);
    fds_verify(txn != NULL);

    StorHvJournalEntryLock je_lock(txn);
    fds_verify(txn->isActive() == true);  // Should not receive resp for inactive txn
    fds_verify(txn->trans_state == FDS_TRANS_GET_BUCKET);


    /*
     * List of blobs ready, respond to callback
     */
    fds::AmQosReq   *qosReq  = static_cast<fds::AmQosReq *>(txn->io);
    fds_verify(qosReq != NULL);
    fds::FdsBlobReq *blobReq = qosReq->getBlobReqPtr();
    fds_verify(blobReq != NULL);
    fds_verify(blobReq->getIoType() == FDS_LIST_BUCKET);
    LOGNOTIFY << "Responding to getBucket trans " << transId
              <<" for bucket " << blobReq->getBlobName()
              << " num of blobs " << blobListResp->num_blobs_in_resp
              << " end_of_list? " << blobListResp->end_of_list
              << " with result " << rxMsg->result;
    /*
     * Mark the IO complete, clean up txn, and callback
     */
    qos_ctrl->markIODone(txn->io);
    txn->reset();
    vol->journal_tbl->releaseTransId(transId);

    if (rxMsg->result == FDSP_ERR_OK) {
        ListBucketContents* contents = new ListBucketContents[blobListResp->num_blobs_in_resp];
        fds_verify(contents != NULL);
        fds_verify((uint)(blobListResp->num_blobs_in_resp) == (blobListResp->blob_info_list).size());
        for (int i = 0; i < blobListResp->num_blobs_in_resp; ++i)
        {
            contents[i].set((blobListResp->blob_info_list)[i].blob_name,
                            0, // last modified
                            "",  // eTag
                            (blobListResp->blob_info_list)[i].blob_size, 
                            "", // ownerId
                            "");
        }

        /* in case there are more blobs in the list, remember iter_cookie */
        ListBucketReq* list_buck = static_cast<ListBucketReq*>(blobReq);
        list_buck->iter_cookie = blobListResp->iterator_cookie;

        /* call ListBucketReq's callback directly */
        list_buck->DoCallback( (blobListResp->end_of_list == true) ? 0 : 1, //isTrancated == 0 if no more blobs to return?
                               "", // next_marker ?
                               blobListResp->num_blobs_in_resp,
                               contents,
                               ERR_OK,
                               NULL);

    } else {
        /*
         * We received an error from SM
         */
        blobReq->cbWithResult(-1);
    }

    /* if there are more blobs to return, we update blobReq with new iter_cookie 
     * that we got from SM and queue back to QoS queue 
     * TODO: or should we not release transaction ? */
    if (blobListResp->end_of_list == false) {
        LOGNOTIFY << "GetBucketResp -- bucket " << blobReq->getBlobName()
                  << " has more blobs, queueing request for next list of blobs";
        pushBlobReq(blobReq);
        return err;
    }

    /*
     * TODO: We're deleting the request structure. This assumes
     * that the caller got everything they needed when the callback
     * was invoked.
     */
    delete blobReq;

    return err;

}

fds::Error StorHvCtrl::getBucketStats(fds::AmQosReq *qosReq) {
    fds::Error err(ERR_OK);
    int om_err = 0;
  
    LOGNORMAL << "Doing a get bucket stats operation!";
  
    /*
     * Pull out the blob request
     */
    FdsBlobReq *blobReq = qosReq->getBlobReqPtr();
    fds_verify(blobReq->magicInUse() == true);

    fds_volid_t   volId = blobReq->getVolId();
    fds_verify(volId == admin_vol_id); /* must be in admin queue */
  
    StorHvVolume *shVol = vol_table->getVolume(volId);
    if ((shVol == NULL) || (shVol->isValidLocked() == false)) {
        LOGCRITICAL << "getBucketStats failed to get admin volume";
        blobReq->cbWithResult(-1);
        err = ERR_NOT_FOUND;
        delete qosReq;
        return err;
    }

    /*
     * Track how long the request was queued before get() dispatch
     * TODO: Consider moving to the QoS request
     */
    blobReq->setQueuedUsec(shVol->journal_tbl->microsecSinceCtime(
        boost::posix_time::microsec_clock::universal_time()));

    bool trans_in_progress = false;
    fds_uint32_t transId = shVol->journal_tbl->get_trans_id_for_blob(blobReq->getBlobName(),
                                                                     blobReq->getBlobOffset(),
                                                                     trans_in_progress);
    StorHvJournalEntry *journEntry = shVol->journal_tbl->get_journal_entry(transId);
  
    StorHvJournalEntryLock je_lock(journEntry);

    if ((trans_in_progress) || (journEntry->isActive())) {
        LOGERROR <<" StorHvisorTx:" << "IO-XID:" << transId 
                 << " - Transaction  is already in ACTIVE state, completing request "
                 << transId << " with ERROR(-2) ";
        // There is an ongoing transaciton for this bucket.
        // We should queue this up for later processing once that completes.
    
        // For now, return an error.
        blobReq->cbWithResult(-2);
        err = ERR_NOT_IMPLEMENTED;
        delete qosReq;
        return err;
    }

    journEntry->setActive();

    LOGNORMAL <<" StorHvisorTx:" << "IO-XID:" << transId << " volID: 0x"
              << std::hex << admin_vol_id << std::dec << " - Activated txn for req :" << transId;
 
    /*
     * Setup journal entry
     */
    journEntry->trans_state = FDS_TRANS_BUCKET_STATS;
    journEntry->sm_msg = NULL; 
    journEntry->dm_msg = NULL;
    journEntry->sm_ack_cnt = 0;
    journEntry->dm_ack_cnt = 0;
    journEntry->op = FDS_BUCKET_STATS;
    journEntry->data_obj_id.digest.clear(); 
    journEntry->data_obj_len = 0;
    journEntry->io = qosReq;
  
    /* send request to OM */
    om_err = om_client->pushGetBucketStatsToOM(transId);

    if(om_err != 0) {
        LOGERROR <<" StorHvisorTx:" << "IO-XID:" << transId << " volID: 0x"
                 << std::hex << admin_vol_id << std::dec
                 << " -  Couldn't send get bucket stats to OM. Completing request with ERROR(-1)";
        blobReq->cbWithResult(-1);
        err = ERR_NOT_FOUND;
        delete qosReq;
        return err;
    }

    LOGNORMAL << " StorHvisorTx:" << "IO-XID:"
              << transId 
              << " - Sent async Get Bucket Stats request to OM";

    // Schedule a timer here to track the responses and the original request
    shVol->journal_tbl->schedule(journEntry->ioTimerTask, std::chrono::seconds(FDS_IO_LONG_TIME));
    return err;
}

void StorHvCtrl::bucketStatsRespHandler(const FDSP_MsgHdrTypePtr& rx_msg,
					const FDSP_BucketStatsRespTypePtr& buck_stats) {
    storHvisor->getBucketStatsResp(rx_msg, buck_stats);
}

void StorHvCtrl::getBucketStatsResp(const FDSP_MsgHdrTypePtr& rx_msg,
				    const FDSP_BucketStatsRespTypePtr& buck_stats)
{
    fds_uint32_t transId = rx_msg->req_cookie;
 
    fds_verify(rx_msg->msg_code == FDS_ProtocolInterface::FDSP_MSG_GET_BUCKET_STATS_RSP);

    StorHvVolume* vol = vol_table->getVolume(admin_vol_id);
    fds_verify(vol != NULL);  // admin vol must always exist

    StorHvVolumeLock vol_lock(vol);
    fds_verify(vol->isValidLocked() == true);

    StorHvJournalEntry *txn = vol->journal_tbl->get_journal_entry(transId);
    fds_verify(txn != NULL);

    StorHvJournalEntryLock je_lock(txn);
    fds_verify(txn->isActive() == true);  // Should not receive resp for inactive txn
    fds_verify(txn->trans_state == FDS_TRANS_BUCKET_STATS);

    /*
     * respond to caller with buckets' stats
     */
    fds::AmQosReq   *qosReq  = static_cast<fds::AmQosReq *>(txn->io);
    fds_verify(qosReq != NULL);
    fds::FdsBlobReq *blobReq = qosReq->getBlobReqPtr();
    fds_verify(blobReq != NULL);
    fds_verify(blobReq->getIoType() == FDS_BUCKET_STATS);
    LOGNOTIFY << "Responding to getBucketStats trans " << transId
              << " number of buckets " << (buck_stats->bucket_stats_list).size()
              << " with result " << rx_msg->result;

    /*
     * Mark the IO complete, clean up txn, and callback
     */
    qos_ctrl->markIODone(txn->io);
    txn->reset();
    vol->journal_tbl->releaseTransId(transId);
    
    if (rx_msg->result == FDSP_ERR_OK) {
        BucketStatsContent* contents = NULL;
        int count = (buck_stats->bucket_stats_list).size();

        if (count > 0) {
            contents = new BucketStatsContent[count];
            fds_verify(contents != NULL);
            for (int i = 0; i < count; ++i)
            {
                contents[i].set((buck_stats->bucket_stats_list)[i].vol_name,
                                (buck_stats->bucket_stats_list)[i].rel_prio,
                                (buck_stats->bucket_stats_list)[i].performance,
                                (buck_stats->bucket_stats_list)[i].sla,
                                (buck_stats->bucket_stats_list)[i].limit);
            }
        }

        /* call BucketStats callback directly */
        BucketStatsReq *stats_req = static_cast<BucketStatsReq*>(blobReq);
        stats_req->DoCallback(buck_stats->timestamp, count, contents, ERR_OK, NULL);

    } else {    
        /*
         * We received an error from OM
         */
        blobReq->cbWithResult(-1);
    }
      /*
     * TODO: We're deleting the request structure. This assumes
     * that the caller got everything they needed when the callback
     * was invoked.
     */
    delete blobReq;
}

