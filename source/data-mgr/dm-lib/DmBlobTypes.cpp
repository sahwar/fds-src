/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <string>
#include <map>
#include <limits>
#include <DmBlobTypes.h>

namespace fds {

//
// Constructs emty metadata list
//
MetaDataList::MetaDataList() {
}

//
// Constructs metadata list from fdsp metadata list
//
MetaDataList::MetaDataList(const fpi::FDSP_MetaDataList& mlist) {
    for (fds_uint32_t i = 0; i < mlist.size(); ++i) {
        (*this)[mlist[i].key] = mlist[i].value;
    }
}

MetaDataList::~MetaDataList() {
}

//
// Updates metadata value for the given key, or if key does not exist
// adds new key-value pair
//
void MetaDataList::updateMetaDataPair(const std::string& key,
                                      const std::string& value) {
    (*this)[key] = value;
}

//
// Copies metadata into fdsp metadata list structure
//
void MetaDataList::toFdspPayload(fpi::FDSP_MetaDataList& mlist) const {
    mlist.clear();
    for (const_iter cit = (*this).cbegin();
         cit != (*this).cend();
         ++cit) {
        fpi::FDSP_MetaDataPair mpair;
        mpair.key = cit->first;
        mpair.value = cit->second;
        mlist.push_back(mpair);
    }
}

MetaDataList & MetaDataList::merge(const MetaDataList & rhs) {
    for (auto elem : rhs) {
        (*this)[elem.first] = elem.second;
    }

    return *this;
}

uint32_t MetaDataList::write(serialize::Serializer* s) const {
    uint32_t bytes = 0;
    uint32_t size = (*this).size();
    bytes += s->writeI32(size);
    for (const_iter cit = (*this).cbegin();
         cit != (*this).cend();
         ++cit) {
        bytes += s->writeString(cit->first);
        bytes += s->writeString(cit->second);
    }
    return bytes;
}

uint32_t MetaDataList::read(serialize::Deserializer* d) {
    uint32_t bytes = 0;
    uint32_t size = 0;
    (*this).clear();
    bytes += d->readI32(size);
    for (fds_uint32_t i = 0; i < size; ++i) {
        std::string key;
        std::string value;
        bytes += d->readString(key);
        bytes += d->readString(value);
        (*this)[key] = value;
    }
    return bytes;
}

std::ostream& operator<<(std::ostream& out, const MetaDataList& metaList) {
    out << "Metadata: ";
    if (metaList.size() == 0) {
        out << "[empty]\n";
        return out;
    }

    for (MetaDataList::const_iter cit = metaList.cbegin();
         cit != metaList.cend();
         ++cit) {
        out << "[" << cit->first << ":" << cit->second << "]";
    }
    return out;
}

//---------   BlobMetaDesc implementation   -----------//


/**
 * Constructs invalid version of a blob meta
 * until someone else actually assigns valid data
 */
BlobMetaDesc::BlobMetaDesc() {
    version = blob_version_invalid;
    vol_id = invalid_vol_id;
    blob_size = 0;
}

BlobMetaDesc::~BlobMetaDesc() {
}

uint32_t BlobMetaDesc::write(serialize::Serializer* s) const {
    uint32_t bytes = 0;
    bytes += s->writeString(blob_name);
    bytes += s->writeI64(vol_id);
    bytes += s->writeI64(version);
    bytes += s->writeI64(blob_size);
    bytes += meta_list.write(s);
    return bytes;
}

uint32_t BlobMetaDesc::read(serialize::Deserializer* d) {
    uint32_t bytes = 0;
    blob_name.clear();
    bytes += d->readString(blob_name);
    bytes += d->readI64(reinterpret_cast<int64_t&>(vol_id));
    bytes += d->readI64(version);
    bytes += d->readI64(blob_size);
    bytes += meta_list.read(d);
    return bytes;
}

std::ostream& operator<<(std::ostream& out, const BlobMetaDesc& blobMetaDesc) {
    out << "BlobMeta: "
        << "name " << blobMetaDesc.blob_name
        << ", volume id " << std::hex << blobMetaDesc.vol_id << std::dec
        << ", version " << blobMetaDesc.version
        << ", size " << blobMetaDesc.blob_size << " bytes; ";
    out << blobMetaDesc.meta_list;
    return out;
}

//----------- BlobObjInfo implementation ---------------//

BlobObjInfo::BlobObjInfo()
        : size(0) {
}

BlobObjInfo::BlobObjInfo(const ObjectID& obj_id, fds_uint64_t sz)
        : oid(obj_id), size(sz) {
}

BlobObjInfo::BlobObjInfo(const fpi::FDSP_BlobObjectInfo& obj_info) {
    oid.SetId((const char*)obj_info.data_obj_id.digest.c_str(),
              obj_info.data_obj_id.digest.length());
    size = obj_info.size;
}

BlobObjInfo::~BlobObjInfo() {
}

BlobObjInfo& BlobObjInfo::operator= (const BlobObjInfo &rhs) {
    oid = rhs.oid;
    size = rhs.size;
    return *this;
}

uint32_t BlobObjInfo::write(serialize::Serializer* s) const {
    uint32_t bytes = 0;
    bytes += oid.write(s);
    bytes += s->writeI64(size);
    return bytes;
}

uint32_t BlobObjInfo::read(serialize::Deserializer* d) {
    uint32_t bytes = 0;
    bytes += oid.read(d);
    bytes += d->readI64(size);
    return bytes;
}


//----------- BlobObjList implementation ---------------//

BlobObjList::BlobObjList() {
    end_of_blob = false;
}

BlobObjList::BlobObjList(const fpi::FDSP_BlobObjectList& blob_obj_list) {
    end_of_blob = false;
    for (fds_uint32_t i = 0; i < blob_obj_list.size(); ++i) {
        if (blob_obj_list[i].size == 0) {
            BlobObjInfo obj_info(NullObjectID, 0);
            (*this)[blob_obj_list[i].offset] = obj_info;
        } else {
            BlobObjInfo obj_info(blob_obj_list[i]);
            (*this)[blob_obj_list[i].offset] = obj_info;
        }
    }
}

BlobObjList::~BlobObjList() {
}

//
// Merge itself with another blob object list and return itself
//
BlobObjList& BlobObjList::merge(const BlobObjList& newer_blist){
    // once end_of_blob is set, do not allow merges
    fds_verify(end_of_blob == false);
    for (const_iter cit = newer_blist.cbegin();
         cit != newer_blist.cend();
         ++cit) {
        (*this)[cit->first] = cit->second;
    }
    return *this;
}

//
// Returns last offset in the obj list if end_of_blob is set
// and there is at least on object in the list;
// otherwise returns max uint64 value
//
fds_uint64_t BlobObjList::lastOffset() const {
    std::map<fds_uint64_t, BlobObjInfo>::const_reverse_iterator crit;
    if (!end_of_blob || size() == 0)
        return std::numeric_limits<fds_uint64_t>::max();

    crit = crbegin();
    fds_verify(crit != crend());
    return crit->first;
}

//
// Merge itself with one object
//
void BlobObjList::updateObject(fds_uint64_t offset,
                               const ObjectID& oid,
                               fds_uint64_t size) {
    BlobObjInfo obj_info(oid, size);
    (*this)[offset] = obj_info;
}

//
// Verifies each obj is max size; if end_of_blob is set
// last object can be less than max size; also that each offset
// is aligned.
//
void BlobObjList::verify(fds_uint32_t max_obj_size) const {
    std::map<fds_uint64_t, BlobObjInfo>::const_reverse_iterator crit;
    crit = crbegin();
    if (crit == crend()) return;

    fds_verify(end_of_blob || (crit->second).size == max_obj_size);
    fds_verify((crit->first % max_obj_size) == 0);
    ++crit;
    while (crit != crend()) {
        fds_verify((crit->second).size == max_obj_size);
        fds_verify((crit->first % max_obj_size) == 0);
        ++crit;
    }
}

//
// fills in blob_obj_list
//
void BlobObjList::toFdspPayload(fpi::FDSP_BlobObjectList& blob_obj_list) const {
    blob_obj_list.clear();
    for (const_iter cit = cbegin();
         cit != cend();
         ++cit) {
        fpi::FDSP_BlobObjectInfo obj_info;
        obj_info.offset = cit->first;
        obj_info.size = (cit->second).size;
        obj_info.data_obj_id.digest = std::string((const char *)((cit->second).data_obj_id.GetId()),
                                                  (size_t)(cit->second).data_obj_id.GetLen());
        ob_info.blob_end = false;  // assume we are not using it in resp
        blob_obj_list.push_back(obj_info);
    }
}

uint32_t BlobObjList::write(serialize::Serializer* s) const {
    uint32_t bytes = 0;
    bytes += s->writeI32(end_of_blob);
    bytes += s->writeI32(size());
    for (const_iter cit = cbegin();
         cit != cend();
         ++cit) {
        bytes += s->writeI64(cit->first);
        bytes += (cit->second).write(s);
    }
    return bytes;
}

uint32_t BlobObjList::read(serialize::Deserializer* d) {
    uint32_t bytes = 0;
    uint32_t sz = 0;
    clear();
    bytes += d->readI32(reinterpret_cast<int32_t&>(end_of_blob));
    bytes += d->readI32(sz);
    for (fds_uint32_t i = 0; i < sz; ++i) {
        fds_uint64_t offset;
        BlobObjInfo obj_info;
        bytes += d->readI64(offset);
        bytes += obj_info.read(d);
        (*this)[offset] = obj_info;
    }
    return bytes;
}

std::ostream& operator<<(std::ostream& out, const BlobObjList& obj_list) {
    out << "Object list \n";
    for (BlobObjList::const_iter cit = obj_list.cbegin();
         cit != obj_list.cend();
         ++cit) {
        out << "[offset " << cit->first << " -> " << (cit->second).oid
            << "," << (cit->second).size << "]\n";
    }
    out << " End of blob? " << obj_list.endOfBlob() << "\n";
    return out;
}


//---------------- BlobUtil methods -----------------------------------------//

void BlobUtil::toFDSPQueryCatalogMsg(const BlobMetaDesc::const_ptr& blob_meta_desc,
                                     const BlobObjList::const_ptr& blob_obj_list,
                                     fds_uint32_t max_obj_size_bytes,
                                     fpi::FDSP_QueryCatalogTypePtr& query_msg) {
    query_msg->blob_name = blob_meta_desc->blob_name;
    query_msg->blob_size = blob_meta_desc->blob_size;
    query_msg->blob_version = blob_meta_desc->version;
    (blob_meta_desc->meta_list).toFdspPayload(query_msg->meta_list);

    // fill in blob_obj_list
    (query_msg->obj_list).clear();
    for (BlobObjList::const_iter cit = blob_obj_list->cbegin();
         cit != blob_obj_list->cend();
         ++cit) {
        fpi::FDSP_BlobObjectInfo obj_info;
        obj_info.offset = cit->first;
        obj_info.data_obj_id.digest =
                std::string((const char *)((cit->second).oid.GetId()),
                            (size_t)(cit->second).oid.GetLen());
        obj_info.size = max_obj_size_bytes;
        if ((query_msg->obj_list).size() == (blob_obj_list->size() - 1)) {
            // this is the last object, object size = blob size % max obj size
            obj_info.size = blob_meta_desc->blob_size % max_obj_size_bytes;
        }
        (query_msg->obj_list).push_back(obj_info);
    }
}

}  // namespace fds
