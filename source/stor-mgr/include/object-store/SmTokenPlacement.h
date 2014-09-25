/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_STOR_MGR_INCLUDE_OBJECT_STORE_SMTOKENPLACEMENT_H_
#define SOURCE_STOR_MGR_INCLUDE_OBJECT_STORE_SMTOKENPLACEMENT_H_

#include <string>
#include <set>
#include <map>

#include <persistent_layer/dm_io.h>

namespace fds {

#define SM_TOKEN_MASK  0xff
#define SM_TOKEN_COUNT 256
#define SM_TIER_COUNT  2

typedef std::set<fds_token_id> SmTokenSet;
typedef std::map<fds_uint16_t, SmTokenSet> DiskSmTokenMap;

/**
 * Object location table that maps SM token id to disk id on
 * HDD and SSD tier. SM token can reside on HDD, SSD or both
 * tiers.
 * We currently support constant number of SM tokens = 256
 * The class is not thread-safe, the caller must ensure thread-safety
 */
class ObjectLocationTable: public serialize::Serializable {
  public:
    ObjectLocationTable();
    ~ObjectLocationTable();

    typedef std::shared_ptr<ObjectLocationTable> ptr;
    typedef std::shared_ptr<const ObjectLocationTable> const_ptr;

    /**
     * Sets disk id for a given sm token id and tier
     */
    void setDiskId(fds_token_id smToken,
                   diskio::DataTier tier,
                   fds_uint16_t disk_id);
    fds_uint16_t getDiskId(fds_token_id smToken,
                           diskio::DataTier tier) const;

    /**
     * Generates reverse map of disk id to token id
     */
    void generateDiskToSmTokenMap();

    /**
     * Get a set of SM tokens that reside on a given disk
     * @param[out] tokenSet is populated with SM tokens
     */
    SmTokenSet getSmTokens(fds_uint16_t disk_id) const;

    // compares to another object location table
    fds_bool_t operator ==(const ObjectLocationTable& rhs) const;

    // Serializable
    uint32_t virtual write(serialize::Serializer*  s) const;
    uint32_t virtual read(serialize::Deserializer* d);

    friend std::ostream& operator<< (std::ostream &out,
                                     const ObjectLocationTable& tbl);

  private:
    // SM token to disk ID table
    // row 0 : disk ID of HDDs
    // row 1 : disk ID of SSD
    fds_uint16_t table[SM_TIER_COUNT][SM_TOKEN_COUNT];

    /// cached reverse map from disk id to tokens
    DiskSmTokenMap diskSmTokenMap;
};

/**
 * Algorithm for distributing SM tokens over disks
 * The class is intended to be stateless. It takes all
 * required input via parameters and returns Object Location
 * Table
 */
class SmTokenPlacement {
  public:
    static void compute(const std::set<fds_uint16_t>& hdds,
                        const std::set<fds_uint16_t>& ssds,
                        ObjectLocationTable::ptr olt);
};

}  // namespace fds

#endif  // SOURCE_STOR_MGR_INCLUDE_OBJECT_STORE_SMTOKENPLACEMENT_H_
