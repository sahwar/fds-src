/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#ifndef SOURCE_STOR_MGR_INCLUDE_SMTYPES_H_
#define SOURCE_STOR_MGR_INCLUDE_SMTYPES_H_

#include <set>
#include <unordered_map>
#include <fds_types.h>

namespace fds {

#define SMTOKEN_MASK  0xff
#define SMTOKEN_COUNT 256u
#define SM_TIER_COUNT  2
#define MAX_HOST_DISKS 8 // Number of disks on which a SM token's data can be distributed.
// file ID types
#define SM_INVALID_FILE_ID         0u
/**
 * Not a real file id. Use SM_WRITE_FILE_ID when need to access
 * a file we are currently appending
 */
#define SM_WRITE_FILE_ID           0x0001
#define SM_INIT_FILE_ID            0x0002

typedef std::set<fds_token_id> SmTokenSet;
typedef std::multiset<fds_token_id> SmTokenMultiSet;

/// Migration types
#define SM_INVALID_EXECUTOR_ID     0
#define SM_MAX_NUM_RETRIES_SAME_SM 2

typedef fds_uint16_t DiskId;
typedef std::set<fds_uint16_t> DiskIdSet;
typedef std::unordered_map<fds_uint16_t, std::string> DiskLocMap;

const fds_uint16_t SM_INVALID_DISK_ID = 0xffff;

extern fds_uint32_t objDelCountThresh;

enum sm_task_type {
    scavenger = 0,
    token_compactor,
    migration,
    tiering,
    disk_change
};

}  // namespace fds
#endif  // SOURCE_STOR_MGR_INCLUDE_SMTYPES_H_
