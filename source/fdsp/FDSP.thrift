/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */

include "svc_types.thrift"

namespace c_glib FDS_ProtocolInterface
namespace cpp FDS_ProtocolInterface
namespace * FDS_ProtocolInterface


enum FDSP_MsgCodeType {
   FDSP_MSG_PUT_OBJ_REQ,
   FDSP_MSG_GET_BUCKET_STATS_RSP,

   FDSP_MSG_CREATE_VOL,
   FDSP_MSG_MODIFY_VOL,
   FDSP_MSG_DELETE_VOL,
   FDSP_MSG_ATTACH_VOL_CMD,
   FDSP_MSG_DETACH_VOL_CMD,

   FDSP_MSG_ATTACH_VOL_CTRL,
   FDSP_MSG_DETACH_VOL_CTRL,
}

enum FDSP_ResultType {
  FDSP_ERR_OK,
  FDSP_ERR_FAILED,
  FDSP_ERR_VOLUME_DOES_NOT_EXIST,
  FDSP_ERR_VOLUME_EXISTS,
  FDSP_ERR_BLOB_NOT_FOUND
  /* DEPRECATED: Don't add anthing here.  We will use Error from fds_err.h */
}

enum FDSP_ErrType {
  FDSP_ERR_OKOK,            /* not to conflict with result type, and protect when using fds_errno_t in msg_hdr.err_code */
  FDSP_ERR_SM_NO_SPACE,
  /* DEPRECATED: Don't add anthing here.  We will use Error from fds_err.h */
}

enum FDSP_VolNotifyType {
  FDSP_NOTIFY_DEFAULT,
  FDSP_NOTIFY_ADD_VOL,
  FDSP_NOTIFY_RM_VOL,
  FDSP_NOTIFY_MOD_VOL,
  FDSP_NOTIFY_SNAP_VOL
}

enum FDSP_ConsisProtoType {
  FDSP_CONS_PROTO_STRONG,
  FDSP_CONS_PROTO_WEAK,
  FDSP_CONS_PROTO_EVENTUAL
}

enum FDSP_AppWorkload {
    FDSP_APP_WKLD_TRANSACTION,
    FDSP_APP_WKLD_NOSQL,
    FDSP_APP_WKLD_HDFS,
    FDSP_APP_WKLD_JOURNAL_FILESYS,  // Ext3/ext4
    FDSP_APP_WKLD_FILESYS,  // XFS, other
    FDSP_APP_NATIVE_OBJS,  // Native object aka not going over http/rest apis
    FDSP_APP_S3_OBJS,  // Amazon S3 style objects workload
    FDSP_APP_AZURE_OBJS,  // Azure style objects workload
}

struct FDSP_PutObjType {
 1: svc_types.FDS_ObjectIdType data_obj_id,
 2: i32              data_obj_len,
 3: i32              volume_offset, /* Offset inside the volume where the object resides */
 4: i32              dlt_version,
 5: binary           data_obj,
 6: binary           dlt_data,
}

struct FDSP_GetObjType {
 1: svc_types.FDS_ObjectIdType data_obj_id,
 2: i32              data_obj_len,
 3: i32              dlt_version,
 4: binary           data_obj,
 5: binary           dlt_data,
}

struct  FDSP_DeleteObjType { /* This is a SH-->SM msg to delete the objectId */
 1: svc_types.FDS_ObjectIdType data_obj_id,
 2: i32              dlt_version,
 3: i32              data_obj_len,
 4: binary           dlt_data,
}

struct FDSP_OffsetWriteObjType {
  1: svc_types.FDS_ObjectIdType   data_obj_id_old,
  2: i32      data_obj_len,
  3: svc_types.FDS_ObjectIdType   data_obj_id_new,
  4: i32      dlt_version,
  5: binary  data_obj,
  6: binary  dlt_data
}

