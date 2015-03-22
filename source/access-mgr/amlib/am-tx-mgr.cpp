/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <map>
#include <string>
#include <fds_process.h>
#include <am-tx-mgr.h>
#include "AmCache.h"

namespace fds {

AmTxDescriptor::AmTxDescriptor(fds_volid_t volUuid,
                               const BlobTxId &id,
                               fds_uint64_t dmtVer,
                               const std::string &name)
        : volId(volUuid),
          txId(id),
          originDmtVersion(dmtVer),
          blobName(name),
          opType(FDS_PUT_BLOB),
          stagedBlobDesc(new BlobDescriptor()) {
    stagedBlobDesc->setVolId(volId);
    stagedBlobDesc->setBlobName(blobName);
    stagedBlobDesc->setBlobSize(0);
    // TODO(Andrew): We're leaving the blob version unset
    // We'll need to revist when we do versioning
    // TODO(Andrew): We default the op type to PUT, but it's
    // conceivable to PUT and DELETE in the same transaction,
    // so we really want to mask the op type.
}

AmTxDescriptor::~AmTxDescriptor() {
}

AmTxManager::AmTxManager()
    : amCache(new AmCache()) {
    FdsConfigAccessor conf(g_fdsprocess->get_fds_config(), "fds.am.");
    maxStagedEntries = conf.get<fds_uint32_t>("cache.tx_max_staged_entries");
}

AmTxManager::~AmTxManager()
{ }

Error
AmTxManager::addTx(fds_volid_t volId,
                   const BlobTxId &txId,
                   fds_uint64_t dmtVer,
                   const std::string &name) {
    SCOPEDWRITE(txMapLock);
    if (txMap.count(txId) > 0) {
        return ERR_DUPLICATE_UUID;
    }
    txMap[txId] = std::make_shared<AmTxDescriptor>(volId, txId, dmtVer, name);

    return ERR_OK;
}

Error
AmTxManager::removeTx(const BlobTxId &txId) {
    SCOPEDWRITE(txMapLock);
    TxMap::iterator txMapIt = txMap.find(txId);
    if (txMapIt == txMap.end()) {
        return ERR_NOT_FOUND;
    }
    fds_verify(txId == txMapIt->second->txId);
    txMap.erase(txMapIt);
    return ERR_OK;
}

Error
AmTxManager::getTxDescriptor(const BlobTxId &txId, descriptor_ptr_type &desc) {
    SCOPEDWRITE(txMapLock);
    TxMap::iterator txMapIt = txMap.find(txId);
    if (txMapIt == txMap.end()) {
        return ERR_NOT_FOUND;
    }
    fds_verify(txId == txMapIt->second->txId);
    desc = txMapIt->second;
    return ERR_OK;
}

Error
AmTxManager::getTxDmtVersion(const BlobTxId &txId, fds_uint64_t *dmtVer) const {
    SCOPEDREAD(txMapLock);
    TxMap::const_iterator txMapIt = txMap.find(txId);
    if (txMapIt == txMap.end()) {
        return ERR_NOT_FOUND;
    }
    *dmtVer = txMapIt->second->originDmtVersion;
    return ERR_OK;
}

Error
AmTxManager::updateTxOpType(const BlobTxId &txId,
                            fds_io_op_t op) {
    SCOPEDWRITE(txMapLock);
    TxMap::iterator txMapIt = txMap.find(txId);
    if (txMapIt == txMap.end()) {
        return ERR_NOT_FOUND;
    }
    fds_verify(txId == txMapIt->second->txId);
    // TODO(Andrew): For now, we're only expecting to change
    // from PUT to DELETE
    fds_verify(txMapIt->second->opType == FDS_PUT_BLOB);
    fds_verify(op == FDS_DELETE_BLOB);
    txMapIt->second->opType = op;

    return ERR_OK;
}

Error
AmTxManager::updateStagedBlobOffset(const BlobTxId &txId,
                                    const std::string &blobName,
                                    fds_uint64_t blobOffset,
                                    const ObjectID &objectId) {
    SCOPEDWRITE(txMapLock);
    TxMap::iterator txMapIt = txMap.find(txId);
    if (txMapIt == txMap.end()) {
        return ERR_NOT_FOUND;
    }
    fds_verify(txId == txMapIt->second->txId);
    // TODO(Andrew): For now, we're only expecting to update
    // a PUT transaction
    fds_verify(txMapIt->second->opType == FDS_PUT_BLOB);

    BlobOffsetPair offsetPair(blobName, blobOffset);
    fds_verify(txMapIt->second->stagedBlobOffsets.count(offsetPair) == 0);

    if (txMapIt->second->stagedBlobOffsets.size() < maxStagedEntries) {
        txMapIt->second->stagedBlobOffsets[offsetPair] = objectId;
    }

    return ERR_OK;
}

Error
AmTxManager::updateStagedBlobObject(const BlobTxId &txId,
                                    const ObjectID &objectId,
                                    boost::shared_ptr<std::string> objectData) {
    SCOPEDWRITE(txMapLock);
    TxMap::iterator txMapIt = txMap.find(txId);
    if (txMapIt == txMap.end()) {
        return ERR_NOT_FOUND;
    }
    fds_verify(txId == txMapIt->second->txId);
    // TODO(Andrew): For now, we're only expecting to update
    // a PUT transaction
    fds_verify(txMapIt->second->opType == FDS_PUT_BLOB);

    if (txMapIt->second->stagedBlobObjects.size() < maxStagedEntries) {
        // Copy the data into the tx manager. This allows the
        // tx manager to hand off the ptr to the cache later.
        txMapIt->second->stagedBlobObjects[objectId] =
                objectData;
    }

    return ERR_OK;
}

Error
AmTxManager::updateStagedBlobDesc(const BlobTxId &txId,
                                  fpi::FDSP_MetaDataList const& metaDataList) {
    SCOPEDWRITE(txMapLock);
    TxMap::iterator txMapIt = txMap.find(txId);
    if (txMapIt == txMap.end()) {
        return ERR_NOT_FOUND;
    }
    fds_verify(txId == txMapIt->second->txId);
    // Verify that we're not overwriting previously staged metadata
    fds_verify(txMapIt->second->stagedBlobDesc->kvMetaBegin() ==
               txMapIt->second->stagedBlobDesc->kvMetaEnd());

    for (auto const & meta : metaDataList) {
        txMapIt->second->stagedBlobDesc->addKvMeta(meta.key, meta.value);
    }
    return ERR_OK;
}

Error
AmTxManager::addVolume(const VolumeDesc& volDesc)
{ return amCache->addVolume(volDesc); }

Error
AmTxManager::putTxDescriptor(const std::shared_ptr<AmTxDescriptor> txDesc, fds_uint64_t const blobSize)
{ return amCache->putTxDescriptor(txDesc, blobSize); }

BlobDescriptor::ptr
AmTxManager::getBlobDescriptor(fds_volid_t volId, const std::string &blobName, Error &error)
{ return amCache->getBlobDescriptor(volId, blobName, error); }

ObjectID::ptr
AmTxManager::getBlobOffsetObject(fds_volid_t volId, const std::string &blobName, fds_uint64_t blobOffset, Error &error)
{ return amCache->getBlobOffsetObject(volId, blobName, blobOffset, error); }

Error
AmTxManager::putObject(fds_volid_t const volId, ObjectID const& objId, boost::shared_ptr<std::string> const obj)
{ return amCache->putObject(volId, objId, obj); }

boost::shared_ptr<std::string>
AmTxManager::getBlobObject(fds_volid_t volId, const ObjectID &objectId, Error &error)
{ return amCache->getBlobObject(volId, objectId, error); }

Error
AmTxManager::putOffset(fds_volid_t const volId, BlobOffsetPair const& blobOff, boost::shared_ptr<ObjectID> const objId)
{ return amCache->putOffset(volId, blobOff, objId); }

Error
AmTxManager::putBlobDescriptor(fds_volid_t const volId, std::string const& blobName, boost::shared_ptr<BlobDescriptor> const blobDesc)
{ return amCache->putBlobDescriptor(volId, blobName, blobDesc); }

}  // namespace fds
