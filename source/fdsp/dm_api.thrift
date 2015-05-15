/*
 * Copyright 2014 by Formation Data Systems, Inc.
 * vim: noai:ts=8:sw=2:tw=100:syntax=cpp:et
 */

include "common.thrift"
include "dm_types.thrift"
include "svc_api.thrift"

namespace cpp FDS_ProtocolInterface
namespace java com.formationds.protocol.dm

/* ------------------------------------------------------------
   Operations on Volumes
   ------------------------------------------------------------*/

/**
 * Lists specific contents of a volume. A specific offset into
 * the current list of blobs in the volumes and number of blobs
 * to be returned may be set.
 * Note that each call is indendant, so the order and contents
 * may change between calls.
 * A pattern filter may be specified that returns only blobs whose
 * name matches the string pattern.
 */
struct GetBucketMsg {
  1: required i64              volume_id;
  2: i64                       startPos = 0;
  3: i64                       count = 10000;
  4: string                    pattern = "";
  5: common.BlobListOrder      orderBy = 0;
  6: bool                      descending = false;
}
/**
 * Returns a list of blob descriptors matching the query. The
 * list may be ordered depending on the query.
 */
struct GetBucketRspMsg {
  1: required dm_types.BlobDescriptorListType     blob_descr_list;
}

/**
 * Gets the dynamic status of a specific volume.
 * TODO(Andrew): Move the volume status to the
 * response structure.
 */
struct StatVolumeMsg {
  1: i64                        volume_id;
  2: dm_types.VolumeStatus      volumeStatus;
}
/**
 * Returns a status descriptor for the volume.
 */
struct StatVolumeRspMsg {
}

/**
 * Gets metadata for a specific volume.
 */
struct GetVolumeMetadataMsg {
  1: i64 volumeId;
}
/**
 * Returns key-value metadata for the volume.
 */
struct GetVolumeMetadataMsgRsp {
  1: dm_types.FDSP_MetaDataList metadataList;
}

/**
 * Sets metadata for a specific volume. Any already existing
 * metadata keys are overwritten.
 */
struct SetVolumeMetadataMsg {
  1: i64                        volumeId;
  2: dm_types.FDSP_MetaDataList metadataList;
}
/**
 * Returns success if metadata was updated.
 */
struct SetVolumeMetadataMsgRsp {
}

/**
 * Creates a snapshot of the volume. A snapshot is a read-only,
 * point-in-time copy of the voluem.
 * TODO(Andrew): Why not remove the snapshot struct and just
 * add those structures to this message?
 */
struct CreateSnapshotMsg {
    1:common.Snapshot snapshot
}
/**
 * Response contains the ID of the newly created snapshot.
 * If the snapshot could not be taken, ERR_DM_SNAPSHOT_FAILED
 * is returned.
 */
struct CreateSnapshotRespMsg {
    1:i64 snapshotId
}

/**
 * Deletes a specific snapshot.
 * TODO(Andrew): Is there actually a distinction between
 * the snapshot ID and the volume ID?
 */
struct DeleteSnapshotMsg {
    1:i64 snapshotId
}
/**
 * Return ERR_OK
 * TODO(Andrew): Why does this return anything?
 */
struct DeleteSnapshotRespMsg {
    1:i64 snapshotId
}

/**
 * Creates a clone of the volume. A clone is an entirely
 * new, read/write-able volume that contains identical contents
 * to the volume it is cloned from. Once cloned updates to
 * the original and clone volume are unrelated.
 * TODO(Andrew): Why is the cloneId in the request?
 */
struct CreateVolumeCloneMsg {
     1:i64 volumeId,
     2:i64 cloneId,
     3:string cloneName,
     4:i64 VolumePolicyId
}  
/**
 * Response contains the ID of the newly created clone
 * volume. This ID is equivalent to the volume's ID.
 * If the clone could not be created, ERR_I_DONT_KNOW (?)
 * is returned.
 */
struct CreateVolumeCloneRespMsg {
     1:i64 cloneId
}

