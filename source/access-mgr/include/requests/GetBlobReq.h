/*
 * Copyright 2013-2014 Formation Data Systems, Inc.
 */

#ifndef SOURCE_ACCESS_MGR_INCLUDE_GETBLOBREQ_H_
#define SOURCE_ACCESS_MGR_INCLUDE_GETBLOBREQ_H_

#include <string>

#include "AmRequest.h"

namespace fds
{

struct GetBlobReq: public AmRequest {
    fds_bool_t get_metadata;

    fds_bool_t oid_cached;
    fds_bool_t metadata_cached;
    // TODO(bszmyd): Mon 23 Mar 2015 02:59:55 AM PDT
    // Take this out when we support transactions.
    fds_bool_t retry { false };
    ObjectID   last_obj_id;

    GetBlobReq(fds_volid_t _volid,
               const std::string& _volumeName,
               const std::string& _blob_name,
               CallbackPtr cb,
               fds_uint64_t _blob_offset,
               fds_uint64_t _data_len);

    ~GetBlobReq();
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_GETBLOBREQ_H_
