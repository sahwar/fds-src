/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <blob/BlobTypes.h>
#include <string>
#include <fds_uuid.h>

namespace fds {

BlobDescriptor::BlobDescriptor() {
}

BlobDescriptor::~BlobDescriptor() {
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

void
BlobDescriptor::setBlobName(const std::string &name) {
    blobName = name;
}

void
BlobDescriptor::setBlobSize(fds_uint64_t size) {
    blobSize = size;
}

void
BlobDescriptor::addKvMeta(const std::string &key,
                          const std::string &value) {
    blobKvMeta[key] = value;
}

BlobTxId::BlobTxId() {
    txId = txIdInvalid;
}

BlobTxId::BlobTxId(fds_uint64_t givenId)
        : txId(givenId) {
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
