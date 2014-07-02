/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef __StorHvisorNet_h__
#define __StorHvisorNet_h__

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <arpa/inet.h>


#include <list>
#include "StorHvDataPlace.h"
#include <fds_error.h>
#include <fds_types.h>

/* TODO: Use API header in include directory. */
#include "ubd.h"
#include <lib/OMgrClient.h>
#include "StorHvVolumes.h"
#include "StorHvisorCPP.h" 
#include "qos_ctrl.h" 
#include "fds_qos.h" 
#include "StorHvQosCtrl.h" 
#include <hash/md5.h>
#include <FdsRandom.h>
#include "./handler/handler.h"
#include <fdsp/FDSP_DataPathReq.h>
#include <fdsp/FDSP_DataPathResp.h>
#include <fdsp/FDSP_MetaDataPathReq.h>
#include <fdsp/FDSP_MetaDataPathResp.h>
#include <fdsp/FDSP_ControlPathReq.h>
#include <fdsp/FDSP_ControlPathResp.h>
#include <fdsp/FDSP_ConfigPathReq.h>
#include <net/RpcRequest.h>


#include "NetSession.h"

#include <map>
// #include "util/concurrency/Thread.h"
#include <concurrency/Synchronization.h>
#include <fds_counters.h>


#undef  FDS_TEST_SH_NOOP              /* IO returns (filled with 0s for read) as soon as SH receives it from ubd */
#undef FDS_TEST_SH_NOOP_DISPATCH     /* IO returns (filled with 0s for read) as soon as dispatcher takes it from the queue */



#define FDS_IO_LONG_TIME 60 // seconds
#define FDS_REPLICATION_FACTOR          1
#define MAX_DM_NODES                    4

#define  FDS_NODE_OFFLINE               0
#define  FDS_NODE_ONLINE                1

#define  FDS_STORAGE_TYPE_SSD           1
#define  FDS_STORAGE_TYPE_FLASH         2
#define  FDS_STORAGE_TYPE_HARD          3
#define  FDS_STORAGE_TYPE_TAPE          4


#define  FDS_NODE_TYPE_PRIM             1
#define  FDS_NODE_TYPE_SEND             2
#define  FDS_NODE_TYPE_BCKP             3


#define  FDS_SUCCESS                    0
#define  FDS_FAILURE                    1

#define  FDS_TIMER_TIMEOUT              1

#define HVISOR_SECTOR_SIZE 		512

typedef unsigned int volid_t;

using namespace FDS_ProtocolInterface;
using namespace std;
using namespace fds;

class FDSP_DataPathRespCbackI : public FDS_ProtocolInterface::FDSP_DataPathRespIf
{
public:
    FDSP_DataPathRespCbackI() {
    }
    ~FDSP_DataPathRespCbackI() {
    }

    virtual void GetObjectResp(const FDSP_MsgHdrType& fdsp_msg, const FDSP_GetObjType& get_obj_req) {
    }
    virtual void GetObjectResp(boost::shared_ptr<FDSP_MsgHdrType>& fdsp_msg, boost::shared_ptr<FDSP_GetObjType>& get_obj_req);
    virtual void PutObjectResp(const FDSP_MsgHdrType& fdsp_msg, const FDSP_PutObjType& put_obj_req) {
    }
    virtual void PutObjectResp(boost::shared_ptr<FDSP_MsgHdrType>& fdsp_msg, boost::shared_ptr<FDSP_PutObjType>& put_obj_req);
    virtual void DeleteObjectResp(const FDSP_MsgHdrType& fdsp_msg, const FDSP_DeleteObjType& del_obj_req) {
    }
    virtual void DeleteObjectResp(boost::shared_ptr<FDSP_MsgHdrType>& fdsp_msg, boost::shared_ptr<FDSP_DeleteObjType>& del_obj_req);
    virtual void OffsetWriteObjectResp(const FDSP_MsgHdrType& fdsp_msg, const FDSP_OffsetWriteObjType& offset_write_obj_req) {
    }
    virtual void OffsetWriteObjectResp(boost::shared_ptr<FDSP_MsgHdrType>& fdsp_msg, boost::shared_ptr<FDSP_OffsetWriteObjType>& offset_write_obj_req) {
    }
    virtual void RedirReadObjectResp(const FDSP_MsgHdrType& fdsp_msg, const FDSP_RedirReadObjType& redir_write_obj_req) {
    }
    virtual void RedirReadObjectResp(boost::shared_ptr<FDSP_MsgHdrType>& fdsp_msg, boost::shared_ptr<FDSP_RedirReadObjType>& redir_write_obj_req){
    }
    virtual void GetObjectMetadataResp(
            boost::shared_ptr<FDSP_GetObjMetadataResp>& metadata_resp) {
    }
    virtual void GetObjectMetadataResp(const FDSP_GetObjMetadataResp& metadata_resp) {
    }

};


