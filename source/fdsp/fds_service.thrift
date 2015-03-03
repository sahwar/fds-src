/*
 * Copyright 2014 by Formation Data Systems, Inc.
 *
 * NOTE (bszmyd): 02/23/2015
 * This file contains some of the common message structs that all
 * the services support, probably because of their dependence on
 * platform.
 *
 * Put any new service specific types in the respective thrift file.
 * Message ids have to go here for now since all SvcRequests use the
 * enum below.
 */

include "FDSP.thrift"
include "common.thrift"

namespace cpp FDS_ProtocolInterface
namespace java com.formationds.protocol

/*
 * List of all FDSP message types that passed between fds services.  Typically all these
 * types are for async messages.
 *
 * Process for adding new type:
 * 1. DON'T CHANGE THE ORDER.  ONLY APPEND.
 * 2. Add the type enum in appropriate service section.  Naming convection is
 *    Message type + "TypeId".  For example PutObjectMsg is PutObjectMsgTypeId.
 *    This helps us to use macros in c++.
 * 3. Add the type defition in the appropriate service .thrift file
 * 4. Register the serializer/deserializer in the appropriate service (.cpp/source) files
 */
enum  FDSPMsgTypeId {
    UnknownMsgTypeId                   = 0,

    /* Common Service Types */
    NullMsgTypeId                      = 10,
    EmptyMsgTypeId                     = 11,
    StatStreamMsgTypeId                = 12,

    /* Node/service event messages. */
    NodeSvcInfoTypeId                  = 1000,
    UuidBindMsgTypeId                  = 1001,
    DomainNodesTypeId                  = 1002,
    NodeInfoMsgTypeId                  = 1003,
    NodeInfoMsgListTypeId              = 1004,
    NodeQualifyTypeId                  = 1005,
    NodeUpgradeTypeId                  = 1006,
    NodeRollbackTypeId                 = 1007,
    NodeIntegrateTypeId                = 1008,
    NodeDeployTypeId                   = 1009,
    NodeFunctionalTypeId               = 1010,
    NodeDownTypeId                     = 1011,
    NodeEventTypeId                    = 1012,
    NodeWorkItemTypeId                 = 1013,
    PhaseSyncTypeId                    = 1014,
    UpdateSvcMapMsgTypeId              = 1015,
    GetSvcMapMsgTypeId                 = 1016,
    GetSvcMapRespMsgTypeId             = 1017,
    GetSvcStatusMsgTypeId              = 1018,
    GetSvcStatusRespMsgTypeId          = 1019,

    /* Volume messages; common for AM, DM, SM. */
    CtrlNotifyVolAddTypeId             = 2020,
    CtrlNotifyVolRemoveTypeId          = 2021,
    CtrlNotifyVolModTypeId             = 2022,
    CtrlNotifySnapVolTypeId            = 2023,
    CtrlTierPolicyTypeId               = 2024,
    CtrlTierPolicyAuditTypeId          = 2025,
    CtrlStartHybridTierCtrlrMsgTypeId  = 2026,

    /* SM messages. */
    CtrlStartMigrationTypeId           = 2040,
    CtrlNotifyScavengerTypeId          = 2041,
    CtrlQueryScavengerStatusTypeId	   = 2042,
    CtrlQueryScavengerStatusRespTypeId = 2043,
    CtrlQueryScavengerProgressTypeId   = 2044,
    CtrlQueryScavengerProgressRespTypeId = 2045,
    CtrlSetScavengerPolicyTypeId 	   = 2046,
    CtrlSetScavengerPolicyRespTypeId   = 2047,
    CtrlQueryScavengerPolicyTypeId     = 2048,
    CtrlQueryScavengerPolicyRespTypeId = 2049,
    CtrlQueryScrubberStatusTypeId	   = 2050,
    CtrlQueryScrubberStatusRespTypeId  = 2051,
    CtrlSetScrubberStatusTypeId		   = 2052,
    CtrlSetScrubberStatusRespTypeId	   = 2053,
    CtrlNotifyMigrationFinishedTypeId  = 2054,
    CtrlNotifyMigrationStatusTypeId	   = 2055,


    CtrlNotifyDLTUpdateTypeId          = 2060,
    CtrlNotifyDLTCloseTypeId           = 2061,
    CtrlNotifySMStartMigrationTypeId   = 2062,
    CtrlObjectRebalanceFilterSetTypeId = 2063,
    CtrlObjectRebalanceDeltaSetTypeId  = 2064,
    CtrlNotifySMAbortMigrationTypeId   = 2065,
    CtrlGetSecondRebalanceDeltaSetTypeId    = 2066,
    CtrlGetSecondRebalanceDeltaSetRspTypeId = 2067,