/**
 * Opens the volume for access with optional given policy with an
 * optional lock object to renew an existing lease. A new lease
 * will be made if a lock has been acquired since the last use
 * of the given lock.
 */
struct OpenVolumeMsg {
  /** The volume to request access to */
  1: required i64                       volume_id;
  /** Existing token */
  2: optional i64                       token = 0;
  /** Requested access policy (or volume default) */
  3: optional common.VolumeAccessPolicy policy;
}

/**
 * Response to a lock request. If lock was renewed it will be
 * equivalent to the lock used in the request.
 */
struct OpenVolumeRspMsg {
  /** Token for volume access */
  1: required i64       token;
}

/**
 * Explicit release of volume lock. Allows other clients to write to volume.
 */
struct CloseVolumeMsg {
  /** The volume to request access to */
  1: required i64       volume_id;
  /** The token we are releasing */
  2: required i64       token;
}

/**
 * Response to release.
 */
struct CloseVolumeRspMsg {
}

/* ------------------------------------------------------------
   Operations on Blobs
   ------------------------------------------------------------*/

/**
 * Starts a new transaction for a blob. The transaction ID is given
 * by the caller and used by the DM.
 */
struct StartBlobTxMsg {
  1: i64                        volume_id;
  2: string                     blob_name;
  /** TODO(Andrew): The blob version isn't used, should be removed. */
  3: i64                        blob_version;
  /** TODO(Andrew): The blob_mode should become a Thrift defined enum. */
  4: i32                        blob_mode;
  5: i64                        txId;
  /** TODO(Andrew): Shouldn't need the DMT version? */
  6: i64                       dmt_version;
}
/**
 * If the transaction was unable to start because the given
 * transcation ID already existed, ERR_DM_TX_EXISTS will be
 * returned.
 */
struct StartBlobTxRspMsg {
}

/**
 * Commits a currently active transaction.
 */
struct CommitBlobTxMsg {
  1: i64                        volume_id;
  2: string                     blob_name;
  3: i64                        blob_version;
  4: i64                        txId;
  5: i64                        dmt_version,
}
/**
 * Response contains the logical size of the blob and its
 * metadata after the the transaction is committed.
 * If the transaction did not exist, ERR_DM_INVALID_TX_ID is
 * returned.
 * If the commit failed because the transaction contained invalid
 * updates, ERR_I_HAVE_NO_CLUE (?) is returned.
 */
struct CommitBlobTxRspMsg {
   /** Blob size */
   1: i64                  byteCount;
   /** Sequence of arbitrary key/value pairs */
   2: dm_types.FDSP_MetaDataList    meta_list;
}

/**
 * Aborts a currently active transaction.
 */
struct AbortBlobTxMsg {
   1: i64                       volume_id;
   2: string                    blob_name;
  /** TODO(Andrew): The blob version isn't used, should be removed. */
   3: i64                       blob_version;
   5: i64                       txId;
}
/**
 * If the transaction did not exist, ERR_DM_INVALID_TX_ID is
 * returned.
 */
struct AbortBlobTxRspMsg {
}

/**
 * Updates an existing transaction with a new blob update. The update
 * is not applied until the transcation is committed. 
 * Updates within a transaction are ordered so this update may overwrite
 * a previous update to the same offset in the same transaction context
 * when committed.
 */
struct UpdateCatalogMsg {
  1: i64                        volume_id;
  2: string                     blob_name;
  /** TODO(Andrew): The blob version isn't used, should be removed. */
  3: i64                       blob_version;
  4: i64                        txId;
  /** List of object ids of the objects that this blob is being mapped to */
  5: dm_types.FDSP_BlobObjectList       obj_list;
}
/**
 * If the transaction did not exist, ERR_DM_INVALID_TX_ID is
 * returned.
 */
struct UpdateCatalogRspMsg {
}

/**
 * Updates a blob in one atomic operation. No existing transaction context
 * is needed.
 * The object and/or metadata list may be empty.
 */
