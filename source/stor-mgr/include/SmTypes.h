/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#ifndef SOURCE_STOR_MGR_INCLUDE_SMTYPES_H_
#define SOURCE_STOR_MGR_INCLUDE_SMTYPES_H_

#include <set>
#include <fds_types.h>

namespace fds {

#define SMTOKEN_MASK  0xff
#define SMTOKEN_COUNT 256
#define SM_TIER_COUNT  2

// file ID types
#define SM_INVALID_FILE_ID         0
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

}  // namespace fds
#endif  // SOURCE_STOR_MGR_INCLUDE_SMTYPES_H_
