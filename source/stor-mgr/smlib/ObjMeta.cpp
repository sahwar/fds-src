
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <algorithm>

#include <fdsp_utils.h>
#include <ObjMeta.h>

namespace fds {

SyncMetaData::SyncMetaData()
{
    reset();
}

/**
 * Resets sync metadata
 */
void SyncMetaData::reset()
{
    born_ts = 0;
    mod_ts = 0;
    assoc_entries.clear();
}

/**
 * Serialization routine
 * @param serializer
 * @return
 */
uint32_t SyncMetaData::write(serialize::Serializer* serializer) const
{
    uint32_t sz = 0;
    sz += serializer->writeI64((int64_t&)born_ts);
    sz += serializer->writeI64((int64_t&)mod_ts);
    sz += serializer->writeVector(assoc_entries);
    return sz;
}

/**
 * Deserialization routine
 * @param deserializer
 * @return
 */
uint32_t SyncMetaData::read(serialize::Deserializer* deserializer)
{
    uint32_t sz = 0;
    sz += deserializer->readI64((int64_t&)born_ts);
    sz += deserializer->readI64((int64_t&)mod_ts);
    sz += deserializer->readVector(assoc_entries);
    return sz;
}

/**
 * Return the the size of serialization buffer
 * @return
 */
uint32_t SyncMetaData::getEstimatedSize() const
{
    uint32_t sz = 0;
    sz += sizeof(born_ts) +
            sizeof(mod_ts) +
            serialize::getEstimatedSize(assoc_entries);
    return sz;
}

bool SyncMetaData::operator==(const SyncMetaData &rhs) const
{
    if (born_ts == rhs.born_ts && mod_ts == rhs.mod_ts &&
        assoc_entries.size() == rhs.assoc_entries.size()) {
        return (memcmp(assoc_entries.data(),
                rhs.assoc_entries.data(),
                sizeof(obj_assoc_entry_t) * assoc_entries.size()) == 0);
    }
    return false;
}

ObjMetaData::ObjMetaData()
{
    mask = 0;

    memset(&obj_map, 0, sizeof(obj_map));

    phy_loc = &obj_map.loc_map[0];
    phy_loc[diskio::flashTier].obj_tier = -1;
    phy_loc[diskio::diskTier].obj_tier = -1;
    phy_loc[3].obj_tier = -1;
}

ObjMetaData::ObjMetaData(const ObjectBuf& buf) {
    deserializeFrom(buf);
}

ObjMetaData::~ObjMetaData()
{
}

/**
 *
 * @param objid
 * @param obj_size
 */
void ObjMetaData::initialize(const ObjectID& objid, fds_uint32_t obj_size) {
    memcpy(&obj_map.obj_id.metaDigest, objid.GetId(), sizeof(obj_map.obj_id.metaDigest));
    obj_map.obj_size = obj_size;

    //Initialize the physical location array
    phy_loc = &obj_map.loc_map[0];
    phy_loc[diskio::flashTier].obj_tier = -1;
    phy_loc[diskio::diskTier].obj_tier = -1;
    phy_loc[3].obj_tier = -1;
}

/**
 *
 * @param buf
 */
void ObjMetaData::unmarshall(const ObjectBuf& buf) {
    deserializeFrom(buf);
}
/**
 * Serialization routine
 * @param serializer
 * @return
 */
uint32_t ObjMetaData::write(serialize::Serializer* serializer) const
{
    uint32_t sz = 0;
    sz += serializer->writeByte(static_cast<int8_t>(mask));

    sz += serializer->writeBuffer(reinterpret_cast<int8_t*>(
            const_cast<meta_obj_map_t*>(&obj_map)),
            sizeof(obj_map));

    fds_verify(obj_map.obj_num_assoc_entry == assoc_entry.size());
    sz += serializer->writeVector(assoc_entry);

    if (syncDataExists()) {
        sz += sync_data.write(serializer);
    }
    fds_assert(sz == getEstimatedSize());
    return sz;
}

/**
 * Deserialization routine
 * @param deserializer
 * @return
 */
uint32_t ObjMetaData::read(serialize::Deserializer* deserializer)
{
    uint32_t sz = 0;
    sz += deserializer->readByte((int8_t&)mask);

    uint32_t nread = deserializer->\
            readBuffer(reinterpret_cast<int8_t*>(&obj_map),sizeof(obj_map));
    fds_assert(nread == sizeof(obj_map));
    sz += nread;

    phy_loc = &obj_map.loc_map[0];

    sz += deserializer->readVector(assoc_entry);
    fds_assert(assoc_entry.size() == obj_map.obj_num_assoc_entry);

    if (syncDataExists()) {
        sz += sync_data.read(deserializer);
    }

    fds_assert(sz == getEstimatedSize());
    return sz;
}

/**
 * Serialziation buffer size is returned here.
 * @return
 */
uint32_t ObjMetaData::getEstimatedSize() const
{
    uint32_t sz = sizeof(mask) +
            sizeof(obj_map) +
            serialize::getEstimatedSize(assoc_entry);

    if (syncDataExists()) {
        sz += sync_data.getEstimatedSize();
    }

    return sz;
}

/**
 *
 * @param buf
 * @return
 */
uint32_t ObjMetaData::serializeTo(ObjectBuf& buf) const
{
    bool ret = getSerialized(buf.data);
    fds_assert(ret == true);
    buf.size = buf.data.size();
    return buf.size;
}

/**
 *
 * @param buf
 * @return
 */
bool ObjMetaData::deserializeFrom(const ObjectBuf& buf)
{
    bool ret = loadSerialized(buf.data);
    fds_assert(ret == true);
    return ret;
}

/**
 *
 * @param s
 * @return
 */
bool ObjMetaData::deserializeFrom(const leveldb::Slice& s)
{
    // TODO(Rao): Avoid the extra copy from s.ToString().  Have
    // leveldb expose underneath string
    bool ret = loadSerialized(s.ToString());
    fds_assert(ret == true);
    return ret;
}

/**
 *
 * @return
 */
uint64_t ObjMetaData::getModificationTs() const
{
    return obj_map.assoc_mod_time;
}

/**
 *
 * @return
 */
fds_uint32_t   ObjMetaData::getObjSize() const
{
    return obj_map.obj_size;
}

/**
 *
 * @param tier
 * @return
 */
obj_phy_loc_t*   ObjMetaData::getObjPhyLoc(diskio::DataTier tier) {
    return &phy_loc[tier];
}

/**
 *
 * @return
 */
meta_obj_map_t*   ObjMetaData::getObjMap() {
    return &obj_map;
}

/**
 *
 * @param refcnt
 */
void ObjMetaData::setRefCnt(fds_uint16_t refcnt) {
    obj_map.obj_refcnt = refcnt;
}

/**
 *
 */
void ObjMetaData::incRefCnt() {
    obj_map.obj_refcnt++;
}

/**
 *
 */
void ObjMetaData::decRefCnt() {
    obj_map.obj_refcnt--;
}
/**
 * Updates volume association entry
 * @param objId
 * @param vol_id
 */
void ObjMetaData::updateAssocEntry(ObjectID objId, fds_volid_t vol_id) {
    fds_assert(obj_map.obj_num_assoc_entry == assoc_entry.size());
    for(int i = 0; i < obj_map.obj_num_assoc_entry; i++) {
        if (vol_id == assoc_entry[i].vol_uuid) {
            assoc_entry[i].ref_cnt++;
            obj_map.obj_refcnt++;
            return;
        }
    }
    obj_assoc_entry_t new_association;
    new_association.vol_uuid = vol_id;
    new_association.ref_cnt = 1;
    obj_map.obj_refcnt++;
    assoc_entry.push_back(new_association);
    obj_map.obj_num_assoc_entry = assoc_entry.size();

}

/**
 * Delete volumes association
 * @param objId
 * @param vol_id
 */
void ObjMetaData::deleteAssocEntry(ObjectID objId, fds_volid_t vol_id) {
    fds_assert(obj_map.obj_num_assoc_entry == assoc_entry.size());
    for(int i = 0; i < obj_map.obj_num_assoc_entry; i++) {
        if (vol_id == assoc_entry[i].vol_uuid) {
            assoc_entry[i].ref_cnt--;
            obj_map.obj_refcnt--;
            return;
        }
    }
    // If Volume did not put this objId then it delete is a noop
}

/**
 *
 * @return
 */
fds_bool_t ObjMetaData::onFlashTier() {
    if (phy_loc[diskio::flashTier].obj_tier == diskio::flashTier) {
        return true;
    }
    return false;
}

/**
 *
 * @param in_phy_loc
 */
void ObjMetaData::updatePhysLocation(obj_phy_loc_t *in_phy_loc) {
    memcpy(&phy_loc[(fds_uint32_t)in_phy_loc->obj_tier], in_phy_loc, sizeof(obj_phy_loc_t));
}

/**
 *
 * @param tier
 */
void ObjMetaData::removePhyLocation(diskio::DataTier tier) {
    phy_loc[tier].obj_tier = -1;
}
/**
 * Applies incoming data to metadata.
 * @param data
 */
void ObjMetaData::apply(const FDSP_MigrateObjectMetadata& data)
{
    fds_assert(!syncDataExists());

    /* Of sync metadata Object size and object id don't require a merge.
     * They can directly be applied to meta data. NOTE: If obj_map has
     * these fields set, incoming sync entry must match with existing.
     */
    if (obj_map.obj_size == 0) {
        obj_map.obj_size = data.obj_len;
        memcpy(obj_map.obj_id.metaDigest, data.object_id.digest.data(),
                sizeof(obj_map.obj_id.metaDigest));
    } else {
        fds_assert(obj_map.obj_size == static_cast<uint32_t>(data.obj_len));
        fds_assert(memcmp(obj_map.obj_id.metaDigest, data.object_id.digest.data(),
                sizeof(obj_map.obj_id.metaDigest)) == 0);
    }
    /* Creation time */
    obj_map.obj_create_time = data.born_ts;

    /* Modification time */
    obj_map.assoc_mod_time = data.modification_ts;

    /* association entries */
    assoc_entry.clear();
    obj_map.obj_refcnt = 0;
    for (auto itr : data.associations) {
        obj_assoc_entry_t e;
        e.ref_cnt = itr.ref_cnt;
        e.vol_uuid = itr.vol_id.uuid;
        assoc_entry.push_back(e);

        obj_map.obj_refcnt += itr.ref_cnt;
    }
    obj_map.obj_num_assoc_entry = assoc_entry.size();
}
/**
 * Extracts sync entry data.  Metadata that is node specific is skipped
 * @param md
 */
void ObjMetaData::extractSyncData(FDSP_MigrateObjectMetadata &md) const
{
    /* Object id */
    fds::assign(md.object_id, obj_map.obj_id);

    /* Object len */
    md.obj_len = static_cast<int32_t>(getObjSize());

    /* Born timestamp */
    md.born_ts = obj_map.obj_create_time;

    /* Modification timestamp */
    md.modification_ts = getModificationTs();

    if (obj_map.obj_refcnt == 0)  {
        return;
    }
    /* Association entries */
    fds_assert(obj_map.obj_num_assoc_entry == assoc_entry.size());
    for(uint32_t i = 0; i < obj_map.obj_num_assoc_entry; i++) {
        if (assoc_entry[i].ref_cnt > 0) {
            fds_assert(assoc_entry[i].vol_uuid != 0);
            FDSP_ObjectVolumeAssociation a;
            a.vol_id.uuid = assoc_entry[i].vol_uuid;
            a.ref_cnt = assoc_entry[i].ref_cnt;
            md.associations.push_back(a);
        }
    }
}

/**
 * While sync is in progress, existin metadata prior to sync point needs
 * to demoted as the sync data.  We do this process here.  While demoting
 * any replicable data is demoted to sync data.
 * @param syncTs
 */
void ObjMetaData::checkAndDemoteUnsyncedData(const uint64_t& syncTs)
{
    if (!syncDataExists() &&
            obj_map.assoc_mod_time != 0 &&
            obj_map.assoc_mod_time < syncTs) {
        /* sync_data will inherit fields from current metadata.  We will do
         * this one field at a time.
         * NOTE: For each field here, there should be an association resolve in
         * mergeNewAndUnsyncedData
         */
        mask |= SYNCMETADATA_MASK;

        /* Creation time */
        sync_data.born_ts = obj_map.obj_create_time;
        /* Modification time */
        sync_data.mod_ts = obj_map.assoc_mod_time;
        /* association entries */
        sync_data.assoc_entries = assoc_entry;

        /* Clear existing modification time.  NOTE: We keep the creation
         * time.  However, during merge we'll use creation time from replica
         */
        obj_map.assoc_mod_time = 0;
        /* Clear association information from existing metadata */
        obj_map.obj_refcnt = 0;
        obj_map.obj_num_assoc_entry = 0;
        assoc_entry.clear();
    }
}

/**
 * Applies data to sync metadata.  Normal metadata is unaffected.
 * @param data
 */
void ObjMetaData::applySyncData(const FDSP_MigrateObjectMetadata& data)
{
    fds_assert(syncDataExists());

    /* Of sync metadata Object size and object id don't require a merge.
     * They can directly be applied to meta data. NOTE: If obj_map has
     * these fields set, incoming sync entry must match with existing.
     */
    if (obj_map.obj_size == 0) {
        obj_map.obj_size = data.obj_len;
        memcpy(obj_map.obj_id.metaDigest, data.object_id.digest.data(),
                sizeof(obj_map.obj_id.metaDigest));
    } else {
        fds_assert(obj_map.obj_size == static_cast<uint32_t>(data.obj_len));
        fds_assert(memcmp(obj_map.obj_id.metaDigest, data.object_id.digest.data(),
                sizeof(obj_map.obj_id.metaDigest)) == 0);
    }

    fds_assert(sync_data.mod_ts <= static_cast<uint64_t>(data.modification_ts));

    /* Creation time */
    sync_data.born_ts = data.born_ts;

    /* Modification time */
    sync_data.mod_ts = data.modification_ts;

    /* association entries */
    sync_data.assoc_entries.clear();
    for (auto itr : data.associations) {
        obj_assoc_entry_t e;
        e.ref_cnt = itr.ref_cnt;
        e.vol_uuid = itr.vol_id.uuid;
        sync_data.assoc_entries.push_back(e);
    }
}

/**
 * Merges sync data with current metadata.  This is done after
 * sync transfer.
 */
void ObjMetaData::mergeNewAndUnsyncedData()
{
    fds_assert(syncDataExists());
    /* We will resolve one field at a time*/

    /* Creation timestamp.  We will honor the one from sync entry */
    obj_map.obj_create_time = sync_data.born_ts;

    /* Modification timestamp.  We'll use the recent of two */
    obj_map.assoc_mod_time = (sync_data.mod_ts > obj_map.assoc_mod_time) ?
            sync_data.mod_ts : obj_map.assoc_mod_time;

    /* Association entries.  We will merge these */
    mergeAssociationArrays_();

    /* Other adjustments/accounting of obj_map based on above changes */
    obj_map.obj_num_assoc_entry = assoc_entry.size();
    obj_map.obj_refcnt = 0;
    for (auto e : assoc_entry)  {
        obj_map.obj_refcnt += e.ref_cnt;
    }
    /* Sync data isn't needed anymore */
    sync_data.reset();
}

/**
 * Less than operator
 */
struct obj_assoc_entryLess {
    bool operator() (const obj_assoc_entry_t &a, const obj_assoc_entry_t& b)
    {
        return a.vol_uuid < b.vol_uuid;
    }
};

/**
 * Merges the sync association array with current metadata association array
 * This is done after sync transfer completes
 */
void ObjMetaData::mergeAssociationArrays_()
{
    if (sync_data.assoc_entries.size() == 0) {
        /* nothing to do */
        return;
    } else if (assoc_entry.size() == 0) {
        assoc_entry = sync_data.assoc_entries;
        return;
    }
    std::sort(sync_data.assoc_entries.begin(),
            sync_data.assoc_entries.end(), obj_assoc_entryLess());
    std::sort(assoc_entry.begin(), assoc_entry.end(), obj_assoc_entryLess());

    // TODO(Rao): consider doing a move here
    auto temp = assoc_entry;
    assoc_entry.clear();

    auto itr1 = sync_data.assoc_entries.begin();
    auto itr2 = temp.begin();
    while (itr1 != sync_data.assoc_entries.end() &&
           itr2 != temp.end()) {
        obj_assoc_entry_t e;
        if (itr1->vol_uuid == itr2->vol_uuid) {
            e = *itr1;
            e.ref_cnt += itr2->ref_cnt;
            itr1++;
            itr2++;
        } else if (itr1->vol_uuid < itr2->vol_uuid) {
            e = *itr1;
            itr1++;
        } else {
            e = *itr2;
            itr2++;
        }
        assoc_entry.push_back(e);
    }
    if (itr1 == sync_data.assoc_entries.end() &&
        itr2 == temp.end()) {
        return;
    } else if (itr1 != sync_data.assoc_entries.end()) {
        assoc_entry.insert(assoc_entry.end(), itr1, sync_data.assoc_entries.end());
    } else {
        assoc_entry.insert(assoc_entry.end(), itr2, temp.end());
    }
}

bool ObjMetaData::dataPhysicallyExists()
{
    if (phy_loc[diskio::diskTier].obj_tier == diskio::diskTier ||
            phy_loc[diskio::flashTier].obj_tier == diskio::flashTier) {
        return true;
    }
    return true;
}

void ObjMetaData::setSyncMask()
{
    mask |= SYNCMETADATA_MASK;
}

bool ObjMetaData::syncDataExists() const
{
    return (mask & SYNCMETADATA_MASK) != 0;
}

bool ObjMetaData::operator==(const ObjMetaData &rhs) const
{
    if (mask == rhs.mask && assoc_entry.size() == rhs.assoc_entry.size() &&
        (memcmp(assoc_entry.data(), rhs.assoc_entry.data(),
                sizeof(obj_assoc_entry_t) * assoc_entry.size()) == 0)) {
        if (syncDataExists()) {
            return (sync_data == rhs.sync_data);
        }
        return true;
    }
    return false;
}

#if 0
void newPersistentBuffer(int num_assoc_entries, bool sync_entry,
        int num_sync_assoc_entries)
{
    /* Allocate buffer */
    size = sizeof(uint8_t) + sizeof(meta_obj_map_t);
    size += sizeof(obj_assoc_entry_t) * num_assoc_entries;
    if (sync_entry) {
        size += sizeof(SyncMetaData::header_t);
        size += sizeof(obj_assoc_entry_t) * num_sync_assoc_entries;
    }

    persistBuffer = new char[size];
    uint32_t offset = 0;

    /* assign pointers */
    mask = (uint8_t*) (persistBuffer + offset);
    offset += sizeof(uint8_t);

    obj_map = (meta_obj_map_t*) (persistBuffer + offset);
    offset += sizeof(meta_obj_map_t);

    assoc_entry = nullptr;
    if (num_assoc_entries > 0) {
        assoc_entry = (obj_assoc_entry_t*) (persistBuffer + offset);
        offset += (sizeof(obj_assoc_entry_t) * num_assoc_entries);
    }

    sync_data.header = nullptr;
    sync_data.assoc_entries = nullptr;
    if (sync_entry) {
        sync_data.header = (SyncMetaData::header_t*) (persistBuffer + offset);
        offset += sizeof(SyncMetaData::header_t);
        if (num_sync_assoc_entries > 0) {
            sync_data.assoc_entries = (obj_assoc_entry_t*) (persistBuffer + offset);
            offset += (sizeof(obj_assoc_entry_t) * num_sync_assoc_entries);
        }
    }

    fds_assert(size == offset);
}
#endif
}  // namespace fds
