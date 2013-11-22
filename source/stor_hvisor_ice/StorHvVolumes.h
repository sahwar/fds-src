/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#ifndef __STOR_HV_VOLS_H_
#define __STOR_HV_VOLS_H_

#include <Ice/Ice.h>
#include <IceUtil/IceUtil.h>
#include <boost/thread/thread.hpp>
#include <boost/lockfree/queue.hpp>
#include <iostream>
#include <boost/atomic.hpp>
#include <concurrency/ThreadPool.h>

#include <unordered_map>

#include <fdsp/FDSP.h>
#include <fds_err.h>
#include <fds_types.h>
#include <fds_volume.h>
#include <qos_ctrl.h>
#include <fds_qos.h>
#include <util/Log.h>
#include <concurrency/RwLock.h>
#include "VolumeCatalogCache.h"
#include "qos_ctrl.h"
#include "StorHvJournal.h"
#include "native_api.h"


/* defaults */
#define FDS_DEFAULT_VOL_UUID 1


/*
 * Forward declaration of SH control class
 */
class StorHvCtrl;

namespace fds {

class StorHvVolume : public FDS_Volume
{
public:
  StorHvVolume(const VolumeDesc& vdesc, StorHvCtrl *sh_ctrl, fds_log *parent_log);
  ~StorHvVolume();

  /* safe destruction, after this, volume object is not valid */
  void destroy();
  
  bool isValidLocked() const;

  /* read locks. Journal table and volume catalog cache have their own locks, 
   * so exposing read lock to protect against volume being destroyed 
   */
  void readLock();
  void readUnlock();

public: /* data*/

  StorHvJournal *journal_tbl;
  VolumeCatalogCache *vol_catalog_cache;
  int blkdev_minor;
  /* Reference to parent SH instance */
  StorHvCtrl *parent_sh;

 /*
   * per volume queue
   */
  FDS_VolumeQueue*  volQueue;

private: /* data */

  /* lock to prevent volume destruction while accessing volume data */
  fds_rwlock rwlock; 
  bool is_valid;
};

class StorHvVolumeLock 
{
 public:
  /* Ok if vol == NULL, will not do anything */
  StorHvVolumeLock(StorHvVolume *vol);
  ~StorHvVolumeLock();
    
 private:
  StorHvVolume *shvol;
};
class AmQosReq;

class StorHvVolumeTable
{
 public:
  /* A logger is created if not passed in */
  StorHvVolumeTable(StorHvCtrl *sh_ctrl);
  /* Use logger that passed in to the constructor */
  StorHvVolumeTable(StorHvCtrl *sh_ctrl, fds_log *parent_log);
  ~StorHvVolumeTable();

  Error registerVolume(const VolumeDesc& vdesc); 
  Error removeVolume(fds_volid_t vol_uuid);

  /* 
   * Returns the locked volume object. Guarantees that the
   * returned volume object is valid (i.e., can safely access
   * journal table and volume catalog cache) 
   * Must call StorHvVolume::readUnlock on returned volume object 
   * Returns NULL if volume does not exist
   */
  StorHvVolume* getLockedVolume(fds_volid_t vol_uuid);

  /* 
   * Returns volume but not thread-safe 
   * Use StorHvVolumeLock to lock the volume and check if volume
   * object is still valid via StorHvVolume::isValidLocked() 
   * before using the volume object 
   * Returns NULL is volume does not exist
   */
  StorHvVolume* getVolume(fds_volid_t vol_uuid);

  /* returns volume uuid if found in volume map.
   * if volume does not exist, returns 'invalid_vol_id'  
   */
  fds_volid_t getVolumeUUID(const std::string& vol_name);

  /* returns true if volume exists, otherwise retuns false */
  fds_bool_t volumeExists(const std::string& vol_name);

  /* add blob request to wait queue -- those are blobs that
   * are waiting for OM to attach buckets to AM; once 
   * vol table receives vol attach event, it will move 
   * all requests waiting in the queue for that bucket to
   * appropriate qos queue */
  void addBlobToWaitQueue(const std::string& bucket_name,
			  FdsBlobReq* blob_req);

 private: /* methods */ 

  /* handler for volume-related control message from OM */
  static void volumeEventHandler(fds_volid_t vol_uuid, 
                                 VolumeDesc *vdb,
                                 fds_vol_notify_t vol_action,
				 FDS_ProtocolInterface::FDSP_ResultType);
 
  void moveWaitBlobsToQosQueue(fds_volid_t vol_uuid,
			       const std::string& vol_name,
			       Error error);

  /* print volume map, other detailed state to log */
  void dump();
    
 private: /* data */

  /* volume uuid -> StorHvVolume map */
  std::unordered_map<fds_volid_t, StorHvVolume*> volume_map;   

  /* Protects volume_map */
  fds_rwlock map_rwlock;

  /* list of blobs that are waiting for OM to attach appropriate
   * bucket to AM if it exists/ or return 'does not exist error */
  typedef std::vector<AmQosReq*> bucket_wait_vec_t;
  typedef std::map<std::string, bucket_wait_vec_t> wait_blobs_t;
  typedef std::map<std::string, bucket_wait_vec_t>::iterator wait_blobs_it_t; 
  wait_blobs_t wait_blobs;
  fds_rwlock wait_rwlock;

  /* Reference to parent SH instance */
  StorHvCtrl *parent_sh;