class FDSP_MetaDataPathRespCbackI : public FDS_ProtocolInterface::FDSP_MetaDataPathRespIf
{
public:
    FDSP_MetaDataPathRespCbackI() {
    }

    ~FDSP_MetaDataPathRespCbackI() {
    }

    virtual void StartBlobTxResp(const FDSP_MsgHdrType &msgHdr) {
    }
    virtual void StartBlobTxResp(boost::shared_ptr<FDSP_MsgHdrType> &msgHdr);
    virtual void UpdateCatalogObjectResp(const FDSP_MsgHdrType& fdsp_msg, const FDSP_UpdateCatalogType& cat_obj_req) {
    }
    virtual void UpdateCatalogObjectResp(boost::shared_ptr<FDSP_MsgHdrType>& fdsp_msg, boost::shared_ptr<FDSP_UpdateCatalogType>& cat_obj_req);
    virtual void QueryCatalogObjectResp(const FDSP_MsgHdrType& fdsp_msg, const FDSP_QueryCatalogType& cat_obj_req) {
    }
    virtual void QueryCatalogObjectResp(boost::shared_ptr<FDSP_MsgHdrType>& fdsp_msg, boost::shared_ptr<FDSP_QueryCatalogType>& cat_obj_req);
    virtual void DeleteCatalogObjectResp(const FDSP_MsgHdrType& fdsp_msg, const FDSP_DeleteCatalogType& cat_obj_req) {
    }
    virtual void DeleteCatalogObjectResp(boost::shared_ptr<FDSP_MsgHdrType>& fdsp_msg, boost::shared_ptr<FDSP_DeleteCatalogType>& cat_obj_req);
    virtual void GetVolumeBlobListResp(const FDSP_MsgHdrType& fds_msg, const FDSP_GetVolumeBlobListRespType& blob_list_rsp) {
    }
    virtual void GetVolumeBlobListResp(boost::shared_ptr<FDSP_MsgHdrType>& fds_msg, boost::shared_ptr<FDSP_GetVolumeBlobListRespType>& blob_list_rsp);

    virtual void StatBlobResp(const FDSP_MsgHdrType &msgHdr,
                              const FDS_ProtocolInterface::BlobDescriptor &blobDesc) {
    }

    virtual void StatBlobResp(boost::shared_ptr<FDSP_MsgHdrType> &msgHdr,
                              boost::shared_ptr<FDS_ProtocolInterface::
                              BlobDescriptor> &blobDesc);

    virtual void SetBlobMetaDataResp(const FDSP_MsgHdrType& header, const std::string& blobName) {}
    virtual void GetBlobMetaDataResp(const FDSP_MsgHdrType& header, const std::string& blobName, const FDSP_MetaDataList& metaDataList) {}
    virtual void GetVolumeMetaDataResp(const FDSP_MsgHdrType& header, const FDSP_VolumeMetaData& volumeMeta) {}

    virtual void SetBlobMetaDataResp(boost::shared_ptr<FDSP_MsgHdrType>& header, boost::shared_ptr<std::string>& blobName);
    virtual void GetBlobMetaDataResp(boost::shared_ptr<FDSP_MsgHdrType>& header, boost::shared_ptr<std::string>& blobName, boost::shared_ptr<FDSP_MetaDataList>& metaDataList);
    virtual void GetVolumeMetaDataResp(boost::shared_ptr<FDSP_MsgHdrType>& header, boost::shared_ptr<FDSP_VolumeMetaData>& volumeMeta);
};


typedef struct {
    double   hash_high;
    double   hash_low;
    char    conflict_id;
} fds_object_id_t;


typedef union {

    fds_object_id_t obj_id; // object id fields
    unsigned char bytes[20]; // byte array

} fds_doid_t;

typedef unsigned char doid_t[20];

/**
 * @brief Storage manager counters
 */
