#include "DataMgr.h"

#include <iostream>

namespace fds {

DataMgr *dataMgr;
fds_threadpool *test_tp;

static void _open_entry(int val) {
  // std::cout << "Hello? " << val << std::endl;
  //FDS_PLOG(dataMgr->GetLog()) << "Starting open process thread of "
  //                          << dataMgr->num_threads;
}

Error DataMgr::_process_open(fds_uint32_t vol_uuid,
                             fds_uint32_t vol_offset,
                             fds_uint32_t trans_id,
                             const ObjectID& oid) {
  Error err;

  /*
   * Check the map to see if we have know about the volume.
   * TODO: We should not implicitly create the volume here!
   * We should only be creating the volume on OM volume create
   * requests.
   * TODO: Just hack the name as the offset for now.
   */
  vol_map_mtx->lock();
  if (vol_meta_map.count(vol_uuid) == 0) {
    vol_meta_map[vol_uuid] = new VolumeMeta(stor_prefix +
                                            std::to_string(vol_uuid),
                                            vol_uuid,
                                            dm_log);
    FDS_PLOG(dataMgr->GetLog()) << "Created vol meta for vol uuid "
                                << vol_uuid << " and transaction "
                                << trans_id;
  }

  VolumeMeta *vol_meta = vol_meta_map[vol_uuid];
  vol_map_mtx->unlock();

  err = vol_meta->OpenTransaction(vol_offset, oid);

  if (err.ok()) {
    FDS_PLOG(dataMgr->GetLog()) << "Opened transaction for volume "
                                << vol_uuid << " at offset "
                                << vol_offset << " and mapped to object "
                                << oid;
  } else {
    FDS_PLOG(dataMgr->GetLog()) << "Failed to open transaction for volume "
                                << vol_uuid;
  }

  return err;
}

Error DataMgr::_process_commit(fds_uint32_t vol_uuid,
                               fds_uint32_t vol_offset,
                               fds_uint32_t trans_id,
                               const ObjectID& oid) {
  Error err(ERR_OK);

  /*
   * Here we should be updating the TVC to reflect the commit
   * and adding back to the VVC. The TVC can be an in-memory
   * update.
   * For now, we don't need to do anything because it was put
   * into the VVC on open.
   */
  FDS_PLOG(dataMgr->GetLog()) << "Committed transaction for volume "
                              << vol_uuid << " to offset "
                              << vol_offset << " and mapped to object "
                              << oid;

  return err;
}

Error DataMgr::_process_abort() {
  Error err(ERR_OK);

  /*
   * TODO: Here we should be determining the state of the
   * transaction and removing the entry from the TVC.
   */

  return err;
}

Error DataMgr::_process_query(fds_uint32_t vol_uuid,
                              fds_uint32_t vol_offset,
                              ObjectID *oid) {
  Error err(ERR_OK);

  /*
   * Check the map to see if we have know about the volume.
   * TODO: Just hack the name as the offset for now.
   */
  vol_map_mtx->lock();
  if (vol_meta_map.count(vol_uuid) == 0) {
    /*
     * We don't know about this volume, so we don't
     * have anything to query.
     */
    vol_map_mtx->unlock();
    FDS_PLOG(dataMgr->GetLog()) << "Vol meta query don't know about volume "
                                << vol_uuid;
    err = ERR_CAT_QUERY_FAILED;
    return err;
  }

  VolumeMeta *vol_meta = vol_meta_map[vol_uuid];
  vol_map_mtx->unlock();

  err = vol_meta->QueryVcat(vol_offset, oid);

  if (err.ok()) {
    FDS_PLOG(dataMgr->GetLog()) << "Vol meta query for volume "
                                << vol_uuid << " at offset "
                                << vol_offset << " found object " << *oid;
  } else {
    FDS_PLOG(dataMgr->GetLog()) << "Vol meta query FAILED for volume "
                                << vol_uuid;
  }
  
  return err;
}

DataMgr::DataMgr()
    : port_num(0),
      num_threads(DM_TP_THREADS) {
  dm_log = new fds_log("dm", "logs");
  vol_map_mtx = new fds_mutex("Volume map mutex");

  // _tp = new fds_threadpool(num_threads);
  _tp = new fds_threadpool(3);
  // test_tp = new fds_threadpool(3);

  FDS_PLOG(dm_log) << "Constructing the Data Manager";
}

DataMgr::~DataMgr() {

  FDS_PLOG(dm_log) << "Destructing the Data Manager";

  // delete test_tp;

  /*
   * This will wait for all current threads to
   * complete.
   */
  delete _tp;

  for (std::unordered_map<fds_uint64_t, VolumeMeta*>::iterator it = vol_meta_map.begin();
       it != vol_meta_map.end();
       it++) {
    delete it->second;
  }
  vol_meta_map.clear();
  
  delete vol_map_mtx;
  delete dm_log;
}

int DataMgr::run(int argc, char* argv[]) {
  /*
   * Process the cmdline args.
   */
  for (fds_int32_t i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--port=", 7) == 0) {
      port_num = strtoul(argv[i] + 7, NULL, 0);
    } else if (strncmp(argv[i], "--prefix=", 9) == 0) {
      stor_prefix = argv[i] + 9;
    } else {
      std::cout << "Invalid argument " << argv[i] << std::endl;
      return -1;
    }
  }