struct FDSP_RedirReadObjType {
  1: svc_types.FDS_ObjectIdType   data_obj_id_old,
  2: i32      data_obj_len,
  3: i32      data_obj_suboffset, /* Offset within the object where the actual data is modified */
  4: i32      data_obj_sublen,
  5: svc_types.FDS_ObjectIdType   data_obj_id_new,
  6: i32      dlt_version,
  7: binary   data_obj,
  8: binary   dlt_data
}

struct FDSP_VerifyObjType {
  1: svc_types.FDS_ObjectIdType   data_obj_id,
  2: i32      data_obj_len,
  3: binary  data_obj
}

struct FDSP_BlobDigestType {
#  1: i64 low,
#  2: i64 high
  1: binary  digest
}

/* Can be consolidated when apis and fdsp merge or whatever */
struct TxDescriptor {
       1: required i64 txId
       /* TODO(Andrew): Maybe add an op type (update/query)? */
}

struct  FDSP_DeleteCatalogType { /* This is a SH-->SM msg to delete the objectId */
  1: string   blob_name,       /* User visible name of the blob*/
  2: i64 blob_version, /* Version to delete */
  3: i32  dmt_version,
}

typedef list<i64> Node_List_Type
typedef list<Node_List_Type> Node_Table_Type

struct FDSP_DLT_Type {
      1: i32 DLT_version,
      2: Node_Table_Type DLT,
}

struct FDSP_DLT_Resp_Type {
  1: i64 DLT_version
}

struct FDSP_DMT_Resp_Type {
  1: i64 DMT_version
}

struct FDSP_AnnounceDiskCapability {
  1: i32	node_iops_max,  /* iops suppported by */
  2: i32	node_iops_min,  /* iops suppported by */
  3: i64	disk_capacity, /* size of the disk  */
  4: i64	ssd_capacity, /* size of the ssd disks  */
  5: i32	disk_type,  /* disk type  */
}

struct FDSP_RegisterNodeType {
  1: svc_types.FDSP_MgrIdType node_type,   /* Type of node - SM/DM/HV */
  2: string 	 node_name,      /* node indetifier - string */
  3: i32 	     domain_id,      /* domain indetifier */
  4: i64		 ip_hi_addr,     /* IP V6 high address */
  5: i64		 ip_lo_addr,     /* IP V4 address of V6 low address of the node */
  6: i32		 control_port,   /* Port number to contact for control messages */
  7: i32		 data_port,      /* Port number to send datapath requests */
  8: i32         migration_port, /*  Port for migration service */
  9: svc_types.FDSP_Uuid   node_uuid;
  10: svc_types.FDSP_Uuid  service_uuid;
  11: FDSP_AnnounceDiskCapability  disk_info, /* Add node capacity and other relevant fields here */
  12: string 	 node_root,      /* node root - string */
  13: i32         metasync_port, /*  Port for meta sync port service */
}

struct FDSP_BucketStatType {
  1: string             vol_name,
  2: double             performance,  /* average iops */
  3: i64                assured,      /* minimum (guaranteed) iops */
  4: i64                throttle,     /* maximum iops */
  5: i32                rel_prio,     /* relative priority */
}

typedef list<FDSP_BucketStatType> FDSP_BucketStatListType

struct FDSP_BucketStatsRespType {
  1: string                    timestamp,          /* timestamp of the stats */
  2: FDSP_BucketStatListType   bucket_stats_list,  /* list of bucket stats */
}

struct FDSP_QueueStateType {

  1: i32 domain_id,
  2: i64  vol_uuid,
  3: i32 priority,
  4: double queue_depth, //current queue depth as a fraction of the total queue size. 0.5 means 50% full.

}

struct FDSP_MsgHdrType {
  1: FDSP_MsgCodeType     msg_code,

   // Message versioning for compatibility check, functionality changes
  2: i32 major_ver,   /* Major version number of this message */
  3: i32 minor_ver,   /* Minor version number of this message */
  4: i32 msg_id,      /* Message id to discard duplicate request & mai32ain causal order */
  5: i32 payload_len,
  6: i32 num_objects, /* Payload could contain more than one object */
  7: i32 frag_len,    /* Fragment Length  */
  8: i32 frag_num,    /* Fragment number for partial transfer */

