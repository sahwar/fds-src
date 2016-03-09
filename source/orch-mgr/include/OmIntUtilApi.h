/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_ORCH_MGR_INCLUDE_OMINTUTILAPI_H_
#define SOURCE_ORCH_MGR_INCLUDE_OMINTUTILAPI_H_
#include <string>
#include <fds_types.h>
#include <kvstore/kvstore.h>
#include <kvstore/configdb.h>

typedef std::vector<fpi::SvcInfo>::iterator SvcInfoIter;

namespace fds 
{
    namespace fpi = FDS_ProtocolInterface;

    /**
     * This is case insensitive
     */
    fds_uint64_t             getUuidFromVolumeName(const std::string& name);
    fds_uint64_t             getUuidFromResourceName(const std::string& name);

    fpi::FDSP_Node_Info_Type fromSvcInfo( const fpi::SvcInfo& svcinfo );
    fpi::FDSP_NodeState      fromServiceStatus(fpi::ServiceStatus svcStatus);

    void                     updateSvcInfoList(std::vector<fpi::SvcInfo>& svcInfos,
                                               bool smFlag, bool dmFlag, bool amFlag);

    SvcInfoIter              isServicePresent(std::vector<fpi::SvcInfo>& svcInfos,
                                              FDS_ProtocolInterface::FDSP_MgrIdType type);

    fpi::SvcInfo             getNewSvcInfo(FDS_ProtocolInterface::FDSP_MgrIdType type);

    void                     getServicesToStart(bool start_sm, bool start_dm,
                                                bool start_am,kvstore::ConfigDB* configDB,
                                                NodeUuid nodeUuid,
                                                std::vector<fpi::SvcInfo>& svcInfoList);

    void                     retrieveSvcId(int64_t pmID, fpi::SvcUuid& svcUuid,
                                           FDS_ProtocolInterface::FDSP_MgrIdType type);

    void                     populateAndRemoveSvc(fpi::SvcUuid serviceTypeId,
                                                  fpi::FDSP_MgrIdType type,
                                                  std::vector<fpi::SvcInfo> svcInfos,
                                                  kvstore::ConfigDB* configDB);

    void                     updateSvcMaps(kvstore::ConfigDB*       configDB,
                                           const fds_uint64_t       svc_uuid,
                                           const fpi::ServiceStatus svc_status,
                                           fpi::FDSP_MgrIdType      svcType,
                                           bool                     handlerUpdate = false,
                                           bool                     registering = false,
                                           fpi::SvcInfo             registeringSvcInfo = fpi::SvcInfo());

    bool                    dbRecordNeedsUpdate(fpi::SvcInfo svcLayerInfo, fpi::SvcInfo dbInfo);
    bool                    areRecordsSame(fpi::SvcInfo svcLayerInfo, fpi::SvcInfo dbInfo );

}  // namespace fds

#endif  // SOURCE_ORCH_MGR_INCLUDE_OMINTUTILAPI_H_
