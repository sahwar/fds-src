/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <string>
#include <AmCache.h>
#include <fds_process.h>
#include <PerfTrace.h>

namespace fds {

AmCache::AmCache(const std::string &modName)
    :   Module(modName.c_str()),
        descriptor_cache("AM blob descriptor cache manager"),
        offset_cache("AM blob offset cache manager"),
        object_cache("AM blob object cache manager"),
        max_entries(500)
{
    FdsConfigAccessor conf(g_fdsprocess->get_fds_config(), "fds.am.");
    max_entries = conf.get<fds_uint32_t>("cache.default_max_entries");
}

Error
AmCache::createCache(const VolumeDesc& volDesc) {
    Error err = descriptor_cache.createCache(volDesc.volUUID, max_entries);
    if (err != ERR_OK) {
        return err;
    }
    err = offset_cache.createCache(volDesc.volUUID, max_entries);
    if (err != ERR_OK) {
        return err;
    }
    err = object_cache.createCache(volDesc.volUUID, max_entries);
    return err;
}

Error
AmCache::removeCache(fds_volid_t volId) {
    Error err = descriptor_cache.deleteCache(volId);
    if (err != ERR_OK) {
        return err;
    }
    err = offset_cache.deleteCache(volId);
    if (err != ERR_OK) {
        return err;
    }
    err = object_cache.deleteCache(volId);
    return err;
}

BlobDescriptor::ptr
AmCache::getBlobDescriptor(fds_volid_t volId,
                           const std::string &blobName,
                           Error &error) {
    LOGTRACE << "Cache lookup for volume " << std::hex << volId << std::dec
             << " blob " << blobName;

    BlobDescriptor::ptr blobDescPtr;
    error = descriptor_cache.get(volId, blobName, blobDescPtr);
    if (error == ERR_OK) {
        PerfTracer::incr(AM_DESC_CACHE_HIT, volId);
    }
    return blobDescPtr;
}

ObjectID::ptr
AmCache::getBlobOffsetObject(fds_volid_t volId,
                             const std::string &blobName,
                             fds_uint64_t blobOffset,
                             Error &error) {
    LOGTRACE << "Cache lookup for volume " << std::hex << volId << std::dec
             << " blob " << blobName << " offset " << blobOffset;

    ObjectID::ptr blobOffsetPtr;
    error = offset_cache.get(volId, BlobOffsetPair(blobName, blobOffset), blobOffsetPtr);
    if (error == ERR_OK) {
        PerfTracer::incr(AM_OFFSET_CACHE_HIT, volId);
    }
    return blobOffsetPtr;
}

boost::shared_ptr<std::string>
AmCache::getBlobObject(fds_volid_t volId,
                       const ObjectID &objectId,
                       Error &error) {
    LOGTRACE << "Cache lookup for volume " << std::hex << volId << std::dec
             << " object " << objectId;

    boost::shared_ptr<std::string> blobObjectPtr;
    error = object_cache.get(volId, objectId, blobObjectPtr);
    if (error == ERR_OK) {
        PerfTracer::incr(AM_OBJECT_CACHE_HIT, volId);
    }
    return blobObjectPtr;
}

Error
AmCache::putTxDescriptor(const AmTxDescriptor::ptr txDesc) {
    LOGTRACE << "Cache insert tx descriptor for volume " << std::hex
             << txDesc->volId << std::dec << " blob " << txDesc->blobName;
    Error err(ERR_OK);

    // If the transaction is a delete, we want to remove the cache entry
    if (txDesc->opType == FDS_DELETE_BLOB) {
        // Remove from blob caches
        err = removeBlob(txDesc->volId,
                         txDesc->blobName);
    } else {
        fds_verify(txDesc->opType == FDS_PUT_BLOB);
        // Add blob descriptor from tx to descriptor cache
        // TODO(Andrew): We copy now because the data given to cache
        // isn't actually shared. It needs its own copy.
        BlobDescriptor::ptr cacheDesc = txDesc->stagedBlobDesc;
        BlobDescriptor::ptr evictedDesc =
                descriptor_cache.add(cacheDesc->getVolId(), cacheDesc->getBlobName(), cacheDesc);
        if (evictedDesc != NULL) {
            LOGTRACE << "Evicted cached descriptor " << *evictedDesc;
        }

        // Add blob offsets from tx to offset cache
        for (const auto &offsetPair : txDesc->stagedBlobOffsets) {
            // TODO(Andrew): Allocate an objectId the cache can own.
            // We should change this to just take a pointer from the
            // transaction manager
            ObjectID::ptr cacheObjId = boost::make_shared<ObjectID>(offsetPair.second);
            ObjectID::ptr evictedObjId =
                    offset_cache.add(txDesc->volId,
                                         offsetPair.first,
                                         cacheObjId);
            if (evictedObjId != NULL) {
                LOGTRACE << "Evicted cached object id " << *evictedObjId;
            }
        }

        // Add blob objects from tx to object cache
        for (const auto &object : txDesc->stagedBlobObjects) {
            boost::shared_ptr<std::string> evictedObject =
                    object_cache.add(txDesc->volId, object.first, object.second);
            if (evictedObject) {
                LOGTRACE << "Evicted cached object data of size " << evictedObject->size();
            }
        }
    }
    return err;
}

Error
AmCache::putBlobDescriptor(fds_volid_t volId,
                           const std::string &blobName,
                           const BlobDescriptor::ptr blobDesc) {
    // Copy desc contents in structure owned by cache
    // TODO(Andrew): We copy now because the data given to cache
    // isn't actually shared. It needs its own copy.
    BlobDescriptor::ptr cacheDesc = boost::make_shared<BlobDescriptor>(blobDesc);
    BlobDescriptor::ptr evictedDesc = descriptor_cache.add(volId, blobName, cacheDesc);

    if (evictedDesc != NULL) {
        LOGTRACE << "Evicted cached descriptor " << *evictedDesc;
    }
    return ERR_OK;
}

Error
AmCache::removeBlob(fds_volid_t volId,
                    const std::string &blobName) {
    // Remove from blob descriptor cache
    Error err = descriptor_cache.remove(volId, blobName);

    // Remove from blob offset cache
    // TODO(Andrew): Need to be able to delete from the offset cache
    // using just blob name or using iterator

    // Remove from blob object cache
    // TODO(Andrew): Need to associate blobs to objects in the cache,
    // though for now it's safe to leave object IDs in the cache as
    // they won't be read if the blob offset cache is cleared on delete
    return err;
}

}  // namespace fds
