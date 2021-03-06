/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_DATA_MGR_INCLUDE_DM_VOL_CAT_DMVOLUMECATALOG_H_
#define SOURCE_DATA_MGR_INCLUDE_DM_VOL_CAT_DMVOLUMECATALOG_H_

// Standard includes.
#include <set>
#include <string>
#include <vector>

// Internal includes.
#include "blob/BlobTypes.h"
#include "concurrency/Mutex.h"
#include "dm-vol-cat/DmPersistVolCat.h"
#include "util/Log.h"
#include "DmBlobTypes.h"
#include "fds_error.h"
#include "fds_types.h"
#include "fds_volume.h"
#include "VcQueryIface.h"

namespace fds {

class VolumeMeta;

/**
 * This modules stores all blob metadata and object ids for
 * all volumes managed by this DM. This module internally
 * uses two sub-modules: persistent layer and a cache that
 * sits on top of persistent layer
 */
class DmVolumeCatalog : public HasModuleProvider,
    public Module,
    public HasLogger,
    public VolumeCatalogQueryIface {
  public:
    typedef boost::shared_ptr<DmVolumeCatalog> ptr;
    typedef boost::shared_ptr<const DmVolumeCatalog> const_ptr;

    // static methods
    inline static fds_uint32_t getLastObjSize(fds_uint64_t blobSize, fds_uint32_t objSize) {
        if (blobSize == 0) {
            return 0;
        }

        fds_uint32_t rc = blobSize % objSize;
        return (rc ? rc : objSize);
    }

    inline static fds_uint64_t getLastOffset(fds_uint64_t blobSize, fds_uint32_t objSize) {
        return (blobSize - getLastObjSize(blobSize, objSize));
    }

    DmVolumeCatalog(CommonModuleProviderIf* modProvider, char const* const name);
    ~DmVolumeCatalog();

    // Methods
    virtual void mod_startup() override {}
    virtual void mod_shutdown() override {}

    /**
     * Add catalog for a new volume described in 'voldesc'
     * When the function returns, the volume catalog for this volume
     * is inactive until activateCatalog() is called. For just added
     * volumes, activateCatalog() will be called right away. For existing
     * volumes for which this DM takes a new responsibility, the volume
     * catalog remains inactive for some time while volume meta is
     * being synced from other DMs.
     *
     * @param voldesc is a volume descriptor, which contains volume id, name
     *        and other information about the volume
     */
    Error addCatalog(const VolumeDesc & voldesc);

    /**
     * Create a copy of a volume
     */
    Error copyVolume(const VolumeDesc & voldesc);

    /**
     * Activate catalog for the given volume 'volId'. After this call
     * Volume Catalog will accept put/get/delete requests for this volume
     */
    Error activateCatalog(fds_volid_t volId) override;

    /**
     * Reload catalog for the given volume
     */
    Error reloadCatalog(const VolumeDesc & voldesc) override;

    /**
     * VolumeCatalogQueryIface methods. This interface is used by DM
     * processing layer to query for committed blob metadata.
     */
    /**
     * Callback to expunge a list of objects from a volume
     */
    void registerExpungeObjectsCb(expunge_objs_cb_t cb) override;

    /**
     * Marks volume as deleted
     * @return ERR_OK
     */
    Error markVolumeDeleted(fds_volid_t volId);

    /**
     * Deletes catalog
     * @return ERR_OK if catalog was deleted; ERR_NOT_READY if volume is not marked
     * as deleted.
     */
    Error deleteCatalog(fds_volid_t volId, bool checkDeleted = true);

    /**
     * Returns logical size of volume and number of blob in the volume 'volume_id'
     * @param[out] size logical size of volume in bytes
     * @param[out] blob_count number of blobs in the volume
     * @param[out] object_count logical object count per volume
     * @param[out] maxSeqId maximum sequenceid.  Only computed if not null
     * @return ERR_OK on success, ERR_VOL_NOT_FOUND if volume is not known
     * to volume catalog
     */
    Error statVolumeLogical(fds_volid_t volId, fds_uint64_t* volSize,
                            fds_uint64_t* blobCount, fds_uint64_t* objCount,
                            sequence_id_t * maxSeqId) override;

    /**
     * Returns physical size of the volume.
     *
     * @param[in] volId volume identifier
     * @param[out] pbytes Volume physical size in bytes.
     * @param[out] pobjects Number of physical oData Objects comprising the volume.
     *
     * @return ERR_OK on success
     */
    Error statVolumePhysical(fds_volid_t volId, fds_uint64_t* pbytes, fds_uint64_t* pObjCount);

    /**
     * Sets the key-value metadata pairs for the volume. Any keys that already
     * existed are overwritten and previously set keys are left unchanged.
     * @param[in] volId The ID of the volume's catalog to update
     * @param[in] metadataList A list of metadata key value pairs to set.
     * @param[in] seq_id sequence id for the operation.
     * @return ERR_OK on success, ERR_VOL_NOT_FOUND if volume is not known
     * to volume catalog.
     */
    Error setVolumeMetadata(fds_volid_t volId,
                            const fpi::FDSP_MetaDataList &metadataList,
                            const sequence_id_t seq_id);

    /**
     * Gets the key-value metadata pairs for the volume.
     * @param[in]  volId The ID of the volume's catalog to update
     * @param[out] metadataList A returned list of metadata key value pairs.
     * @return ERR_OK on success, ERR_VOL_NOT_FOUND if volume is not known
     * to volume catalog.
     */
    Error getVolumeMetadata(fds_volid_t volId,
                            fpi::FDSP_MetaDataList &metadataList) override;

    /**
     * Get all objects for the volume
     *
     * @param[in] volId volume identifier
     * @param[in,out] objIds set of all object ids for this volume
     *
     * @return ERR_OK on success
     */
    Error getVolumeObjects(fds_volid_t volId, std::set<ObjectID> & objIds);

    /**
    * @brief Returns object ids in snap(if null then active volume) starting from dbItr upto
    * maxObjs count of objects.
    *
    * @param volId
    * @param maxObjs
    * @param snap
    * @param dbItr
    * @param objects
    *
    * @return
    */
    Error getObjectIds(fds_volid_t volId,
                       const uint32_t &maxObjs,
                       const Catalog::MemSnap &snap,
                       std::unique_ptr<Catalog::catalog_iterator_t>& dbItr,
                       std::list<ObjectID> &objects) override;

    /**
     * Retrieves blob meta for the given blobName and volume 'volId'
     * @param[in] volId volume identifier
     * @param[in] blobName name of the blob
     * @param[in] blobVersion version of the blob to retrieve, if not set,
     *            the most recent version is retrieved. When the method returns,
     *            blobVersion is set to actual version that is retrieved
     * @param[out] blobSize size of the blob
     * @param[out] metaList list of metadata key-value pairs
     * @return ERR_OK on success, ERR_VOL_NOT_FOUND if volume is not known
     */
    Error getBlobMeta(fds_volid_t volId, const std::string & blobName,
            blob_version_t * blobVersion, fds_uint64_t * blobSize,
            fpi::FDSP_MetaDataList * metaList) override;

    /**
     * Retrieves all info about the blob with given blobName and volume 'volId'
     * @param[in] volId volume uuid
     * @param[in] blobName name of the blob
     * @param[in] startOffset starting offset being queried
     * @param[in] endOffset end offset being queried
     * @param[in,out] blobVersion version of the blob to retrieve, if not set,
     *                the most recent version is retrieved. When the method returns,
     *                blobVersion is set to actual version that is retrieved
     * @param[out] metaList list of metadata key-value pairs
     * @param[out] objList list of offset to object id mappings
     * @param[out] blobSize ptr to blob size in bytes
     * @return ERR_OK on success, ERR_VOL_NOT_FOUND if volume is not known
     */
    Error getBlob(fds_volid_t volId, const std::string& blobName, fds_uint64_t startOffset,
            fds_int64_t endOffset, blob_version_t* blobVersion, fpi::FDSP_MetaDataList* metaList,
                  fpi::FDSP_BlobObjectList* objList, fds_uint64_t* blobSize) override;

    /**
     * Returns blob (descriptor + offset to object_id mappings) for a blob_id
     * intended to be used for logical replication
     * @param[in] volume_id volume uuid
     * @param[in] blob_id id of the blob
     * @param[out] meta the blob metadata descriptor
     * @param[out] obj_list list of offset to object id mappings
     * @param[in] m the snapshot to read from
     * @return ERR_OK on success; ERR_VOL_NOT_FOUND is volume is not known
     * to volume catalog
     */
    Error getBlobAndMetaFromSnapshot(fds_volid_t volume_id,
                                     const std::string & blobName,
                                     BlobMetaDesc &meta,
                                     fpi::FDSP_BlobObjectList& obj_list,
                                     const Catalog::MemSnap snap) override;

    /**
     * Returns the list of blobs in the volume with basic blob info
     * @param[out] binfoList list of blobs
     * @return ERR_OK on success, ERR_VOL_NOT_FOUND if volume is not known
     */
    Error listBlobs(fds_volid_t volId, fpi::BlobDescriptorListType* bdescrList) override;

    Error listBlobsWithPrefix (fds_volid_t volId,
                               std::string const& prefix,
                               std::string const& delimiter,
                               fpi::BlobDescriptorListType& results,
                               std::vector<std::string>& skippedPrefixes) override;


    /**
     * Updates committed blob in the Volume Catalog.
     */
    Error putBlobMeta(fds_volid_t volId, const std::string& blobName,
            const MetaDataList::const_ptr& metaList,
            const BlobTxId::const_ptr& txId, const sequence_id_t seq_id);
    Error putBlob(fds_volid_t volId, const std::string& blobName,
            const MetaDataList::const_ptr& metaList,
            const BlobObjList::const_ptr& blobObjList,
            const BlobTxId::const_ptr& txId, const sequence_id_t seq_id);
    Error putBlob(fds_volid_t volId, const std::string& blobName, fds_uint64_t blobSize,
            const MetaDataList::const_ptr& metaList,
            CatWriteBatch & wb, const sequence_id_t seq_id, bool truncate = true);

    /**
     * Flushes given blob to the persistent storage. Blocking method, will
     * return when whole blob is flushed to persistent storage.
     */
    Error flushBlob(fds_volid_t volId, const std::string& blobName);

    /**
     * Deletes each blob in the volume and marks volume as deleted.
     */
    Error removeVolumeMeta(fds_volid_t volId);

    /**
     * Deletes the blob 'blobName' verison 'blobVersion'.
     * If the blob version is invalid, deletes the most recent blob version.
     */
    Error deleteBlob(fds_volid_t volId, const std::string& blobName,
            blob_version_t blobVersion,
            bool const expunge_data = true) override;

    /**
     * Sync volume catalog to DM 'dmUuid'
     */
    Error syncCatalog(fds_volid_t volId, const NodeUuid& dmUuid) override;

    /**
     * insert blob descriptors into catalog during static migration, bypassing
     * the commit log.
     * NOTE: do NOT use for any data path operation.
     */
    Error migrateDescriptor(fds_volid_t volId,
                            const std::string& blobName,
                            const std::string& blobData,
                            VolumeMeta& volMeta);

    /**
     * Get total matadata size for a volume
     */
     fds_uint64_t getTotalMetadataSize(fds_volid_t volId) override {
         // TODO(umesh): implement this
         return 0;
     }

    Error getVolumeSequenceId(fds_volid_t volId, sequence_id_t& seq_id);

    Error getAllBlobsWithSequenceId(fds_volid_t volId, std::map<std::string, int64_t>& blobsSeqId,
														const Catalog::MemSnap snap);

    Error getAllBlobsWithSequenceId(fds_volid_t volId, std::map<std::string, int64_t>& blobsSeqId,
														const Catalog::MemSnap snap,
														const fds_bool_t &abortFlag);

    DmPersistVolCat::ptr getVolume(fds_volid_t volId);

    Error getBlobMetaDesc(fds_volid_t volId, const std::string & blobName,
            BlobMetaDesc & blobMeta);

    /**
     * Takes a snapshot and returns a pointer to the snapshot for
     * further diff, operations.
     * This is used for migrations, etc.
     * Caller MUST free the snapshot once done with it using freeInMemorySnapshot below.
     */
    Error getVolumeSnapshot(fds_volid_t volId, Catalog::MemSnap &snap);

    /**
     * Given a volume snapshot within opts, delete the snapshot.
     */
    Error freeVolumeSnapshot(fds_volid_t volId, Catalog::MemSnap &snap);

    Error forEachObject(fds_volid_t volId, std::function<void(const ObjectID&)>);

    Error putObject(fds_volid_t volId, const std::string & blobName, const BlobObjList & objs);

    Error getVersion(fds_volid_t volId, int32_t &version) override;
  private:

    // vars
    std::unordered_map<fds_volid_t, DmPersistVolCat::ptr> volMap_;
    fds_mutex volMapLock_;

    expunge_objs_cb_t expungeCb_;

    bool _ft_newStats;

    // methods
    Error statVolumeInternal(fds_volid_t volId, fds_uint64_t * volSize,
                             fds_uint64_t * blobCount, fds_uint64_t * objCount,
                             sequence_id_t * maxSeqId);

    inline void mergeMetaList(MetaDataList & lhs, const MetaDataList & rhs) {
        for (auto & it : rhs) {
            lhs[it.first] = it.second;
        }
    }

    void _updateLBytes (fds_volid_t volId);
};
}  // namespace fds
#endif  // SOURCE_DATA_MGR_INCLUDE_DM_VOL_CAT_DMVOLUMECATALOG_H_
