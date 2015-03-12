/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_DATA_MGR_INCLUDE_DMSVCHANDLER_H_
#define SOURCE_DATA_MGR_INCLUDE_DMSVCHANDLER_H_

#include <fdsp/svc_types_types.h>
#include <net/PlatNetSvcHandler.h>
#include <fdsp/DMSvc.h>
// TODO(Rao): Don't include DataMgr here.  The only reason we include now is
// b/c dmCatReq is subclass in DataMgr and can't be forward declared
#include <DataMgr.h>

namespace FDS_ProtocolInterface {
struct CtrlNotifyDLTUpdate;
}

namespace fds {

class DMSvcHandler : virtual public fpi::DMSvcIf, public PlatNetSvcHandler {
 public:
    explicit DMSvcHandler(CommonModuleProviderIf *provider);

    void startBlobTx(const fpi::AsyncHdr& asyncHdr,
                       const fpi::StartBlobTxMsg& startBlob) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void commitBlobTx(const fpi::AsyncHdr& asyncHdr,
                             const fpi::CommitBlobTxMsg& commitBlbMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void abortBlobTx(const fpi::AsyncHdr& asyncHdr,
                             const fpi::AbortBlobTxMsg& abortBlbMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void volSyncState(const fpi::AsyncHdr& asyncHdr,
                      const fpi::VolSyncStateMsg& syncMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void fwdCatalogUpdateMsg(const fpi::AsyncHdr& AsyncHdr,
                             const fpi::ForwardCatalogMsg& fwdMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void registerStreaming(const fpi::AsyncHdr& asyncHdr,
                           const fpi::StatStreamRegistrationMsg & streamRegstrMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void deregisterStreaming(const fpi::AsyncHdr& asyncHdr,
                           const fpi::StatStreamDeregistrationMsg & streamDeregstrMsg) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void createSnapshot(const fpi::AsyncHdr& asyncHdr,
                         const fpi::CreateSnapshotMsg& createSnap) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void deleteSnapshot(const fpi::AsyncHdr& asyncHdr,
                         const fpi::DeleteSnapshotMsg& createSnap) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void createVolumeClone(const fpi::AsyncHdr& asyncHdr,
                         const fpi::CreateVolumeCloneMsg& createClone) {
        // Don't do anything here. This stub is just to keep cpp compiler happy
    }

    void volSyncState(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                      boost::shared_ptr<fpi::VolSyncStateMsg>& syncMsg);

    void registerStreaming(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                           boost::shared_ptr<fpi::StatStreamRegistrationMsg>& streamRegstrMsg);

    void deregisterStreaming(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                        boost::shared_ptr<fpi::StatStreamDeregistrationMsg>& streamDeregstrMsg);

     // OM - DM snapshot messaging interafce
    void createSnapshot(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                         boost::shared_ptr<fpi::CreateSnapshotMsg>& createSnap);

    void deleteSnapshot(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                         boost::shared_ptr<fpi::DeleteSnapshotMsg>& deleteSnap);

    void createVolumeClone(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                         boost::shared_ptr<fpi::CreateVolumeCloneMsg>& createClone);
    virtual void
    notifySvcChange(boost::shared_ptr<fpi::AsyncHdr>    &hdr,
                    boost::shared_ptr<fpi::NodeSvcInfo> &msg);

    virtual void
    NotifyAddVol(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlNotifyVolAdd> &vol_msg);

    virtual void
    NotifyRmVol(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                boost::shared_ptr<fpi::CtrlNotifyVolRemove> &vol_msg);

    virtual void
    NotifyModVol(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlNotifyVolMod> &vol_msg);
    virtual void
    NotifyDMTClose(boost::shared_ptr<fpi::AsyncHdr>           &hdr,
                   boost::shared_ptr<fpi::CtrlNotifyDMTClose> &msg);

    virtual void
    NotifyDMTUpdate(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                    boost::shared_ptr<fpi::CtrlNotifyDMTUpdate> &msg);

    virtual void
    shutdownDM(boost::shared_ptr<fpi::AsyncHdr>& asyncHdr,
                    boost::shared_ptr<fpi::ShutdownMODMsg>& shutdownMsg);

    virtual void
    NotifyDMTCloseCb(boost::shared_ptr<fpi::AsyncHdr>& hdr,
            boost::shared_ptr<fpi::CtrlNotifyDMTClose>& dmtClose,
            Error &err);

    virtual void
    NotifyDMAbortMigration(boost::shared_ptr<fpi::AsyncHdr>& hdr,
            boost::shared_ptr<fpi::CtrlNotifyDMAbortMigration>& abortMsg);
    virtual void
    NotifyDMTUpdateCb(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                      const Error &err);
    virtual void
    NotifyDLTUpdate(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                              boost::shared_ptr<fpi::CtrlNotifyDLTUpdate> &dlt);

    void
    NotifyDLTUpdateCb(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                      boost::shared_ptr<fpi::CtrlNotifyDLTUpdate> &dlt,
                      const Error                                 &err);

    virtual void
    StartDMMetaMigration(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                         boost::shared_ptr<fpi::CtrlDMMigrateMeta>   &migrMsg);

    void
    StartDMMetaMigrationCb(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                           const Error &err);
};

}  // namespace fds

#endif  // SOURCE_DATA_MGR_INCLUDE_DMSVCHANDLER_H_
