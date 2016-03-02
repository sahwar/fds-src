/*
 * Copyright 2014-2016 Formation Data Systems, Inc.
 */
#ifndef SOURCE_ACCESS_MGR_INCLUDE_AMDISPATCHER_H_
#define SOURCE_ACCESS_MGR_INCLUDE_AMDISPATCHER_H_

#include <deque>
#include <mutex>
#include <string>
#include <tuple>
#include "AmDataProvider.h"
#include <net/SvcRequest.h>
#include "concurrency/RwLock.h"

/* Forward declarations */
namespace FDS_ProtocolInterface {
struct OpenVolumeRspMsg;
}

namespace fds {

struct AbortBlobTxReq;
struct AttachVolumeReq;
struct CommitBlobTxReq;
struct DeleteBlobReq;
struct ErrorHandler;
struct GetVolumeMetadataReq;
struct GetBlobReq;
struct DetachVolumeReq;
struct RenameBlobReq;
struct PutObjectReq;
struct SetBlobMetaDataReq;
struct SetVolumeMetadataReq;
struct StatBlobReq;
struct StatVolumeReq;
struct StartBlobTxReq;
struct UpdateCatalogReq;
struct VolumeContentsReq;
struct VolumeGroupHandle;

class MockSvcHandler;
struct DLT;

struct VolumeDispatchTable {
    using entry_type = std::pair<std::mutex, fds_uint64_t>;
    using key_type = fds_volid_t;
    using map_type = std::unordered_map<key_type, entry_type>;

    VolumeDispatchTable() = default;
    VolumeDispatchTable(VolumeDispatchTable const& rhs) = delete;
    VolumeDispatchTable& operator=(VolumeDispatchTable const& rhs) = delete;
    ~VolumeDispatchTable() = default;

    /**
     * Set the sequence id of a request to the next value and return a unique
     * lock preventing any other request that would update the volume from
     * being dispatched until this has entered SvcLayer
     */
    std::unique_lock<std::mutex> getAndLockVolumeSequence(fds_volid_t const vol_id, int64_t& seq_id) {
        SCOPEDREAD(table_lock);
        auto it = lock_table.find(vol_id);
        fds_assert(lock_table.end() != it);
        std::unique_lock<std::mutex> g(it->second.first);
        seq_id = ++it->second.second;
        return g;
    }

    /**
     * Either register a new sequence id for dispatching updates or update an
     * existing id if newer.
     */
    void registerVolumeSequence(fds_volid_t const vol_id, fds_uint64_t const seq_id) {
        SCOPEDWRITE(table_lock);
        auto it = lock_table.find(vol_id);
        if (lock_table.end() != it) {
            auto& curr_seq_id = it->second.second;
            curr_seq_id = std::max(curr_seq_id, seq_id);
        } else {
            lock_table[vol_id].second = seq_id;
        }
    }

 private:
    map_type lock_table;
    fds_rwlock table_lock;
};

/**
 * AM FDSP request dispatcher and reciever. The dispatcher
 * does the work to send and receive AM network messages over
 * the service layer.
 */
struct AmDispatcher : public AmDataProvider
{
    using shared_str = boost::shared_ptr<std::string>;
    /**
     * The dispatcher takes a shared ptr to the DMT manager
     * which it uses when deciding who to dispatch to. The
     * DMT manager is still owned and updated to omClient.
     * TODO(Andrew): Make the dispatcher own this piece or
     * iterface with platform lib.
     */
    explicit AmDispatcher(AmDataProvider* prev);
    AmDispatcher(AmDispatcher const&)               = delete;
    AmDispatcher& operator=(AmDispatcher const&)    = delete;
    AmDispatcher(AmDispatcher &&)                   = delete;
    AmDispatcher& operator=(AmDispatcher &&)        = delete;
    ~AmDispatcher() override;

    /**
     * These are the Volume specific DataProvider routines.
     * Everything else is pass-thru.
     */
    void start() override;
    bool done() override;
    void stop() override;
    void removeVolume(VolumeDesc const& volDesc) override;
    void openVolume(AmRequest * amReq) override;
    void closeVolume(AmRequest * amReq) override;
    Error modifyVolumePolicy(const VolumeDesc& vdesc) override;
    void addToVolumeGroup(const fpi::AddToVolumeGroupCtrlMsgPtr &addMsg,
                               const AddToVolumeGroupCb &cb) override;
    void statVolume(AmRequest * amReq) override;
    void setVolumeMetadata(AmRequest * amReq) override;
    void getVolumeMetadata(AmRequest * amReq) override;
    void abortBlobTx(AmRequest * amReq) override;
    void startBlobTx(AmRequest * amReq) override;
    void commitBlobTx(AmRequest * amReq) override;
    void putObject(AmRequest * amReq) override;
    void getObject(AmRequest * amReq) override;
    void deleteBlob(AmRequest * amReq) override;
    void getOffsets(AmRequest * amReq) override;
    void statBlob(AmRequest * amReq) override;
    void renameBlob(AmRequest * amReq) override;
    void setBlobMetadata(AmRequest * amReq) override;
    void updateCatalog(AmRequest * amReq) override;
    void volumeContents(AmRequest * amReq) override;

