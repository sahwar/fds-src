/*
 * Copyright 2013-2014 Formation Data Systems, Inc.
 */

#ifndef SOURCE_ACCESS_MGR_INCLUDE_REQUESTS_VOLUMECONTENTSREQ_H_
#define SOURCE_ACCESS_MGR_INCLUDE_REQUESTS_VOLUMECONTENTSREQ_H_

#include <string>

#include "AmRequest.h"

namespace fds
{

struct VolumeContentsReq: public AmRequest {
    size_t maxkeys;
    BucketContext *bucket_ctxt;

    VolumeContentsReq(fds_volid_t _volid,
                      BucketContext *_bucket_ctxt,
                      size_t const _max_keys,
                      CallbackPtr cb)
            :   AmRequest(FDS_VOLUME_CONTENTS, _volid, _bucket_ctxt->bucketName, "", cb),
                bucket_ctxt(_bucket_ctxt)
    {
        e2e_req_perf_ctx.type = AM_VOLUME_CONTENTS_REQ;
        fds::PerfTracer::tracePointBegin(e2e_req_perf_ctx);
    }
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_REQUESTS_VOLUMECONTENTSREQ_H_