    /* DM messages. */
    CtrlNotifyDMTCloseTypeId           = 2081,
    CtrlNotifyDMTUpdateTypeId          = 2082,
    CtrlNotifyDMAbortMigrationTypeId   = 2083,


    /* OM-> AM messages. */
    CtrlNotifyThrottleTypeId           = 2101,
    CtrlNotifyQoSControlTypeId         = 2102,

    /* AM-> OM */
    CtrlTestBucketTypeId	           = 3000,
    CtrlGetBucketStatsTypeId	       = 3001,
    CtrlCreateBucketTypeId             = 3002,
    CtrlDeleteBucketTypeId             = 3003,
    CtrlModifyBucketTypeId             = 3004,

    /* Svc -> OM */
    CtrlSvcEventTypeId                 = 9000,
    CtrlTokenMigrationAbortTypeId      = 9001,

    /* SM Type Ids*/
    GetObjectMsgTypeId 		= 10000, 
    GetObjectRespTypeId 	= 10001,
    PutObjectMsgTypeId		= 10002, 
    PutObjectRspMsgTypeId	= 10003,
    DeleteObjectMsgTypeId,
    DeleteObjectRspMsgTypeId,
    AddObjectRefMsgTypeId,
    AddObjectRefRspMsgTypeId,
    ShutdownMODMsgTypeId,
	

    /* DM Type Ids */
    QueryCatalogMsgTypeId = 20000,
    QueryCatalogRspMsgTypeId,
    StartBlobTxMsgTypeId,
    StartBlobTxRspMsgTypeId,
    UpdateCatalogMsgTypeId,
    UpdateCatalogRspMsgTypeId,
    UpdateCatalogOnceMsgTypeId,
    UpdateCatalogOnceRspMsgTypeId,
    SetBlobMetaDataMsgTypeId,
    SetBlobMetaDataRspMsgTypeId,
    GetBlobMetaDataMsgTypeId,
    GetBlobMetaDataRspMsgTypeId,
    SetVolumeMetaDataMsgTypeId,
    SetVolumeMetaDataRspMsgTypeId,
    GetVolumeMetaDataMsgTypeId,
    GetVolumeMetaDataRspMsgTypeId,
    CommitBlobTxMsgTypeId,
    CommitBlobTxRspMsgTypeId,
    AbortBlobTxMsgTypeId,
    AbortBlobTxRspMsgTypeId,
    GetBucketMsgTypeId,
    GetBucketRspMsgTypeId,
    DeleteBlobMsgTypeId,
    DeleteBlobRspMsgTypeId,
    ForwardCatalogMsgTypeId,
    ForwardCatalogRspMsgTypeId,
    VolSyncStateMsgTypeId,
    VolSyncStateRspMsgTypeId,
    StatStreamRegistrationMsgTypeId,
    StatStreamRegistrationRspMsgTypeId,
    StatStreamDeregistrationMsgTypeId,
    StatStreamDeregistrationRspMsgTypeId,
    CreateSnapshotMsgTypeId,
    CreateSnapshotRespMsgTypeId,
    DeleteSnapshotMsgTypeId,
    DeleteSnapshotRespMsgTypeId,
    CreateVolumeCloneMsgTypeId,
    CreateVolumeCloneRespMsgTypeId,
    GetDmStatsMsgTypeId,
    GetDmStatsMsgRespTypeId,
    ListBlobsByPatternMsgTypeId,
    ListBlobsByPatternRspMsgTypeId,
    ReloadVolumeMsgTypeId,
    ReloadVolumeRspMsgTypeId
}

/*
 * Generic response message format.
 */

/*
 * This message header is owned, controlled and set by the net service layer.
 * Application code treats it as opaque type.
 */
struct AsyncHdr {
    1: required i32           	msg_chksum;
    2: required FDSPMsgTypeId 	msg_type_id;
    3: required i32           	msg_src_id;
    4: required common.SvcUuid  msg_src_uuid;
    5: required common.SvcUuid  msg_dst_uuid;
    6: required i32           	msg_code;
    7: optional i64             dlt_version = 0;
    8: i64			rqSendStartTs;
    9: i64			rqSendEndTs;
    10:i64        		rqRcvdTs;
    11:i64        		rqHndlrTs;
    12:i64        		rspSerStartTs;	
    13:i64        		rspSendStartTs;	
    14:i64			rspRcvdTs;
}