  Ice::PropertiesPtr props = communicator()->getProperties();

  /*
   * Set basic thread properties.
   */
  props->setProperty("DataMgr.ThreadPool.Size", "50");
  props->setProperty("DataMgr.ThreadPool.SizeMax", "100");
  props->setProperty("DataMgr.ThreadPool.SizeWarn", "75");

  if (port_num == 0) {
    /*
     * Pull the port from the config file if it wasn't
     * specified on the command line.
     */
    port_num = props->getPropertyAsInt("DataMgr.PortNumber");
  }

  FDS_PLOG(dm_log) << "Data Manager using port " << port_num;
  FDS_PLOG(dm_log) << "Data Manager using storage prefix "
                   << stor_prefix;
  
  callbackOnInterrupt();

  /*
   * Setup TCP endpoint.
   */
  std::ostringstream tcpProxyStr;
  tcpProxyStr << "tcp -p " << port_num;
  Ice::ObjectAdapterPtr adapter = communicator()->createObjectAdapterWithEndpoints("DataMgr", tcpProxyStr.str());

  reqHandleSrv = new ReqHandler(communicator());
  adapter->add(reqHandleSrv, communicator()->stringToIdentity("DataMgr"));

  adapter->activate();
  
  communicator()->waitForShutdown();

  return EXIT_SUCCESS;
}

void DataMgr::swapMgrId(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& fdsp_msg) {
  FDS_ProtocolInterface::FDSP_MgrIdType temp_id;
  temp_id = fdsp_msg->dst_id;
  fdsp_msg->dst_id = fdsp_msg->src_id;
  fdsp_msg->src_id = temp_id;
  
  long tmp_addr;
  tmp_addr = fdsp_msg->dst_ip_hi_addr;
  fdsp_msg->dst_ip_hi_addr = fdsp_msg->src_ip_hi_addr;
  fdsp_msg->src_ip_hi_addr = tmp_addr;
  
  tmp_addr = fdsp_msg->dst_ip_lo_addr;
  fdsp_msg->dst_ip_lo_addr = fdsp_msg->src_ip_lo_addr;
  fdsp_msg->src_ip_lo_addr = tmp_addr;
}

fds_log* DataMgr::GetLog() {
  return dm_log;
}

void
DataMgr::interruptCallback(int)
{
  FDS_PLOG(dataMgr->GetLog()) << "Shutting down communicator";
  communicator()->shutdown();
}

DataMgr::ReqHandler::ReqHandler(const Ice::CommunicatorPtr& communicator) {
}

DataMgr::ReqHandler::~ReqHandler() {
}

DataMgr::RespHandler::RespHandler() {
}

DataMgr::RespHandler::~RespHandler() {
}

void DataMgr::ReqHandler::PutObject(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr &msg_hdr,
                                    const FDS_ProtocolInterface::FDSP_PutObjTypePtr &put_obj,
                                    const Ice::Current&) {
}

void DataMgr::ReqHandler::GetObject(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr &msg_hdr,
                                    const FDS_ProtocolInterface::FDSP_GetObjTypePtr& get_obj,
                                    const Ice::Current&) {
}

