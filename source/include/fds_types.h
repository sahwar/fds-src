/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

/*
 * Object database class. The object database is a key-value store
 * that provides local objec storage.
 */
#ifndef SOURCE_INCLUDE_FDS_TYPES_H_
#define SOURCE_INCLUDE_FDS_TYPES_H_

#include <stdint.h>
#include <stdio.h>
#include <limits>

#include <sstream>
#include <iostream>  // NOLINT(*)
#include <string>
#include <cstring>
#include <iomanip>
#include <ios>
#include <functional>
#include <atomic>
#include <shared/fds_types.h>
#include <serialize.h>
#include <fds_assert.h>
#include <PerfTrace.h>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

/*
 * Consider encapsulating in the global
 * fds namespace.
 */

/*
 * UDP & TCP port numbers reserver for
 * StorMgr and DataMgr servers.
 */
#define FDS_CLUSTER_TCP_PORT_SM         6900
#define FDS_CLUSTER_TCP_PORT_DM         6901
#define FDS_CLUSTER_UDP_PORT_SM         9600
#define FDS_CLUSTER_UDP_PORT_DM         9601

/**
 * In-memory representation of an object ID
 */
namespace fds {

typedef fds_int64_t fds_volid_t;
typedef fds_int64_t VolumeId;
typedef fds_int64_t fds_qid_t;  // type for queue id

/**
 * Represent an offset.
 */
typedef fds_uint64_t fds_off_t;

/**
 * A token id identifies a particular token
 * region in a DLT routing table.
 */
typedef fds_uint32_t fds_token_id;

/**
 * A blob version identifies a unique
 * version instance of a blob.
 */
typedef fds_uint64_t blob_version_t;

typedef std::atomic<fds_uint64_t> fds_atomic_ullong;

typedef boost::shared_ptr<void> VoidPtr;

/**
 * Blob versions cannot be 0. That value will represent
 * either a null or uninitialized version.
 * The first valid version is 1.
 */
static const blob_version_t blob_version_invalid = 0;
static const blob_version_t blob_version_initial = 1;
static const blob_version_t blob_version_deleted =
        std::numeric_limits<fds_uint64_t>::max();
static const uint OBJECTID_DIGESTLEN = 20;
class ObjectID : public serialize::Serializable {
  private:
    uint8_t  digest[OBJECTID_DIGESTLEN];

  public:
    ObjectID();
    explicit ObjectID(uint32_t dataSet);
    explicit ObjectID(uint8_t *objId);
    ObjectID(const ObjectID& rhs);  // NOLINT
    explicit ObjectID(const std::string& oid);
    ObjectID(uint8_t  *objId, uint32_t length);
    virtual ~ObjectID();

    /*
     * Assumes the length of the data is 2 * hash size.
     * The caller needs to ensure this is the case.
     */
    void SetId(const char*data, fds_uint32_t len);
    void SetId(uint8_t  *data, fds_uint32_t  length);
    void SetId(const std::string &data) {
        fds_assert(data.length() <= sizeof(digest));
        memcpy(digest, data.data(), data.length());
    }
    const uint8_t* GetId() const;

    const uint getDigestLength() const {
        return OBJECTID_DIGESTLEN;
    }

   /*
    * bit mask. this will help to boost the performance 
    */

    fds_uint64_t getTokenBits(fds_uint32_t numBits) const;
    fds_uint32_t GetLen() const;
    std::string ToString() const;
    bool operator==(const ObjectID& rhs) const;
    bool operator!=(const ObjectID& rhs) const;
    bool operator < (const ObjectID& rhs) const;
    bool operator > (const ObjectID& rhs) const;
    ObjectID& operator=(const ObjectID& rhs);
    ObjectID& operator=(const std::string& hexStr);
    std::string ToHex() const;

    uint32_t write(serialize::Serializer*  serializer) const;
    uint32_t read(serialize::Deserializer* deserializer);

    static std::string ToHex(const ObjectID& oid);
    static std::string ToHex(const uint8_t *key, fds_uint32_t len);
    static std::string ToHex(const char *key, fds_uint32_t len);
    static std::string ToHex(const fds_uint32_t *key, fds_uint32_t len);
    static int compare(const ObjectID &lhs, const ObjectID &rhs);
    static void getTokenRange(const fds_token_id& token,
            const uint32_t& nTokenBits,
            ObjectID &start, ObjectID &end);