  /*
   * Pointer to logger to use 
   */
  fds_log *vt_log;
  fds_bool_t created_log;
};

class GetBlobReq: public FdsBlobReq {
 public:
  BucketContext *bucket_ctxt;
  std::string ObjKey;
  GetConditions *get_cond;
  //  fds_uint64_t startByte; //same as blob_offset in base class
  fds_uint64_t byteCount;
  void *req_context;
  fdsnGetObjectHandler getObjCallback;
  void *callback_data;
  
  GetBlobReq(fds_volid_t _volid,
	     const std::string& _blob_name, //same as objKey
	     fds_uint64_t _blob_offset,
	     fds_uint64_t _data_len,
	     char* _data_buf,
	     fds_uint64_t _byte_count, 
	     BucketContext* _bucket_ctxt,
	     GetConditions* _get_conds,
	     void* _req_context,
	     fdsnGetObjectHandler _get_obj_handler,
	     void* _callback_data)
    : FdsBlobReq(FDS_GET_BLOB, _volid, _blob_name, _blob_offset, _data_len, _data_buf, NULL),
    bucket_ctxt(_bucket_ctxt),
    ObjKey(_blob_name),
    get_cond(_get_conds),
    byteCount(_byte_count),
    req_context(_req_context),
    getObjCallback(_get_obj_handler),
    callback_data(_callback_data)
    {
    }

  ~GetBlobReq() { };

  void DoCallback(FDSN_Status status, ErrorDetails* errDetails) {
    (getObjCallback)(req_context, dataLen, dataBuf, callback_data, status, errDetails);
  }

};


class PutBlobReq: public FdsBlobReq {
 public:
  BucketContext *bucket_ctxt;
  std::string ObjKey;
  PutProperties *putProperties;
  void *req_context;
  fdsnPutObjectHandler putObjCallback;
  void *callback_data;
  
 PutBlobReq(fds_volid_t _volid,
	    const std::string& _blob_name, //same as objKey
	    fds_uint64_t _blob_offset,
	    fds_uint64_t _data_len,
	    char* _data_buf,
	    BucketContext* _bucket_ctxt,
	    PutProperties* _put_props,
	    void* _req_context,
	    fdsnPutObjectHandler _put_obj_handler,
	    void* _callback_data) 
   : FdsBlobReq(FDS_PUT_BLOB, _volid, _blob_name, _blob_offset, _data_len, _data_buf, NULL),
    bucket_ctxt(_bucket_ctxt),
    ObjKey(_blob_name),
    putProperties(_put_props),
    req_context(_req_context),
    putObjCallback(_put_obj_handler),
    callback_data(_callback_data)
    {
    }

  ~PutBlobReq() { };

  void DoCallback(FDSN_Status status, ErrorDetails* errDetails) {
    (putObjCallback)(req_context, dataLen, dataBuf, callback_data, status, errDetails);
  }
};


class DeleteBlobReq: public FdsBlobReq {
 public:
  BucketContext *bucket_ctxt;
  std::string ObjKey;
  void *req_context;
  fdsnResponseHandler responseCallback;
  void *callback_data;
  
 DeleteBlobReq(fds_volid_t _volid,
	       const std::string& _blob_name,
	       BucketContext* _bucket_ctxt,
	       void* _req_context,
	       fdsnResponseHandler _resp_handler,
	       void* _callback_data) 
   : FdsBlobReq(FDS_DELETE_BLOB, _volid, _blob_name, 0, 0, NULL, NULL),
    bucket_ctxt(_bucket_ctxt),
    ObjKey(_blob_name),
    req_context(_req_context),
    responseCallback(_resp_handler),
    callback_data(_callback_data)
    {
    }

  ~DeleteBlobReq() { };
};

class ListBucketReq { 
 public:
  BucketContext *bucketctxt;
  std::string prefix;
  std::string marker;
  std::string delimiter;
  fds_uint32_t maxkeys;
  void *requestContext;
  fdsnListBucketHandler handler;
  void *callbackData;
  
  ListBucketReq() { };
  ~ListBucketReq() { };
};

/*
 * Internal wrapper class for AM QoS
 * requests.
 */
class AmQosReq : public FDS_IOType {
private:
  FdsBlobReq *blobReq;
  PutBlobReq  *putBlobReq;
  GetBlobReq  *getBlobReq;
  DeleteBlobReq  *deleteBlobReq;;
  ListBucketReq  *listBucketReq;

public:
  AmQosReq(FdsBlobReq *_br)
      : blobReq(_br) {
    /*
     * Set the base class defaults
     */
    io_magic  = FDS_SH_IO_MAGIC_IN_USE;
    io_module = STOR_HV_IO;
    io_vol_id = blobReq->getVolId();
    io_type   = blobReq->getIoType();
    /*
     * Set a reqId here. Need a atomic counter in
     * Sh ctrl
     */
    io_req_id = 885;

    /*
     * Zero out FBD stuff to make sure we don't use it.
     * TODO: Remove this once it's remove from base class.
     */
    fbd_req = NULL;
    vbd     = NULL;
    vbd_req = NULL;
  }
  ~AmQosReq() {
  }

  fds_bool_t magicInUse() const {
    return blobReq->magicInUse();
  }

  FdsBlobReq *fdsBlobReq() const {
    return blobReq;
  }
 
};

} // namespace fds

#endif // __STOR_HV_VOLS_H_