void DataMgr::ReqHandler::UpdateCatalogObject(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr &msg_hdr,
                                              const FDS_ProtocolInterface::FDSP_UpdateCatalogTypePtr& update_catalog,
                                              const Ice::Current&) {
  Error err(ERR_OK);

  ObjectID oid(update_catalog->data_obj_id.hash_high,
               update_catalog->data_obj_id.hash_low);

  FDS_PLOG(dataMgr->GetLog()) << "Processing update catalog request with "
                              << "volume offset: " << update_catalog->volume_offset
                              << ", Obj ID " << oid
                              << ", Trans ID " << update_catalog->dm_transaction_id
                              << ", OP ID " << update_catalog->dm_operation;

  // dataMgr->_tp->schedule(_open_entry, 6);
  _open_entry(5);
  
  /*
   * For now, just treat this as an open
   */
  if (update_catalog->dm_operation ==
      FDS_ProtocolInterface::FDS_DMGR_TXN_STATUS_OPEN) {
    err = dataMgr->_process_open(msg_hdr->glob_volume_id,
                                 update_catalog->volume_offset,
                                 update_catalog->dm_transaction_id,
                                 oid);
  } else if (update_catalog->dm_operation ==
             FDS_ProtocolInterface::FDS_DMGR_TXN_STATUS_COMMITED) {
    err = dataMgr->_process_commit(msg_hdr->glob_volume_id,
                                   update_catalog->volume_offset,
                                   update_catalog->dm_transaction_id,
                                   oid);
  } else {
    err = ERR_CAT_QUERY_FAILED;
  }

  if (err.ok()) {
    msg_hdr->result  = FDS_ProtocolInterface::FDSP_ERR_OK;
    msg_hdr->err_msg = "Dude, you're good to go!";
  } else {
    msg_hdr->result   = FDS_ProtocolInterface::FDSP_ERR_FAILED;
    msg_hdr->err_msg  = "Something hit the fan...";
    msg_hdr->err_code = FDS_ProtocolInterface::FDSP_ERR_SM_NO_SPACE;
  }
  
  /*
   * Reverse the msg direction and send the response.
   * TODO: Are we wasting space sending the original update_catalog?
   */
  msg_hdr->msg_code = FDS_ProtocolInterface::FDSP_MSG_UPDATE_CAT_OBJ_RSP;
  dataMgr->swapMgrId(msg_hdr);
  dataMgr->respHandleCli->begin_UpdateCatalogObjectResp(msg_hdr, update_catalog);

  FDS_PLOG(dataMgr->GetLog()) << "Sending async update catalog response with "
                              << "volume offset: " << update_catalog->volume_offset
                              << ", Obj ID " << oid
                              << ", Trans ID " << update_catalog->dm_transaction_id
                              << ", OP ID " << update_catalog->dm_operation;

  if (update_catalog->dm_operation ==
      FDS_ProtocolInterface::FDS_DMGR_TXN_STATUS_OPEN) {
    FDS_PLOG(dataMgr->GetLog()) << "Sent update response for trans open request";
  } else if (update_catalog->dm_operation ==
             FDS_ProtocolInterface::FDS_DMGR_TXN_STATUS_COMMITED) {
    FDS_PLOG(dataMgr->GetLog()) << "Sent update response for trans commit request";
  }
}