class AMCounters : public FdsCounters
{
 public:
    AMCounters(const std::string &id, FdsCountersMgr *mgr)
    : FdsCounters(id, mgr),
      put_reqs("put_reqs", this),
      get_reqs("get_reqs", this),
      puts_latency("puts_latency", this),
      gets_latency("gets_latency", this)
    {
    }

    NumericCounter put_reqs;
    NumericCounter get_reqs;
    LatencyCounter puts_latency;
    LatencyCounter gets_latency;
};
/*************************************************************************** */

class StorHvCtrl : public HasLogger {
public:
    /*
     * Defines specific test modes used to
     * construct the object.
     */
    typedef enum {
        DATA_MGR_TEST, /* Only communicate with DMs */
        STOR_MGR_TEST, /* Only communicate with SMs */
        TEST_BOTH,     /* Communication with DMs and SMs */
        NORMAL,        /* Normal, non-test mode */
        MAX
    } sh_comm_modes;
  
    StorHvCtrl(int argc, char *argv[], SysParams *params);
    StorHvCtrl(int argc, char *argv[], SysParams *params,
               sh_comm_modes _mode);
    StorHvCtrl(int argc,
               char *argv[],
               SysParams *params,
               sh_comm_modes _mode,
               fds_uint32_t sm_port_num,
               fds_uint32_t dm_port_num);
    ~StorHvCtrl();
    hv_create_blkdev cr_blkdev;
    hv_delete_blkdev del_blkdev;
    void initHandlers();
    GetVolumeMetaDataHandler *handlerGetVolumeMetaData;
    //imcremental checksum  for header and payload 
    checksum_calc   *chksumPtr;
  
    // Data Members
    StorHvDataPlacement        *dataPlacementTbl;
    boost::shared_ptr<netSessionTbl> rpcSessionTbl; // RPC calls Switch Table
    StorHvVolumeTable          *vol_table;  
    fds::StorHvQosCtrl             *qos_ctrl; // Qos Controller object
    OMgrClient                 *om_client;
    FDS_ProtocolInterface::FDSP_AnnounceDiskCapabilityPtr dInfo;

    std::string                 myIp;
    std::string                 my_node_name;

    RandNumGenerator::ptr randNumGen;

    Error sendTestBucketToOM(const std::string& bucket_name,
                             const std::string& access_key_id = "",
                             const std::string& secret_access_key = "");

    void initVolInfo(FDSP_VolumeInfoTypePtr vol_info,
                     const std::string& bucket_name);

    Error startBlobTx(AmQosReq *qosReq);
    void attachVolume(AmQosReq *qosReq);
    fds::Error pushBlobReq(FdsBlobReq *blobReq);
    fds::Error putBlob(AmQosReq *qosReq);
    fds::Error getBlob(AmQosReq *qosReq);
    fds::Error deleteBlob(AmQosReq *qosReq);
    Error StatBlob(AmQosReq *qosReq);
    Error SetBlobMetaData(AmQosReq *qosReq);

    // Stuff for pending offset operations
    // TODO(Andrew): Reconcile with dispatchSm...
    fds::Error processSmPutObj(PutBlobReq *putBlobReq,
                               StorHvJournalEntry *journEntry);
    fds::Error processDmUpdateBlob(PutBlobReq *putBlobReq,
                                   StorHvJournalEntry *journEntry);
    fds::Error resumePutBlob(StorHvJournalEntry *journEntry);
    fds::Error resumeGetBlob(StorHvJournalEntry *journEntry);
    fds::Error resumeDeleteBlob(StorHvJournalEntry *journEntry);

    fds::Error listBucket(AmQosReq *qosReq);
    fds::Error getBucketStats(AmQosReq *qosReq);
    fds::Error putObjResp(const FDSP_MsgHdrTypePtr& rxMsg,
                          const FDSP_PutObjTypePtr& putObjRsp);
    fds::Error upCatResp(const FDSP_MsgHdrTypePtr& rxMsg, 
                         const FDSP_UpdateCatalogTypePtr& catObjRsp);
    void statBlobResp(const FDSP_MsgHdrTypePtr rxMsg, 
                      const FDS_ProtocolInterface::
                      BlobDescriptorPtr blobDesc);
    void startBlobTxResp(const FDSP_MsgHdrTypePtr rxMsg);
    fds::Error deleteCatResp(const FDSP_MsgHdrTypePtr& rxMsg,
                             const FDSP_DeleteCatalogTypePtr& delCatRsp);
    fds::Error getObjResp(const FDSP_MsgHdrTypePtr& rxMsg,
                          const FDSP_GetObjTypePtr& getObjRsp);
    fds::Error deleteObjResp(const FDSP_MsgHdrTypePtr& rxMsg,
                             const FDSP_DeleteObjTypePtr& cat_obj_req);
    fds::Error getBucketResp(const FDSP_MsgHdrTypePtr& rxMsg,
                             const FDSP_GetVolumeBlobListRespTypePtr& blobListResp);