  private:

    /**
     * FEATURE TOGGLE: VolumeGrouping Support
     * Wed 19 Aug 2015 10:56:46 AM MDT
     */
    bool volume_grouping_support { false };

    /**
     * Shared ptrs to the DLT and DMT managers used
     * for deciding who to dispatch to.
     */
    boost::shared_ptr<DLTManager> dltMgr;
    boost::shared_ptr<DMTManager> dmtMgr;

    mutable VolumeDispatchTable dispatchTable;

    using dmt_ver_count_type = std::tuple<fds_uint64_t, size_t, std::deque<StartBlobTxReq*>>;
    using blob_id_type = std::pair<fds_volid_t, std::string>;
    using tx_map_barrier_type = std::map<blob_id_type, dmt_ver_count_type>;

    std::mutex tx_map_lock;
    tx_map_barrier_type tx_map_barrier;

    template<typename CbMeth, typename MsgPtr, typename ReqPtr>
    void readFromDM(ReqPtr request, MsgPtr message, CbMeth cb_func, uint32_t const timeout=0);

    template<typename CbMeth, typename MsgPtr, typename ReqPtr>
    void writeToDM(ReqPtr request, MsgPtr message, CbMeth cb_func, uint32_t const timeout=0);

    template<typename CbMeth, typename MsgPtr, typename ReqPtr>
    void readFromSM(ReqPtr request, MsgPtr payload, CbMeth cb_func, uint32_t const timeout=0);

    template<typename CbMeth, typename MsgPtr, typename ReqPtr>
    void writeToSM(ReqPtr request, MsgPtr payload, CbMeth cb_func, uint32_t const timeout=0);

    /**
     * FEATURE TOGGLE: Volume grouping support
     * Thu Jan 14 10:47:10 2016
     */
    template<typename CbMeth, typename MsgPtr, typename ReqPtr>
    void volumeGroupRead(ReqPtr request, MsgPtr message, CbMeth cb_func);

    template<typename CbMeth, typename MsgPtr, typename ReqPtr>
    void volumeGroupModify(ReqPtr request, MsgPtr message, CbMeth cb_func);

    template<typename CbMeth, typename MsgPtr, typename ReqPtr>
    void volumeGroupCommit(ReqPtr request, MsgPtr message, CbMeth cb_func);

    std::unique_ptr<ErrorHandler> volumegroup_handler;
    fds_rwlock volumegroup_lock;
    std::unordered_map<fds_volid_t, std::unique_ptr<VolumeGroupHandle>> volumegroup_map;
    void _abortBlobTxCb(AbortBlobTxReq *amReq, const Error& error, shared_str payload);
    void _commitBlobTxCb(CommitBlobTxReq* amReq, const Error& error, shared_str payload);
    void _closeVolumeCb(DetachVolumeReq* amReq);
    void _deleteBlobCb(DeleteBlobReq *amReq, const Error& error, shared_str payload);
    void _getQueryCatalogCb(GetBlobReq* amReq, const Error& error, shared_str payload);
    void _getVolumeMetadataCb(GetVolumeMetadataReq* amReq, const Error& error, shared_str payload);
    void _openVolumeCb(AttachVolumeReq* amReq, const Error& error, boost::shared_ptr<FDS_ProtocolInterface::OpenVolumeRspMsg> const& msg);
    void _putBlobTxCb(UpdateCatalogReq* amReq, const Error& error, shared_str payload);
    void _renameBlobCb(RenameBlobReq *amReq, const Error& error, shared_str payload);
    void _setBlobMetadataCb(SetBlobMetaDataReq *amReq, const Error& error, shared_str payload);
    void _setVolumeMetadataCb(SetVolumeMetadataReq* amReq, const Error& error, shared_str payload);
    void _startBlobTxCb(StartBlobTxReq* amReq, const Error& error, shared_str payload);
    void _statVolumeCb(StatVolumeReq* amReq, const Error& error, shared_str payload);
    void _statBlobCb(StatBlobReq *amReq, const Error& error, shared_str payload);
    void _updateCatalogCb(UpdateCatalogReq* amReq, const Error& error, shared_str payload);
    void _volumeContentsCb(VolumeContentsReq *amReq, const Error& error, shared_str payload);