void DataMgr::ReqHandler::QueryCatalogObject(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr &msg_hdr,
                                             const FDS_ProtocolInterface::FDSP_QueryCatalogTypePtr& query_catalog,
                                             const Ice::Current&) {
  Error err(ERR_OK);
  ObjectID oid(query_catalog->data_obj_id.hash_high,
               query_catalog->data_obj_id.hash_low);

  FDS_PLOG(dataMgr->GetLog()) << "Processing query catalog request with "
                              << "volume offset: " << query_catalog->volume_offset
                              << ", Obj ID " << oid
                              << ", Trans ID " << query_catalog->dm_transaction_id
                              << ", OP ID " << query_catalog->dm_operation;

  err = dataMgr->_process_query(msg_hdr->glob_volume_id,
                                query_catalog->volume_offset,
                                &oid);
  if (err.ok()) {
    msg_hdr->result  = FDS_ProtocolInterface::FDSP_ERR_OK;
    msg_hdr->err_msg = "Dude, you're good to go!";
    query_catalog->data_obj_id.hash_high = oid.GetHigh();
    query_catalog->data_obj_id.hash_low = oid.GetLow();
  } else {
    msg_hdr->result   = FDS_ProtocolInterface::FDSP_ERR_FAILED;
    msg_hdr->err_msg  = "Something hit the fan...";
    msg_hdr->err_code = FDS_ProtocolInterface::FDSP_ERR_SM_NO_SPACE;
  }

  /*
   * Reverse the msg direction and send the response.
   * TODO: Are we wasting space sending the original update_catalog?
   */
  msg_hdr->msg_code = FDS_ProtocolInterface::FDSP_MSG_QUERY_CAT_OBJ_RSP;
  dataMgr->swapMgrId(msg_hdr);
  dataMgr->respHandleCli->begin_QueryCatalogObjectResp(msg_hdr, query_catalog);
  FDS_PLOG(dataMgr->GetLog()) << "Sending async query catalog response with "
                              << "volume offset: " << query_catalog->volume_offset
                              << ", Obj ID " << oid
                              << ", Trans ID " << query_catalog->dm_transaction_id
                              << ", OP ID " << query_catalog->dm_operation;

  
}

void DataMgr::ReqHandler::OffsetWriteObject(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& msg_hdr,
                                            const FDS_ProtocolInterface::FDSP_OffsetWriteObjTypePtr& offset_write_obj,
                                            const Ice::Current&) {
}

void DataMgr::ReqHandler::RedirReadObject(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr &msg_hdr,
                                          const FDS_ProtocolInterface::FDSP_RedirReadObjTypePtr& redir_read_obj,
                                          const Ice::Current&) {
}

void DataMgr::ReqHandler::AssociateRespCallback(const Ice::Identity& ident,
                                                const Ice::Current& current) {
  try {
    std::cout << "Associating response Callback client to DataMgr`"
              << _communicator->identityToString(ident) << "'"<< std::endl;
  } catch(IceUtil::NullHandleException) {
    FDS_PLOG(dataMgr->GetLog()) << "Caught a NULL exception accessing _communicator";
    std::cout << "Caught some exception?!?!?!" << std::endl;
  }

  dataMgr->respHandleCli = FDS_ProtocolInterface::FDSP_DataPathRespPrx::uncheckedCast(current.con->createProxy(ident));
}

void
DataMgr::RespHandler::PutObjectResp(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr &msg_hdr,
                                    const FDS_ProtocolInterface::FDSP_PutObjTypePtr &put_obj,
                                    const Ice::Current&) 
{
}

void
DataMgr::RespHandler::GetObjectResp(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr &msg_hdr,
                                    const FDS_ProtocolInterface::FDSP_GetObjTypePtr& get_obj,
                                    const Ice::Current&) {
}

void
DataMgr::RespHandler::UpdateCatalogObjectResp(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr &msg_hdr, 
                                              const FDS_ProtocolInterface::FDSP_UpdateCatalogTypePtr& update_catalog ,
                                              const Ice::Current&)
{
}

void
DataMgr::RespHandler::QueryCatalogObjectResp(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr &msg_hdr, 
                                             const FDS_ProtocolInterface::FDSP_QueryCatalogTypePtr& query_catalog ,
                                             const Ice::Current&)
{
}

void
DataMgr::RespHandler::OffsetWriteObjectResp(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr& msg_hdr,
                                            const FDS_ProtocolInterface::FDSP_OffsetWriteObjTypePtr& offset_write_obj,
                                            const Ice::Current&)
{
}

void
DataMgr::RespHandler::RedirReadObjectResp(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr &msg_hdr,
                                          const FDS_ProtocolInterface::FDSP_RedirReadObjTypePtr& redir_read_obj,
                                          const Ice::Current&) {
}

}  // namespace fds

int main(int argc, char *argv[]) {

  fds::dataMgr = new fds::DataMgr();
  
  fds::dataMgr->main(argc, argv, "dm_test.conf");

  // fds::dataMgr->_tp->schedule(fds::_open_entry, 6);

  delete fds::dataMgr;
}
