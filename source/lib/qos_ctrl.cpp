/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#include "qos_ctrl.h"
#include "fds_qos.h"
#include "fds_err.h"
#include "qos_htb.h"

namespace fds {

FDS_QoSControl::FDS_QoSControl() {
}

FDS_QoSControl::FDS_QoSControl(fds_uint32_t _max_threads, 
                               dispatchAlgoType algo, 
                               fds_log *log) :
    qos_max_threads(_max_threads)  {
   threadPool = new fds_threadpool(qos_max_threads);
   dispatchAlgo = algo;
   qos_log = log;
   total_rate = 20000; //IOPS
}


FDS_QoSControl::~FDS_QoSControl()  {
  //delete dispatcher;
  delete threadPool;
}

fds_uint32_t FDS_QoSControl::waitForWorkers() {

  return 10;
}


void startDispatcher(FDS_QoSControl *qosctl) { 
  if (qosctl && qosctl->dispatcher) {
     qosctl->dispatcher->dispatchIOs();
  }
  
}
void FDS_QoSControl::runScheduler() { 
  
  threadPool->schedule(startDispatcher, this);
}

void   FDS_QoSControl::setQosDispatcher(dispatchAlgoType algo_type, FDS_QoSDispatcher *qosDispatcher) {
}

Error   FDS_QoSControl::registerVolume(fds_volid_t vol_uuid, FDS_VolumeQueue *volq) {
Error err(ERR_OK);
   err = dispatcher->registerQueue(vol_uuid, volq);
   return err;
}


Error   FDS_QoSControl::deregisterVolume(fds_volid_t vol_uuid) {
Error err(ERR_OK);
    err = dispatcher->deregisterQueue(vol_uuid);
   return err;
}

Error FDS_QoSControl::enqueueIO(fds_volid_t volUUID, FDS_IOType *io) {
  Error err(ERR_OK);

  FDS_PLOG(qos_log)  << "Enqueuing IO";
  err = dispatcher->enqueueIO(volUUID, io);
  return err;
}

}  // namespace fds
