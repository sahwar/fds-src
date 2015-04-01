/*
 * Copyright 2013-2014 Formation Data Systems, Inc.
 */

#ifndef SOURCE_ACCESS_MGR_INCLUDE_REQUESTS_PUTBLOBREQ_H_
#define SOURCE_ACCESS_MGR_INCLUDE_REQUESTS_PUTBLOBREQ_H_

#include <string>

#include "AmRequest.h"

namespace fds
{

struct PutBlobReq
    :   public AmRequest,
        public AmTxReq
{
    fds_bool_t last_buf;

    // Needed fields
    fds_uint64_t dmt_version;

    /// Shared pointer to data.
    boost::shared_ptr<std::string> dataPtr;

    /// Used for putBlobOnce scenarios.
    boost::shared_ptr< std::map<std::string, std::string> > metadata;
    fds_int32_t blob_mode;

    /* ack cnt for responses, decremented when response from SM and DM come back */
    std::atomic<int> resp_acks;
    /* Return status for completion callback */
    Error ret_status;

    /* Final metadata view after catalog update */
    fpi::FDSP_MetaDataList final_meta_data;

    /* Final blob size after catalog update */
    fds_uint64_t final_blob_size;

    /// Constructor used on regular putBlob requests.
    PutBlobReq(fds_volid_t _volid,
               const std::string& _volumeName,
               const std::string& _blob_name,
               fds_uint64_t _blob_offset,
               fds_uint64_t _data_len,
               boost::shared_ptr<std::string> _data,
               BlobTxId::ptr _txDesc,
               fds_bool_t _last_buf,
               CallbackPtr _cb);

    /// Constructor used on putBlobOnce requests.
    PutBlobReq(fds_volid_t          _volid,
               const std::string&   _volumeName,
               const std::string&   _blob_name,
               fds_uint64_t         _blob_offset,
               fds_uint64_t         _data_len,
               boost::shared_ptr<std::string> _data,
               fds_int32_t          _blobMode,
               boost::shared_ptr< std::map<std::string, std::string> >& _metadata,
               CallbackPtr _cb);

    void setTxId(const BlobTxId &txId) {
        // We only expect to need to set this in putBlobOnce cases
        fds_verify(tx_desc == NULL);
        tx_desc = BlobTxId::ptr(new BlobTxId(txId));
    }

    virtual ~PutBlobReq();

    void notifyResponse(const Error &e);
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_REQUESTS_PUTBLOBREQ_H_