enum ServiceStatus {
    SVC_STATUS_INVALID      = 0x0000,
    SVC_STATUS_ACTIVE       = 0x0001,
    SVC_STATUS_INACTIVE     = 0x0002
}

/**
 * Service map info
 */
struct SvcInfo {
    1: required common.SvcID             svc_id;
    2: required i32                      svc_port;
    3: required FDSP.FDSP_MgrIdType      svc_type;
    4: required ServiceStatus            svc_status;
    5: required string                   svc_auto_name;
    // TODO(Rao): We should make these required.  They aren't made required as of this writing
    // because it can break existing code.
    6: string				 ip;
    7: i32                               incarnationNo;
    8: string				 name;
    /* <key, value> properties */
    9: map<string, string>               props;
}

/* Message to sent to update the service information */
struct UpdateSvcMapMsg {
    1: required list<SvcInfo>       updates;
}

/* Request for getting the service map */
struct GetSvcMapMsg {
}

/* Reponse for GetSvcMap request */
struct GetSvcMapRespMsg {
    1: required list<SvcInfo>       svcMap;
}

/* Message for requesting service status */
struct GetSvcStatusMsg {
}

/* Response message for service status */
struct GetSvcStatusRespMsg {
    1: ServiceStatus status;
}

/*
 * Uuid to physical location binding registration.
 */
struct UuidBindMsg {
    1: required common.SvcID             svc_id,
    2: required string                   svc_addr,
    3: required i32                      svc_port,
    4: required common.SvcID             svc_node,
    5: required string                   svc_auto_name,
    6: required FDSP.FDSP_MgrIdType      svc_type,
}

struct StorCapMsg {
    1: i32                    disk_iops_max,
    2: i32                    disk_iops_min,
    3: double                 disk_capacity,
    4: i32                    disk_latency_max,
    5: i32                    disk_latency_min,
    6: i32                    ssd_iops_max,
    7: i32                    ssd_iops_min,
    8: double                 ssd_capacity,
    9: i32                    ssd_latency_max,
    10: i32                   ssd_latency_min,
    11: i32                   ssd_count,
    12: i32                   disk_type,
    13: i32                   disk_count,
}

/*
 * Node registration message format.
 */
struct NodeInfoMsg {
    1: required UuidBindMsg   node_loc,
    2: required common.DomainID      node_domain,
    3: StorCapMsg    	      node_stor,
    4: required i32           nd_base_port,
    5: required i32           nd_svc_mask,
    6: required bool          nd_bcast,
}

struct NodeInfoMsgList {
    1: list<NodeInfoMsg>      nd_list,
}

/**
 * This becomes a control path message
 */
struct NodeSvcInfo {
    1: required common.SvcUuid                  node_base_uuid,
    2: i32                               node_base_port,
    3: string                            node_addr,
    4: string                            node_auto_name,
    5: FDSP.FDSP_NodeState               node_state;
    6: i32                               node_svc_mask,
    7: list<SvcInfo>                     node_svc_list,
}

struct DomainNodes {
    1: required common.DomainID                 dom_id,
    2: list<NodeSvcInfo>                 dom_nodes,
}

struct NodeVersion {
    1: required i32                      nd_fds_maj,
    2: required i32                      nd_fds_min,
    3: i32                               nd_fds_release,
    4: i32                               nd_fds_patch,
    5: string                            nd_java_ver,
}

/**
 * Qualify a node before admitting it to the domain.
 */
struct NodeQualify {
    1: required NodeInfoMsg              nd_info,
    2: required string                   nd_acces_token,
    3: NodeVersion                       nd_sw_ver,
    4: string                            nd_md5sum_pm,
    5: string                            nd_md5sum_sm,
    6: string                            nd_md5sum_dm,
    7: string                            nd_md5sum_am,
    8: string                            nd_md5sum_om,
}

/**
 * Notify node to upgrade/rollback SW version.
 */
struct NodeUpgrade {
    1: required common.DomainID                 nd_dom_id,
    2: required common.SvcUuid                  nd_uuid,
    3: NodeVersion                       nd_sw_ver,
    4: required FDSPMsgTypeId            nd_op_code,
    5: required string                   nd_md5_chksum,
    6: string                            nd_pkg_path,
}

struct NodeIntegrate {
    1: required common.DomainID                 nd_dom_id,
    2: required common.SvcUuid                  nd_uuid,
    3: bool                              nd_start_am,
    4: bool                              nd_start_dm,
    5: bool                              nd_start_sm,
    6: bool                              nd_start_om,
}