    static void bucketStatsRespHandler(const FDSP_MsgHdrTypePtr& rx_msg,
                                       const FDSP_BucketStatsRespTypePtr& buck_stats);
    void getBucketStatsResp(const FDSP_MsgHdrTypePtr& rx_msg,
                            const FDSP_BucketStatsRespTypePtr& buck_stats);

    void handleSetBlobMetaDataResp(const FDSP_MsgHdrTypePtr rxMsg);

    void InitDmMsgHdr(const FDSP_MsgHdrTypePtr &msg_hdr);
    void InitSmMsgHdr(const FDSP_MsgHdrTypePtr &msg_hdr);
    void fbd_process_req_timeout(unsigned long arg);

    int fds_move_wr_req_state_machine(const FDSP_MsgHdrTypePtr& rx_msg);  
    int fds_move_del_req_state_machine(const FDSP_MsgHdrTypePtr& rx_msg);
    SysParams* getSysParams();
    void StartOmClient();
    sh_comm_modes GetRunTimeMode() { return mode; }
    boost::shared_ptr<FDSP_DataPathRespCbackI> dPathRespCback;
    boost::shared_ptr<FDSP_MetaDataPathRespCbackI> mPathRespCback;
    Error dispatchSmPutMsg(StorHvJournalEntry *journEntry, const NodeUuid &send_uuid);
    Error dispatchSmGetMsg(StorHvJournalEntry *journEntry);
    Error dispatchDmUpdMsg(StorHvJournalEntry *journEntry,
                           const NodeUuid &send_uuid);
    friend class FDSP_MetaDataPathRespCbackI;

    struct TxnResponseHelper {
        StorHvCtrl* storHvisor = NULL;
        fds_uint32_t txnId = 0xFFFFFFFF;
        fds_volid_t  volId = 0;
        StorHvJournalEntry *txn = NULL;
        StorHvVolumeLock *vol_lock = NULL;
        StorHvJournalEntryLock *je_lock = NULL;
        StorHvVolume* vol = NULL;
        fds::FdsBlobReq* blobReq = NULL;

        TxnResponseHelper(StorHvCtrl* storHvisor, fds_volid_t  volId, fds_uint32_t txnId);
        void setStatus(FDSN_Status  status);
        ~TxnResponseHelper();
    };

    struct TxnRequestHelper {
        StorHvCtrl* storHvisor = NULL;
        fds_uint32_t txnId = 0;
        fds_volid_t  volId = 0;
        FDSN_Status  status = FDSN_StatusNOTSET;
        StorHvVolume *shVol = NULL;
        StorHvJournalEntry *txn = NULL;
        StorHvJournalEntryLock *jeLock = NULL;
        StorHvVolume* vol = NULL;
        AmQosReq *qosReq;
        fds::FdsBlobReq* blobReq = NULL;

        TxnRequestHelper(StorHvCtrl* storHvisor, AmQosReq *qosReq);

        bool getPrimaryDM(fds_uint32_t& ip, fds_uint32_t& port);
        bool isValidVolume();
        bool setupTxn();
        bool hasError();
        void setStatus(FDSN_Status  status);
        void scheduleTimer();
        ~TxnRequestHelper();
    };

    struct BlobRequestHelper {
        StorHvCtrl* storHvisor = NULL;
        fds_volid_t volId = invalid_vol_id;
        FdsBlobReq *blobReq = NULL;
        const std::string& volumeName;

        explicit BlobRequestHelper(StorHvCtrl* storHvisor, const std::string& volumeName);
        void setupVolumeInfo();
        fds::Error processRequest();
    };

    fds::Error getBlobSvc(fds::AmQosReq *qosReq);
    fds::Error putBlobSvc(fds::AmQosReq *qosReq);
    fds::Error startBlobTxSvc(AmQosReq *qosReq);