    friend class ObjectLess;
    friend class ObjIdGen;
};

/* NullObjectID */
extern ObjectID NullObjectID;

std::ostream& operator<<(std::ostream& out, const ObjectID& oid);

class ObjectHash {
  public:
    size_t operator()(const ObjectID& oid) const {
      return std::hash<std::string>()(oid.ToHex());
    }
};

class ObjectLess {
  public:
    bool operator() (const ObjectID& oid1, const ObjectID& oid2) {
      return oid1 < oid2;
    }
};

struct DiskLoc {
    fds_uint64_t vol_id;
    fds_uint16_t file_id;
    fds_uint64_t offset;
};

class ObjectBuf {
  public:
    fds_uint32_t size;
    std::string data;
    ObjectBuf()
      : size(0), data("")
      {
      }
    explicit ObjectBuf(const std::string &str)
    : data(str)
    {
        size = str.length();
    }
    void resize(const size_t &sz)
    {
        size = sz;
        data.resize(sz);
    }
};

fds_uint32_t str_to_ipv4_addr(std::string ip_str);
std::string ipv4_addr_to_str(fds_uint32_t ip);

typedef void (*blkdev_complete_req_cb_t)(void *arg1, void *arg2, void *treq, int res);
typedef enum {
    FDS_IO_READ,
    FDS_IO_WRITE,
    FDS_IO_REDIR_READ,
    FDS_IO_OFFSET_WRITE,
    FDS_CAT_UPD,
    FDS_CAT_UPD_SVC,
    FDS_CAT_QRY,
    FDS_CAT_QRY_SVC,
    FDS_START_BLOB_TX,
    FDS_START_BLOB_TX_SVC,
    FDS_ABORT_BLOB_TX,
    FDS_COMMIT_BLOB_TX,
    FDS_ATTACH_VOL,
    FDS_LIST_BLOB,
    FDS_PUT_BLOB,
    FDS_GET_BLOB,
    FDS_STAT_BLOB,
    FDS_GET_BLOB_METADATA,
    FDS_SET_BLOB_METADATA,
    FDS_GET_VOLUME_METADATA,
    FDS_DELETE_BLOB,
    FDS_DELETE_BLOB_SVC,
    FDS_LIST_BUCKET,
    FDS_BUCKET_STATS,
    FDS_SM_GET_OBJECT,
    FDS_SM_PUT_OBJECT,
    FDS_SM_DELETE_OBJECT,
    FDS_SM_READ_TOKEN_OBJECTS,
    FDS_SM_WRITE_TOKEN_OBJECTS,
    FDS_SM_SNAPSHOT_TOKEN,
    FDS_SM_SYNC_APPLY_METADATA,
    FDS_SM_SYNC_RESOLVE_SYNC_ENTRY,
    FDS_SM_APPLY_OBJECTDATA,
    FDS_SM_READ_OBJECTDATA,
    FDS_SM_READ_OBJECTMETADATA,
    FDS_SM_COMPACT_OBJECTS,
    FDS_DM_SNAP_VOLCAT,
    FDS_DM_SNAPDELTA_VOLCAT,
    FDS_DM_FWD_CAT_UPD,
    FDS_DM_PUSH_META_DONE,
    FDS_DM_PURGE_COMMIT_LOG,
    FDS_OP_INVALID
} fds_io_op_t;

std::ostream& operator<<(std::ostream& os, const fds_io_op_t& opType);

#define  FDS_SH_IO_MAGIC_IN_USE 0x1B0A2C3D
#define  FDS_SH_IO_MAGIC_NOT_IN_USE 0xE1D0A2B3


class FDS_IOType {
  public:
    FDS_IOType() {}
    virtual ~FDS_IOType() {}

    typedef enum {
        STOR_HV_IO,
        STOR_MGR_IO,
        DATA_MGR_IO
    } ioModule;

    int         io_magic;
    fds_io_op_t io_type;
    fds_uint32_t io_req_id;
    fds_volid_t io_vol_id;
    fds_int32_t io_status;
    fds_uint32_t io_service_time;  // usecs
    fds_uint32_t io_wait_time;  // usecs
    fds_uint32_t io_total_time;  // usecs
    ioModule io_module;  // IO belongs to which module for Qos proc
    boost::posix_time::ptime enqueue_time;
    boost::posix_time::ptime dispatch_time;
    boost::posix_time::ptime io_done_time;

    /*
     * TODO: Blkdev specific fields that can be REMOVED.
     * These are left here simply because legacy, unused code
     * still references these and needs them to compile.
     */
    void *fbd_req;
    void *vbd;
    void *vbd_req;
    blkdev_complete_req_cb_t comp_req;

    // performance data collection related structures
    std::string perfNameStr;
    PerfEventType opReqFailedPerfEventType;
    PerfContext opReqLatencyCtx;
    PerfContext opLatencyCtx;

    PerfContext opTransactionWaitCtx;

    PerfContext opQoSWaitCtx;
};

namespace blob {

typedef enum {
    TRUNCATE = 1
} fds_blob_mode_t;

}  // namespace blob

}  // namespace fds

namespace std {
template <>
struct hash<fds::ObjectID> {
    std::size_t operator()(const fds::ObjectID oid) const {
        return std::hash<std::string>()(oid.ToHex());
    }
};
}  // namespace std

/*
 * NOTE!!! include only std typedefs here. Dont use any fds objects !!!!
 */
#include <vector>

namespace fds {
    // new c++11 typedef convention - pretty cool !!!
    using StringList  = std::vector<std::string>;
    using ConstString = const std::string&;
}  // namespace fds

#endif  // SOURCE_INCLUDE_FDS_TYPES_H_