/**
 * Work item done by a node.
 */
struct NodeWorkItem {
    1: required i32                      nd_work_code,
    2: required common.DomainID                 nd_dom_id,
    3: required common.SvcUuid                  nd_from_svc,
    4: required common.SvcUuid                  nd_to_svc,
}

struct NodeDeploy {
    1: required common.DomainID                 nd_dom_id,
    2: required common.SvcUuid                  nd_uuid,
    3: list<NodeWorkItem>                nd_work_item,
}

struct NodeFunctional {
    1: required common.DomainID                 nd_dom_id,
    2: required common.SvcUuid                  nd_uuid,
    3: required FDSPMsgTypeId            nd_op_code,
    4: list<NodeWorkItem>                nd_work_item,
}

struct NodeDown {
    1: required common.DomainID                 nd_dom_id,
    2: required common.SvcUuid                  nd_uuid,
}

/**
 * Events emit from a node.
 */
struct NodeEvent {
    1: required common.DomainID                 nd_dom_id,
    2: required common.SvcUuid                  nd_uuid,
    3: required string                   nd_evt,
    4: string                            nd_evt_text,
}

/**
 * 2-Phase Commit message.
 */
struct PhaseSync {
    1: required common.SvcUuid                  ph_rcv_uuid,
    2: required i32                      ph_sync_evt,
    3: binary                            ph_data,
}

/*
 * --------------------------------------------------------------------------------
 * Common services
 * --------------------------------------------------------------------------------
 */

service BaseAsyncSvc {
    oneway void asyncReqt(1: AsyncHdr asyncHdr, 2: string payload);
    oneway void asyncResp(1: AsyncHdr asyncHdr, 2: string payload);
    AsyncHdr uuidBind(1: UuidBindMsg msg);
}

service PlatNetSvc extends BaseAsyncSvc {
    oneway void allUuidBinding(1: UuidBindMsg mine);
    oneway void notifyNodeActive(1: FDSP.FDSP_ActivateNodeType info);

    list<NodeInfoMsg> notifyNodeInfo(1: NodeInfoMsg info, 2: bool bcast);
    DomainNodes getDomainNodes(1: DomainNodes dom);

    ServiceStatus getStatus(1: i32 nullarg);
    map<string, i64> getCounters(1: string id);
    void resetCounters(1: string id);
    void setConfigVal(1:string id, 2:i64 value);
    void setFlag(1:string id, 2:i64 value);
    i64 getFlag(1:string id);
    string getProperty(1: string name);
    map<string, string> getProperties(1: i32 nullarg);
    map<string, i64> getFlags(1: i32 nullarg);
    /* For setting fault injection.
     * @param cmdline format based on libfiu
     */
    bool setFault(1: string cmdline);
}

/*
 * --------------------------------------------------------------------------------
 * Common messages
 * --------------------------------------------------------------------------------
 */
struct EmptyMsg {
}

/*
 * --------------------------------------------------------------------------------
 * Common Control Path Messages
 * --------------------------------------------------------------------------------
 */

/* ----------------------  CtrlNotifyVolAddTypeId  ----------------------------- */
struct CtrlNotifyVolAdd {
     1: FDSP.FDSP_VolumeDescType  vol_desc;   /* volume properties and attributes. */
     3: FDSP.FDSP_NotifyVolFlag   vol_flag;
}

/* ----------------------  CtrlNotifyVolRemoveTypeId  -------------------------- */
struct CtrlNotifyVolRemove {
     1: FDSP.FDSP_VolumeDescType  vol_desc;   /* volume properties and attributes. */
     2: FDSP.FDSP_NotifyVolFlag   vol_flag;
}

/* -----------------------  CtrlNotifyVolModTypeId  ---------------------------- */
struct CtrlNotifyVolMod {
     1: FDSP.FDSP_VolumeDescType  vol_desc;   /* volume properties and attributes. */
     2: FDSP.FDSP_NotifyVolFlag   vol_flag;
}

/* ----------------------  CtrlNotifySnapVolTypeId  ---------------------------- */
struct CtrlNotifySnapVol {
     1: FDSP.FDSP_VolumeDescType  vol_desc;   /* volume properties and attributes. */
     3: FDSP.FDSP_NotifyVolFlag   vol_flag;
}

/* ------------ Debug message for starting hybrid tier controller manually --------- */
struct CtrlStartHybridTierCtrlrMsg
{
}

/* ---------------------  ShutdownMODMsgTypeId  --------------------------- */
struct ShutdownMODMsg {
}
