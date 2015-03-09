

/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#ifndef SOURCE_ORCH_MGR_INCLUDE_SNAPSHOT_SVCHANDLER_H_
#define SOURCE_ORCH_MGR_INCLUDE_SNAPSHOT_SVCHANDLER_H_

#include <string>
#include <fds_error.h>
#include <fds_types.h>
#include <fds_typedefs.h>
#include <fdsp/common_types.h>
#include <net/SvcRequestPool.h>

namespace FDS_ProtocolInterface {
struct CreateSnapshotMsg;
struct CreateVolumeCloneMsg;
using CreateSnapshotMsgPtr = boost::shared_ptr<CreateSnapshotMsg>;
using CreateVolumeCloneMsgPtr = boost::shared_ptr<CreateVolumeCloneMsg>;
}

namespace fds {
class OrchMgr;
namespace snapshot {

class OmSnapshotSvcHandler {
  public:
    explicit OmSnapshotSvcHandler(OrchMgr* om);
    Error omSnapshotCreate(const fpi::Snapshot& snapshot);
    void omSnapshotCreateResp(fpi::CreateSnapshotMsgPtr request,
                              QuorumSvcRequest* svcReq,
                              const Error& error,
                              boost::shared_ptr<std::string> payload);
    Error omSnapshotDelete(fds_uint64_t snapshotId, fds_uint64_t volId);
    void omSnapshotDeleteResp(QuorumSvcRequest* svcReq,
                              const Error& error,
                              boost::shared_ptr<std::string> payload);
    Error omVolumeCloneCreate(const fpi::CreateVolumeCloneMsgPtr &stVolumeCloneTxMsg);
    void  omVolumeCloneCreateResp(QuorumSvcRequest* svcReq,
                                  const Error& error,
                                  boost::shared_ptr<std::string> payload);
  protected:
    OrchMgr* om;
};
}  // namespace snapshot
}  // namespace fds
#endif  // SOURCE_ORCH_MGR_INCLUDE_SNAPSHOT_SVCHANDLER_H_