struct UpdateCatalogOnceMsg {
   1: i64                       volume_id;
   2: string                    blob_name;
  /** TODO(Andrew): The blob version isn't used, should be removed. */
   3: i64                       blob_version;
   4: i32                       blob_mode;
   5: i64                       dmt_version;
   6: i64                       txId;
   /** List of object ids of the objects that this blob is being mapped to */
   7: dm_types.FDSP_BlobObjectList      obj_list;
   8: dm_types.FDSP_MetaDataList        meta_list;
}
/**
 * Response contains the logical size of the blob and its
 * metadata after the the transaction is committed.
 */
struct UpdateCatalogOnceRspMsg {
   /** Blob size */
   1: i64                        byteCount;
   /** Sequence of arbitrary key/value pairs */
   2: dm_types.FDSP_MetaDataList meta_list;
}

/**
 * Updates an existing transaction with a new blob update. The
 * is not applied until the transcation is committed.
 * Metadata keys are unique so this update may overwrite a
 * previously written value for the same key.
 * Updates within a transaction are ordered so this update may overwrite
 * a previous update to the same key in the same transaction context
 * when committed.
 */
struct SetBlobMetaDataMsg {
  1: i64                        volume_id;
  2: string                     blob_name;
  /** TODO(Andrew): The blob version isn't used, should be removed. */
  3: i64                        blob_version;
  4: dm_types.FDSP_MetaDataList         metaDataList;
  5: i64                        txId;
}
/**
 * If the transaction did not exist, ERR_DM_INVALID_TX_ID is
 * returned.
 */
struct SetBlobMetaDataRspMsg {
}

/**
 * Updates an existing transaction with a request to delete a blob.
 * When committed, the entire blob and all of its metadata are deleted.
 * The delete may be lazy so there may be options to restore the blob later.
 */
struct DeleteBlobMsg {
  1: i64                       txId;
  2: i64                       volume_id;
  3: string                    blob_name;
  4: i64                       blob_version;
}
/**
 * If the transaction did not exist, ERR_DM_INVALID_TX_ID is
 * returned.
 */
struct DeleteBlobRspMsg {
}

/**
 * Gets all metadata associated with a specific blob. The metadata
 * includes fields that DM maintains (e.g., blob size) and key-value
 * metadata that is opaque to DM.
 */
struct GetBlobMetaDataMsg {
  1: i64                       volume_id;
  2: string                    blob_name;
  /** TODO(Andrew): The blob version isn't used, should be removed. */
  3: i64                       blob_version;
  /** TODO(Andrew): This should move to the response */
  4: i64                       byteCount;
  /** TODO(Andrew): This should move to the response */
  5: dm_types.FDSP_MetaDataList         metaDataList;
}
struct GetBlobMetaDataRspMsg {
}

/**
 * Retrieves offset and metadata information for a specific
 * range within a blob. A start and end offset is specified.
 * The start offset is required however if the end offset is not
 * specified it is assumed to mean the end of the blob.
 * By default, the blob's size and full metadata list are always
 * returned.
 */
struct QueryCatalogMsg {
   1: i64                          volume_id;
   2: string                       blob_name;
   3: i64                          start_offset;
   /** The end_offset is optional. TODO(Andrew): Mark it optional? */
   4: i64                          end_offset;
   /** TODO(Andrew): The blob version isn't used, should be removed. */
   5: i64                          blob_version;
   /** TODO(Andrew): Move this to the response. */
   6: dm_types.FDSP_BlobObjectList obj_list;
   /** TODO(Andrew): Move this to the response. Do we actually need to return it at all? */
   7: dm_types.FDSP_MetaDataList   meta_list;
   /** TODO(Andrew): Move this to the response. Do we actually need to return it at all? */
   8: i64                          byteCount;
}
/**
 * TODO(Andrew): Move fields from request to response.
 */
struct QueryCatalogRspMsg {
}

/* ------------------------------------------------------------
   Operations for Statistics
   ------------------------------------------------------------*/

