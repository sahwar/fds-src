/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_OBJ_META_STORMGR_H_
#define SOURCE_OBJ_META_STORMGR_H_


#include <unistd.h>
#include <utility>
#include <atomic>
#include <unordered_map>

#include <leveldb/db.h>

#include <fdsp/FDSP_types.h>
#include "stor_mgr_err.h"
#include <fds_volume.h>
#include <fds_types.h>
#include <odb.h>

#include <assert.h>
#include <iostream>
#include <util/Log.h>
#include "StorMgrVolumes.h"
#include "SmObjDb.h"
#include <persistent_layer/dm_service.h>
#include <persistent_layer/dm_io.h>
#include <fds_migration.h>

#include <fds_qos.h>
#include <fds_assert.h>
#include <fds_config.hpp>
#include <fds_stopwatch.h>

#include <ObjStats.h>
#include <serialize.h>

namespace fds {

#define SYNCMETADATA_MASK 0x1

struct SyncMetaData : public serialize::Serializable{
    SyncMetaData();
    void reset();

    /* Overrides from Serializable */
    virtual uint32_t write(serialize::Serializer* serializer) const override;
    virtual uint32_t read(serialize::Deserializer* deserializer) override;
    virtual uint32_t getEstimatedSize() const override;

    bool operator== (const SyncMetaData &rhs) const;

    /* Born timestamp */
    uint64_t born_ts;
    /* Modification timestamp */
    uint64_t mod_ts;
    /* Association entries */
    std::vector<obj_assoc_entry_t> assoc_entries;
};

/*
 * Persistent class for storing MetaObjMap, which
 * maps an object ID to its locations in the persistent
 * layer.
 */
class ObjMetaData : public serialize::Serializable {
private:

public:
    ObjMetaData();
    virtual ~ObjMetaData();

    void initialize(const ObjectID& objid, fds_uint32_t obj_size);

    bool isInitialized() const;

    ObjMetaData(const ObjectBuf& buf);

    void unmarshall(const ObjectBuf& buf);

    virtual uint32_t write(serialize::Serializer* serializer) const override;

    virtual uint32_t read(serialize::Deserializer* deserializer) override;

    virtual uint32_t getEstimatedSize() const override;

    uint32_t serializeTo(ObjectBuf& buf) const;

    bool deserializeFrom(const ObjectBuf& buf);

    bool deserializeFrom(const leveldb::Slice& s);

    uint64_t getModificationTs() const;

    void apply(const FDSP_MigrateObjectMetadata& data);

    void extractSyncData(FDSP_MigrateObjectMetadata &md) const;

    void checkAndDemoteUnsyncedData(const uint64_t& syncTs);

    void setSyncMask();

    bool syncDataExists() const;

    void applySyncData(const FDSP_MigrateObjectMetadata& data);

    void mergeNewAndUnsyncedData();

    bool dataPhysicallyExists();

    fds_uint32_t   getObjSize() const;
    obj_phy_loc_t*   getObjPhyLoc(diskio::DataTier tier);
    meta_obj_map_t*   getObjMap();

    void setRefCnt(fds_uint16_t refcnt);

    void incRefCnt();

    void decRefCnt();

    fds_uint16_t getRefCnt() const;

    void updateAssocEntry(ObjectID objId, fds_volid_t vol_id);

    void deleteAssocEntry(ObjectID objId, fds_volid_t vol_id, fds_uint64_t ts);

    fds_bool_t isVolumeAssociated(fds_volid_t vol_id);

    // Tiering/Physical Location update routines
    fds_bool_t onFlashTier();

    void updatePhysLocation(obj_phy_loc_t *in_phy_loc);
    void removePhyLocation(diskio::DataTier tier);

    bool operator==(const ObjMetaData &rhs) const;

    std::string logString() const;

private:
    void mergeAssociationArrays_();

    friend std::ostream& operator<<(std::ostream& out, const ObjMetaData& objMap);
    friend class SmObjDb;

    /* Data mask to indicate which metadata members are valid.
     * When sync is in progress mask will be set to indicate sync_data
     * is valid.
     */
    uint8_t mask;

    /* current object meta data */
    meta_obj_map_t obj_map;

    /* Physical location entries.  Pointer to field inside obj_map */
    obj_phy_loc_t *phy_loc;

    /* Volume association entries */
    std::vector<obj_assoc_entry_t> assoc_entry;

    /* Sync related metadata.  Valid only when mask is set appropriately */
    SyncMetaData sync_data;
};

inline std::ostream& operator<<(std::ostream& out, const ObjMetaData& objMd)
{
     out << "Object MetaData: Version" << objMd.obj_map.obj_map_ver
             << "  objId : " << objMd.obj_map.obj_id.metaDigest
             << "  obj_size " << objMd.obj_map.obj_size
             << "  obj_ref_cnt " << objMd.obj_map.obj_refcnt
             << "  num_assoc_entry " << objMd.obj_map.obj_num_assoc_entry
             << "  create_time " << objMd.obj_map.obj_create_time
             << "  del_time " << objMd.obj_map.obj_del_time
             << "  mod_time " << objMd.obj_map.assoc_mod_time
             << std::endl;
     for (fds_uint32_t i = 0; i < MAX_PHY_LOC_MAP; i++) {
         out << "Object MetaData: "
             << " Tier (" << objMd.phy_loc[i].obj_tier
             << ") loc id (" << objMd.phy_loc[i].obj_stor_loc_id
             << ") offset (" << objMd.phy_loc[i].obj_stor_offset
             << ") file id (" << objMd.phy_loc[i].obj_file_id << ")"
            << std::endl;
    }
    return out;
}

}  // namespace fds

#endif //  SOURCE_STOR_MGR_STORMGR_H_