    void issueQueryCatalog(const std::string& blobName,
                           const fds_uint64_t& blobOffset,
                           const fds_volid_t& volId,
                           FailoverRpcRespCb respCb);
    void issueGetObject(const fds_volid_t& volId, const ObjectID& objId,
                        FailoverRpcRespCb respCb);
    void issueStartBlobTxMsg(const std::string& blobName,
                             const fds_volid_t& volId,
                             const fds_uint64_t& txId,
                             QuorumRpcRespCb respCb);
    void issuePutObjectMsg(const ObjectID &objId,
                           const char* dataBuf,
                           const fds_uint64_t &len,
                           const fds_volid_t& volId,
                           QuorumRpcRespCb respCb);
    void issueUpdateCatalogMsg(const ObjectID &objId,
                               const std::string& blobName,
                               const fds_uint64_t& blobOffset,
                               const fds_uint64_t &len,
                               const bool &lastBuf,
                               const fds_volid_t& volId,
                               QuorumRpcRespCb respCb);


    void getBlobQueryCatalogResp(fds::AmQosReq* qosReq,
                                 FailoverRpcRequest* rpcReq,
                                 const Error& error,
                                 boost::shared_ptr<std::string> payload);
    void getBlobGetObjectResp(fds::AmQosReq* qosReq,
                              FailoverRpcRequest* rpcReq,
                              const Error& error,
                              boost::shared_ptr<std::string> payload);
    void startBlobTxMsgResp(fds::AmQosReq* qosReq,
                            QuorumRpcRequest* rpcReq,
                            const Error& error,
                            boost::shared_ptr<std::string> payload);
    void putBlobUpdateCatalogMsgResp(fds::AmQosReq* qosReq,
                                     QuorumRpcRequest* rpcReq,
                                     const Error& error,
                                     boost::shared_ptr<std::string> payload);

    void putBlobPutObjectMsgResp(fds::AmQosReq* qosReq,
                                 QuorumRpcRequest* rpcReq,
                                 const Error& error,
                                 boost::shared_ptr<std::string> payload);

    fds::Error updateCatalogCache(GetBlobReq *blobReq,
                                  FDS_ProtocolInterface::FDSP_BlobObjectList& blobOffList);
    inline AMCounters& getCounters()
    {
        return counters_;
    }
private:
    void handleDltMismatch(StorHvVolume *vol,
                           StorHvJournalEntry *journEntry);
    void procNewDlt(fds_uint64_t newDltVer);


    SysParams *sysParams;
    sh_comm_modes mode;

    /// Toggles the local volume catalog cache
    fds_bool_t disableVcc;
    /** Counters */
    AMCounters counters_;
};

extern StorHvCtrl *storHvisor;

/*
 * Static function for process IO via a threadpool
 */
static void processBlobReq(AmQosReq *qosReq) {
    fds_verify(qosReq->io_module == FDS_IOType::STOR_HV_IO);
    fds_verify(qosReq->magicInUse() == true);

    fds::Error err(ERR_OK);
    switch (qosReq->io_type) {
        case fds::FDS_START_BLOB_TX:
            err = storHvisor->startBlobTx(qosReq);
            break;

        case fds::FDS_ATTACH_VOL:
            storHvisor->attachVolume(qosReq);
            break;

        case fds::FDS_IO_READ:
        case fds::FDS_GET_BLOB:
            err = storHvisor->getBlob(qosReq);
            break;

        case fds::FDS_IO_WRITE:
        case fds::FDS_PUT_BLOB:
            err = storHvisor->putBlob(qosReq);
            break;

        case fds::FDS_DELETE_BLOB:
            err = storHvisor->deleteBlob(qosReq);
            break;

        case fds::FDS_STAT_BLOB:
            err = storHvisor->StatBlob(qosReq);
            break;

        case fds::FDS_LIST_BUCKET:
            err = storHvisor->listBucket(qosReq);
            break;

        case fds::FDS_BUCKET_STATS:
            err = storHvisor->getBucketStats(qosReq);
            break;

        case fds::FDS_SET_BLOB_METADATA:
            err = storHvisor->SetBlobMetaData(qosReq);
            break;

        case fds::FDS_GET_VOLUME_METADATA:
            err = storHvisor->handlerGetVolumeMetaData->handleQueueItem(qosReq);
            break;

        default :
            break;
    }

    fds_verify((err == ERR_OK) || (err == ERR_NOT_IMPLEMENTED));
}

#endif  // SOURCE_STOR_HVISOR_STORHVISORNET_H_
