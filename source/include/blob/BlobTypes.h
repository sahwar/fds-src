/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_INCLUDE_BLOB_BLOBTYPES_H_
#define SOURCE_INCLUDE_BLOB_BLOBTYPES_H_

#include <fds_types.h>
#include <string>
#include <list>
#include <unordered_map>
#include <utility>

namespace fds {

/**
 * Describes an aligned offset within a blob.
 */
class BlobOffsetPair :
    private std::pair<std::string, fds_uint64_t>
{
    mutable size_t hash {0};
  public:
    typedef boost::shared_ptr<BlobOffsetPair> ptr;
    typedef boost::shared_ptr<const BlobOffsetPair> const_ptr;

    BlobOffsetPair(const std::string &a, const fds_uint64_t &b)
            : std::pair<std::string, fds_uint64_t>(a, b) {
    }

    bool operator==(const BlobOffsetPair& rhs) const {
        if (rhs.second != second) return false;
        if (rhs.first != first) return false;
        return true;
    }

    std::string getName() const { return first; }

    fds_uint64_t getOffset() const { return second; }

    size_t getHash() const {
        if (0 == hash) {
            hash = std::hash<std::string>()(first + std::to_string(second));
        }
        return hash;
    }

    friend std::ostream& operator<<(std::ostream& out,
                                    const BlobOffsetPair& blobOffset);
};

class BlobOffsetPairHash {
  public:
    size_t operator()(const BlobOffsetPair &blobOffset) const {
        return blobOffset.getHash();
    }
};

/**
 * Describes a specific blob transaction
 */
class BlobTxId {
  private:
    fds_uint64_t txId;

  public:
    /// Creates a new blob transaction ID with invalid value
    BlobTxId() : txId(txIdInvalid) {}
    /// Creates a new blob transaction with a specific value
    explicit BlobTxId(fds_uint64_t givenId) : txId(givenId) {}
    explicit BlobTxId(const BlobTxId &rhs) = default;
    ~BlobTxId() = default;
    typedef boost::shared_ptr<BlobTxId> ptr;
    typedef boost::shared_ptr<const BlobTxId> const_ptr;

    static constexpr fds_uint64_t txIdInvalid = 0;

    BlobTxId& operator=(const BlobTxId& rhs)
    { txId = rhs.txId; return *this; }

    fds_bool_t operator==(const BlobTxId& rhs) const
    { return txId == rhs.txId; }

    fds_bool_t operator!=(const BlobTxId& rhs) const
    { return txId != rhs.txId; }

    friend std::ostream& operator<<(std::ostream& out, const BlobTxId& txId);

    fds_uint64_t getValue() const
    { return txId; }
};

/// Provides hashing function for trans id
class BlobTxIdHash {
  public:
    size_t operator()(const BlobTxId &id) const {
        return std::hash<fds_uint64_t>()(id.getValue());
    }
};

class BlobTxIdPtrHash {
  public:
    size_t operator()(const BlobTxId::ptr id) const {
        return BlobTxIdHash()(*id);
    }
};

static const BlobTxId blobTxIdInvalid(BlobTxId::txIdInvalid);

std::ostream& operator<<(std::ostream& out, const BlobTxId& txId);

/**
 * List structure containing const blob transaction ID ptrs
 */
class BlobTxList : public std::list<BlobTxId::const_ptr> {
  public:
    typedef boost::shared_ptr<BlobTxList> ptr;
    typedef boost::shared_ptr<const BlobTxList> const_ptr;
};

}  // namespace fds

namespace std {
template<>
struct hash<fds::BlobTxId> {
    std::size_t operator()(const fds::BlobTxId & key) const {
        return std::hash<fds_uint64_t>()(key.getValue());
    }
};
} /* namespace std */

#endif  // SOURCE_INCLUDE_BLOB_BLOBTYPES_H_