/**
 * Registers a new listener for a statistics stream (i think?).
 * TODO(Andrew): Check that we actually use this.
 */
struct StatStreamRegistrationMsg {
   1:i32 id,
   2:string url,
   3:string method
   4:common.SvcUuid dest,
   5:list<i64> volumes,
   6:i32 sample_freq_seconds,
   7:i32 duration_seconds,
}
struct StatStreamRegistrationRspMsg {
}

/**
 * Registers a new listener for a statistics stream (i think?).
 * TODO(Andrew): Check that we actually use this.
 */
struct StatStreamDeregistrationMsg {
    1: i32 id;
}
struct StatStreamDeregistrationRspMsg {
}

/**
 * Time slot containing volume stats
 */
struct VolStatSlot {
    /** Timestamp in seconds relative to start_timestamp */
    1: i64    rel_seconds;
    /** Represents time slot of stats */
    2: binary slot_data;
}

/**
 * Volume's time-series of stats
 */
struct VolStatList {
    1: i64                  volume_id;
    /** List of time slots */
    2: list<VolStatSlot>    statlist;
}

/**
 * Stats from a service to DM for aggregation
 */
struct StatStreamMsg {
    1: i64                    start_timestamp;
    2: list<VolStatList>      volstats;
}

/**
 * TODO(Andrew): Find out if this is still used.
 */
struct GetDmStatsMsg {
  1: i64                       volume_id;
  2: i64                       commitlog_size;
  3: i64                       extent0_size;
  4: i64                       extent_size;
  5: i64                       metadata_size;
}
struct GetDmStatsRespMsg {
}

/* ------------------------------------------------------------
   Operations for Replication
   ------------------------------------------------------------*/

/**
 * Starts a volume replica migration between two DMs.
 * TODO(Andrew): This needs a response structure.
 */
struct CtrlDMMigrateMeta
{
     /* meta data */
     2: dm_types.FDSP_metaDataList          metaVol;
}

/**
 * Notifies DM that a specific DMT version has been closed.
 * TODO(Andrew): This needs a response structure.
 */
struct CtrlNotifyDMTClose {
     1: dm_types.FDSP_DmtCloseType    dmt_close;
}

/**
 * Aborts a currently active replica migration. The
 * migration is addressed by DMT version and all migrations
 * on behalf of that migration are aborted.
 * TODO(Andrew): This needs a response structure.
 */
struct CtrlNotifyDMAbortMigration {
     1: i64  DMT_version;
}

/**
 * Notifies DM of a catalog update at a replica that it
 * is currently migrating data to it.
 * TODO(Andrew): The ID of the migration that this forward
 * is on behalf of should be included.
 * TODO(Andrew): We likely need some sort of ordering info
 * so that forwarded messages don't come out of order to
 * the receiving node.
 */
struct ForwardCatalogMsg {
  1: i64                          volume_id;
  2: string                       blob_name;
  /** TODO(Andrew): The blob version isn't used, should be removed. */
  3: i64                          blob_version;
  4: dm_types.FDSP_BlobObjectList obj_list;
  5: dm_types.FDSP_MetaDataList   meta_list;
}
/**
 * Forward catalog update response message
 */
struct ForwardCatalogRspMsg {
}

/**
 * Volume sync state msg sent from source to destination DM
 * TODO(Andrew): Is this still used?
 */
struct VolSyncStateMsg
{
    1: i64        volume_id;
    2: bool       forward_complete;   /* true = forwarding done, false = second rsync done */
}
struct VolSyncStateRspMsg {
}

/**
 * Reload volume will close leveldb for a specified volume
 * and re-instantiate it
 */
struct ReloadVolumeMsg {
    1: i64 volume_id;
}
struct ReloadVolumeRspMsg {
}

/* ------------------------------------------------------------
   Other specified services
   ------------------------------------------------------------*/

/**
 * DM Service.  Only put sync rpc calls in here.  Async RPC calls use
 * message passing provided by BaseAsyncSvc
 */
service DMSvc extends svc_api.PlatNetSvc {
}
