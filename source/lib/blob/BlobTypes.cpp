/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <blob/BlobTypes.h>
#include <string>
#include <fds_uuid.h>

namespace fds {

BlobDescriptor::BlobDescriptor()
        : blobSize(0) {
}

BlobDescriptor::~BlobDescriptor() {
}

BlobDescriptor::BlobDescriptor(const BlobDescriptor::ptr &blobDesc)
        : blobName(blobDesc->blobName),
          blobVersion(blobDesc->blobVersion),
          volumeUuid(blobDesc->volumeUuid),
          blobSize(blobDesc->blobSize),
          blobKvMeta(blobDesc->blobKvMeta) {
}

BlobDescriptor::BlobDescriptor(const BlobDescriptor &blobDesc)
        : blobName(blobDesc.blobName),
          blobVersion(blobDesc.blobVersion),
          volumeUuid(blobDesc.volumeUuid),
          blobSize(blobDesc.blobSize),
          blobKvMeta(blobDesc.blobKvMeta) {
}

const_kv_iterator
BlobDescriptor::kvMetaBegin() const {
    return blobKvMeta.cbegin();
}

const_kv_iterator
BlobDescriptor::kvMetaEnd() const {
    return blobKvMeta.cend();
}

fds_uint64_t
BlobDescriptor::getBlobSize() const {
    return blobSize;
}

const std::string &
BlobDescriptor::getBlobName() const {
    return blobName;
}

fds_volid_t
BlobDescriptor::getVolId() const {
    return volumeUuid;
}

void
BlobDescriptor::setBlobName(const std::string &name) {
    blobName = name;
}

void
BlobDescriptor::setBlobSize(fds_uint64_t size) {
    blobSize = size;
}

void
BlobDescriptor::updateBlobSize(fds_uint64_t size) {
    blobSize += size;
}

void
BlobDescriptor::setVolId(fds_volid_t volId) {
    volumeUuid = volId;
}

void
BlobDescriptor::addKvMeta(const std::string &key,
                          const std::string &value) {
    blobKvMeta[key] = value;
}

std::ostream&
operator<<(std::ostream& out, const BlobDescriptor& blobDesc) {
    out << "Blob " << blobDesc.blobName << " size " << blobDesc.blobSize
        << " volume " << std::hex << blobDesc.volumeUuid << std::dec;
    return out;
}

BlobTxId::BlobTxId() {
    txId = txIdInvalid;
}

BlobTxId::BlobTxId(fds_uint64_t givenId)
        : txId(givenId) {
}

BlobTxId::BlobTxId(const BlobTxId &rhs)
        : txId(rhs.txId) {
}

BlobTxId::~BlobTxId() {
}

fds_uint64_t
BlobTxId::getValue() const {
    return txId;
}

BlobTxId&
BlobTxId::operator=(const BlobTxId& rhs) {
    txId = rhs.txId;
    return *this;
}

fds_bool_t
BlobTxId::operator==(const BlobTxId& rhs) const {
    return txId == rhs.txId;
}

fds_bool_t
BlobTxId::operator!=(const BlobTxId& rhs) const {
    return txId != rhs.txId;
}

std::ostream&
operator<<(std::ostream& out, const BlobTxId& txId) {
    return out << "0x" << std::hex << txId.txId << std::dec;
}

}  // namespace fds