  // Volume entity idenfiers
  9:  i32    tennant_id,      /* Tennant owning the Local-domain and  Storage  hypervisor */
  10: i32    local_domain_id, /* Local domain the volume in question bei64s */
  11: i64    glob_volume_id, /* Tennant owning the Local-domain and Storage hypervisor */
  12: string bucket_name, /* Bucket Name or Container Name for S3 or Azure entities */

 // Source and Destination Distributed s/w entities
  13: svc_types.FDSP_MgrIdType  src_id,
  14: svc_types.FDSP_MgrIdType  dst_id,
  15: i64             src_ip_hi_addr, /* IPv4 or IPv6 Address */
  16: i64             src_ip_lo_addr, /* IPv4 or IPv6 Address */
  17: i64             dst_ip_hi_addr, /* IPv4 or IPv6 address */
  18: i64             dst_ip_lo_addr, /* IPv4 or IPv6 address */
  19: i32             src_port,
  20: i32             dst_port,
  21: string          src_node_name, /* string identifying the source node that sent this request */

  // FDSP Result valid for response messages
  22: FDSP_ResultType result,
  23: string          err_msg,
  24: i32             err_code,

  // Checksum of the entire message including the payload/objects
  25: i32         req_cookie,
  26: i32         msg_chksum,
  27: string      payload_chksum,
  28: string      session_uuid,
  29: svc_types.FDSP_Uuid   src_service_uuid,    /* uuid of service that sent this request */
  30: i64         origin_timestamp,
  31: i32         proxy_count,
  32: string      session_cache,  // this will be removed once we have  transcation journel  in DM
}

struct FDSP_VolMetaState
{
    1: i64        vol_uuid;
    2: bool       forward_done;   /* true means forwarding done, false means second rsync done */
}

/* Token type */
typedef i32 FDSP_Token

/* raw data for the object */
typedef string FDSP_ObjectData

struct FDSP_MigMsgHdrType
{
	/* Header */
	1: FDSP_MsgHdrType            base_header

	/* Id of the overall migration */
	2: string                     mig_id

	/* Id to identify unique migration between sender and receiver */
	3: string                     mig_stream_id;
}

/* Object id data pair */
struct FDSP_ObjectIdDataPair
{
	1: svc_types.FDS_ObjectIdType  obj_id
	
	2: FDSP_ObjectData   data
}

/* Volume associations */
struct FDSP_ObjectVolumeAssociation
{
	1: svc_types.FDSP_Uuid				vol_id
	2: i32						ref_cnt
}
typedef list<FDSP_ObjectVolumeAssociation> FDSP_ObjectVolumeAssociationList

/* Meta data for migration object */
/* DEPRECATED */
struct FDSP_MigrateObjectMetadata
{
    1: FDSP_Token              token_id
    2: svc_types.FDS_ObjectIdType        object_id
    3: i32                     obj_len
    4: i64                     born_ts
    5: i64                     modification_ts
    6: FDSP_ObjectVolumeAssociationList associations
}

struct FDSP_GetObjMetadataReq {
 1: FDSP_MsgHdrType		header
 2: svc_types.FDS_ObjectIdType 	obj_id
}

struct FDSP_GetObjMetadataResp {
 1: FDSP_MsgHdrType				header
 2: FDSP_MigrateObjectMetadata 	meta_data
}

/* Current state of tokens in an SM */
struct FDSP_TokenMigrationStats {
	/* Number of tokens for which migration is complete */
	1: i32			completed
	/* Number of token for which migration is in progress */
	2: i32          inflight
	/* Number of tokens for which migration is pending */
	3: i32          pending
}

/* Response for establish session request */
struct FDSP_SessionReqResp {
    1: i32           status, // TODO: This should be fdsp error code
    2: string        sid,
}

service FDSP_Service {
	FDSP_SessionReqResp EstablishSession(1:FDSP_MsgHdrType fdsp_msg)
}