    void putBlobTx(AmRequest * amReq);
    void _startBlobTx(AmRequest *amReq);

    /**
     * FEATURE TOGGLE: SvcLayer Direct callbacks
     * Thu Jan 14 10:47:25 2016
     */
    void abortBlobTxCb(AbortBlobTxReq *amReq,
                       MultiPrimarySvcRequest* svcReq,
                       const Error& error,
                       shared_str payload);

    void closeVolumeCb(DetachVolumeReq* amReq,
                       MultiPrimarySvcRequest* svcReq,
                       const Error& error,
                       shared_str payload);

    void commitBlobTxCb(CommitBlobTxReq* amReq,
                        MultiPrimarySvcRequest* svcReq,
                        const Error& error,
                        shared_str payload);

    void deleteBlobCb(DeleteBlobReq *amReq,
                      MultiPrimarySvcRequest* svcReq,
                      const Error& error,
                      shared_str payload)
    { _deleteBlobCb(amReq, error, payload); }

    void putObjectCb(PutObjectReq* amReq,
                     QuorumSvcRequest* svcReq,
                     const Error& error,
                     shared_str payload);

    void getObjectCb(AmRequest* amReq,
                     FailoverSvcRequest* svcReq,
                     const Error& error,
                     shared_str payload);

    void getQueryCatalogCb(GetBlobReq* amReq,
                           FailoverSvcRequest* svcReq,
                           const Error& error,
                           shared_str payload);

    void getVolumeMetadataCb(GetVolumeMetadataReq* amReq,
                             FailoverSvcRequest* svcReq,
                             const Error& error,
                             shared_str payload);

    void openVolumeCb(AttachVolumeReq* amReq,
                      MultiPrimarySvcRequest* svcReq,
                      const Error& error,
                      shared_str payload);

    void updateCatalogCb(UpdateCatalogReq* amReq,
                         MultiPrimarySvcRequest* svcReq,
                         const Error& error,
                         shared_str payload);

    void putBlobTxCb(UpdateCatalogReq* amReq,
                     MultiPrimarySvcRequest* svcReq,
                     const Error& error,
                     shared_str payload)
    { _putBlobTxCb(amReq, error, payload); }

    void renameBlobCb(RenameBlobReq *amReq,
                      MultiPrimarySvcRequest* svcReq,
                      const Error& error,
                      shared_str payload);

    void setBlobMetadataCb(SetBlobMetaDataReq *amReq,
                           MultiPrimarySvcRequest* svcReq,
                           const Error& error,
                           shared_str payload)
    { _setBlobMetadataCb(amReq, error, payload); }


    void setVolumeMetadataCb(SetVolumeMetadataReq* amReq,
                             MultiPrimarySvcRequest* svcReq,
                             const Error& error,
                             shared_str payload);

    void startBlobTxCb(StartBlobTxReq* amReq,
                       MultiPrimarySvcRequest* svcReq,
                       const Error& error,
                       shared_str payload);

    void statBlobCb(StatBlobReq *amReq,
                    FailoverSvcRequest* svcReq,
                    const Error& error,
                    shared_str payload);

    void statVolumeCb(StatVolumeReq* amReq,
                      FailoverSvcRequest* svcReq,
                      const Error& error,
                      shared_str payload);

    void volumeContentsCb(VolumeContentsReq *amReq,
                          FailoverSvcRequest* svcReq,
                          const Error& error,
                          shared_str payload);

    fds_bool_t missingBlobStatusCb(AmRequest* amReq,
                                   const Error& error,
                                   shared_str payload);

    /**
     * Configurable timeouts and defaults (ms)
     */
    uint32_t message_timeout_default { 0 };
    uint32_t message_timeout_io { 0 };
    uint32_t message_timeout_open { 0 };

    /**
     * Number of primary replicas (right now DM/SM are identical)
     */
    uint32_t numPrimaries;

    boost::shared_ptr<MockSvcHandler> mockHandler_;
    uint64_t mockTimeoutUs_  = 200;

    /**
     * Drains any pending start tx requests into the svc layer with updated
     * dmt version
     */
    void releaseTx(blob_id_type const& blob_id);

    /**
     * Sets the configured request serialization.
     */
    void setSerialization(AmRequest* amReq, boost::shared_ptr<SvcRequestIf> svcReq);
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_AMDISPATCHER_H_
