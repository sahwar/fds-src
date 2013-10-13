/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#include <iostream>

#include "StorMgr.h"
#include "DiskMgr.h"

namespace fds {

fds_bool_t  stor_mgr_stopping = false;

#define FDS_XPORT_PROTO_TCP 1
#define FDS_XPORT_PROTO_UDP 2
#define FDSP_MAX_MSG_LEN (4*(4*1024 + 128))

ObjectStorMgr *objStorMgr;

ObjectStorMgrI::ObjectStorMgrI(const Ice::CommunicatorPtr& communicator): _communicator(communicator) {
}

ObjectStorMgrI::~ObjectStorMgrI() {
}

void
ObjectStorMgrI::PutObject(const FDSP_MsgHdrTypePtr &msg_hdr, const FDSP_PutObjTypePtr &put_obj, const Ice::Current&) {
  FDS_PLOG(objStorMgr->GetLog()) << "Putobject()";
  objStorMgr->PutObject(msg_hdr, put_obj);
  msg_hdr->msg_code = FDSP_MSG_PUT_OBJ_RSP;
  objStorMgr->swapMgrId(msg_hdr);
  objStorMgr->fdspDataPathClient->begin_PutObjectResp(msg_hdr, put_obj);
   FDS_PLOG(objStorMgr->GetLog()) << "Sent async PutObj response to Hypervisor";
}

void
ObjectStorMgrI::GetObject(const FDSP_MsgHdrTypePtr &msg_hdr, const FDSP_GetObjTypePtr& get_obj, const Ice::Current&) {
  FDS_PLOG(objStorMgr->GetLog()) << "Getobject()";
  objStorMgr->GetObject(msg_hdr, get_obj);
  msg_hdr->msg_code = FDSP_MSG_GET_OBJ_RSP;
  objStorMgr->swapMgrId(msg_hdr);
  objStorMgr->fdspDataPathClient->begin_GetObjectResp(msg_hdr, get_obj);
  FDS_PLOG(objStorMgr->GetLog()) << "Sent async GetObj response to Hypervisor";
}

void
ObjectStorMgrI::UpdateCatalogObject(const FDSP_MsgHdrTypePtr &msg_hdr, const FDSP_UpdateCatalogTypePtr& update_catalog , const Ice::Current&) {
  FDS_PLOG(objStorMgr->GetLog()) << "Wrong Interface Call: In the interface updatecatalog()";
}

void
ObjectStorMgrI::QueryCatalogObject(const FDSP_MsgHdrTypePtr &msg_hdr, const FDSP_QueryCatalogTypePtr& query_catalog , const Ice::Current&) {
  FDS_PLOG(objStorMgr->GetLog())<< "Wrong Interface Call: In the interface QueryCatalogObject()";
}

void
ObjectStorMgrI::OffsetWriteObject(const FDSP_MsgHdrTypePtr& msg_hdr, const FDSP_OffsetWriteObjTypePtr& offset_write_obj, const Ice::Current&) {
  FDS_PLOG(objStorMgr->GetLog()) << "In the interface offsetwrite()";
}

void
ObjectStorMgrI::RedirReadObject(const FDSP_MsgHdrTypePtr &msg_hdr, const FDSP_RedirReadObjTypePtr& redir_read_obj, const Ice::Current&) {
  FDS_PLOG(objStorMgr->GetLog()) << "In the interface redirread()";
}

void
ObjectStorMgrI::AssociateRespCallback(const Ice::Identity& ident, const Ice::Current& current) {
  FDS_PLOG(objStorMgr->GetLog()) << "Associating response Callback client to ObjStorMgr  :" << _communicator->identityToString(ident);

  objStorMgr->fdspDataPathClient = FDSP_DataPathRespPrx::uncheckedCast(current.con->createProxy(ident));
}
//--------------------------------------------------------------------------------------------------
ObjectStorMgr::ObjectStorMgr() {

  // Init  the log infra  
  sm_log = new fds_log("sm", "logs");
  FDS_PLOG(sm_log) << "Constructing the Object Storage Manager";

  // Create all data structures 
  diskMgr = new DiskMgr();

  objStorMutex = new fds_mutex("Object Store Mutex");

  /*
   * Will setup OM comm during run()
   */
  omClient = NULL;
}

ObjectStorMgr::~ObjectStorMgr()
{
  FDS_PLOG(objStorMgr->GetLog()) << " Destructing  the Storage  manager";
  if (objStorDB)
    delete objStorDB;
  if (objIndexDB)
    delete objIndexDB;

  delete diskMgr;
  delete sm_log;
  delete volTbl;
  delete objStorMutex;
}

void ObjectStorMgr::nodeEventOmHandler(int node_id,
                                       unsigned int node_ip_addr,
                                       int node_state,
                                       fds_uint32_t node_port,
                                       FDS_ProtocolInterface::FDSP_MgrIdType node_type)
{
    switch(node_state) {
       case FDS_Node_Up :
           FDS_PLOG(objStorMgr->GetLog()) << "ObjectStorMgr - Node UP event NodeId " << node_id << " Node IP Address " <<  node_ip_addr;
         break;

       case FDS_Node_Down:
       case FDS_Node_Rmvd:
           FDS_PLOG(objStorMgr->GetLog()) << " ObjectStorMgr - Node Down event NodeId :" << node_id << " node IP addr" << node_ip_addr ;
        break;
    }
}


void ObjectStorMgr::volEventOmHandler(fds::fds_volid_t volume_id, fds::VolumeDesc *vdb, int vol_action)
{
    switch(vol_action) {
       case FDS_VOL_ACTION_CREATE :
	 FDS_PLOG(objStorMgr->GetLog()) << "ObjectStorMgr - Volume Create " << volume_id << " Volume Name " <<  (*vdb);
         break;

       case FDS_VOL_ACTION_DELETE:
	 FDS_PLOG(objStorMgr->GetLog()) << " ObjectStorMgr - Volume Delete :" << volume_id << " Volume Name " << (*vdb);
	 break;
    }

}

void ObjectStorMgr::unitTest()
{
  Error err(ERR_OK);

  FDS_PLOG(objStorMgr->GetLog()) << "Running unit test";
  
  /*
   * Create fake objects
   */
  std::string object_data("Hi, I'm object data.");
  FDSP_PutObjType *put_obj_req;
  put_obj_req = new FDSP_PutObjType();

  put_obj_req->volume_offset = 0;
  put_obj_req->data_obj_id.hash_high = 0x00;
  put_obj_req->data_obj_id.hash_low = 0x101;
  put_obj_req->data_obj_len = object_data.length() + 1;
  put_obj_req->data_obj = object_data;

  /*
   * Create fake volume ID
   */
  fds_uint64_t vol_id   = 0xbeef;
  fds_uint32_t num_objs = 1;

  /*
   * Write fake object.
   */
  err = putObjectInternal(put_obj_req, vol_id, num_objs);
  if (err != ERR_OK) {
    FDS_PLOG(objStorMgr->GetLog()) << "Failed to put object ";
    // delete put_obj_req;
    return;
  }
  // delete put_obj_req;

  FDSP_GetObjType *get_obj_req = new FDSP_GetObjType();
  memset((char *)&(get_obj_req->data_obj_id),
	 0x00,
	 sizeof(get_obj_req->data_obj_id));
  get_obj_req->data_obj_id.hash_low = 0x101;
  err = getObjectInternal(get_obj_req, vol_id, 1, num_objs);
  // delete get_obj_req;
}

Error
ObjectStorMgr::checkDuplicate(FDS_ObjectIdType *object_id, 
                              fds_uint32_t obj_len, 
                              fds_char_t *data_object) {
  ObjectID obj_id(object_id->hash_high, object_id->hash_low);
  ObjectBuf obj;
  Error err(ERR_OK);

  obj.size = obj_len;

  objStorMutex->lock();
  err = objStorDB->Get(obj_id, obj);
  objStorMutex->unlock();
  if (err == ERR_OK) {
    /*
     * Perform an inline check that the data is the same.
     * TODO: We need a better solution. This is going to
     * be crazy slow.
     */
    if (memcmp(data_object, obj.data.c_str(), obj_len) == 0 ) { 
      err = ERR_DUPLICATE;
    } else {
      /*
       * Handle hash-collision - insert the next collision-id+obj-id 
       */
      err = ERR_HASH_COLLISION;
    }
  } else if (err == ERR_DISK_READ_FAILED) {
    /*
     * This error indicates the DB entry was empty
     * so we can reset the error to OK.
     */
    err = ERR_OK;
  }

  return err;
}

fds_sm_err_t 
ObjectStorMgr::writeObject(FDS_ObjectIdType *object_id, 
                           fds_uint32_t obj_len, 
                           fds_char_t *data_object, 
                           FDS_DataLocEntry  *data_loc)
{
   //Hash the object_id to DiskNumber, FileName
fds_uint32_t disk_num = 1;

   // Now append the object to the end of this filename
   diskMgr->writeObject(object_id, obj_len, data_object, data_loc, disk_num);

   return FDS_SM_OK;
}

fds_sm_err_t 
ObjectStorMgr::writeObjLocation(FDS_ObjectIdType *object_id, 
                                fds_uint32_t obj_len, 
                                fds_uint32_t volid, 
                                FDS_DataLocEntry *data_loc)
{
  // fds_uint32_t disk_num = 1;
   // Enqueue the object location entry into the thread that maintains global index file
   //disk_mgr_write_obj_loc(object_id, obj_len, volid, data_loc);
  return FDS_SM_OK;
}


/*------------------------------------------------------------------------- ------------
 * FDSP Protocol internal processing 
 -------------------------------------------------------------------------------------*/
Error
ObjectStorMgr::putObjectInternal(FDSP_PutObjTypePtr put_obj_req, 
                                 fds_uint64_t  volid, 
                                 fds_uint32_t num_objs) {
  fds_sm_err_t result = FDS_SM_OK;
  fds::Error err(fds::ERR_OK);

  for(fds_uint32_t obj_num = 0; obj_num < num_objs; obj_num++) {
       // Find if this object is a duplicate
       err = checkDuplicate(&put_obj_req->data_obj_id, 
                            put_obj_req->data_obj_len, 
                            (fds_char_t *)put_obj_req->data_obj.data());

       if (err == ERR_DUPLICATE) {
         FDS_PLOG(objStorMgr->GetLog()) << "Put dup:  " << err
                                        << ", returning success";
         writeObjLocation(&put_obj_req->data_obj_id,
                          put_obj_req->data_obj_len,
                          volid,
                          NULL);
         return err;
       } else if (err != ERR_OK) {
         FDS_PLOG(objStorMgr->GetLog()) << "Failed to put object: " << err;
         return err;
       }

       /*
        * This is the levelDB insertion. It's a totally
        * separate DB from the one above.
        */
       ObjectID obj_id(put_obj_req->data_obj_id.hash_high, put_obj_req->data_obj_id.hash_low);
       ObjectBuf obj;
       obj.size = put_obj_req->data_obj_len;
       obj.data  = put_obj_req->data_obj;

       objStorMutex->lock();
       err = objStorDB->Put( obj_id, obj);
       objStorMutex->unlock();
 
       if (err != fds::ERR_OK) {
         FDS_PLOG(objStorMgr->GetLog()) << "Failed to put object " << err;
         return err;
       } else {
         FDS_PLOG(objStorMgr->GetLog()) << "Successfully put key ";
       }

       volTbl->createVolIndexEntry(volid, 
                                  put_obj_req->volume_offset, 
                                  put_obj_req->data_obj_id,
                                  put_obj_req->data_obj_len);
       // Move the buffer pointer to the next object
       //put_obj_buf += (sizeof(FDSP_PutObjType) + put_obj_req->data_obj_len); 
       //put_obj_req = (FDSP_PutObjType *)put_obj_buf;
   }

   return err;
}

void ObjectStorMgr::PutObject(const FDSP_MsgHdrTypePtr& fdsp_msg, const FDSP_PutObjTypePtr& put_obj_req) {

    // Verify the integrity of the FDSP msg using chksums
    // 
    // stor_mgr_verify_msg(fdsp_msg);
    //put_obj_req->data_obj_id.hash_high = ntohl(put_obj_req->data_obj_id.hash_high);
    //put_obj_req->data_obj_id.hash_low = ntohl(put_obj_req->data_obj_id.hash_low);
    //
    ObjectID oid(put_obj_req->data_obj_id.hash_high,
               put_obj_req->data_obj_id.hash_low);

    FDS_PLOG(objStorMgr->GetLog()) << "PutObject Obj ID:" << oid <<"glob_vol_id:" << fdsp_msg->glob_volume_id << "Num Objs:" << fdsp_msg->num_objects;
    putObjectInternal(put_obj_req, fdsp_msg->glob_volume_id, fdsp_msg->num_objects);
}

Error
ObjectStorMgr::getObjectInternal(FDSP_GetObjTypePtr get_obj_req, 
                                 fds_uint32_t volid, 
                                 fds_uint32_t trans_id, 
                                 fds_uint32_t num_objs) {
  ObjectID obj_id(get_obj_req->data_obj_id.hash_high, get_obj_req->data_obj_id.hash_low);
  ObjectBuf obj;
  obj.size = get_obj_req->data_obj_len;
  obj.data = get_obj_req->data_obj;
  fds::Error err(fds::ERR_OK);

  objStorMutex->lock();
  err = objStorDB->Get(obj_id, obj);
  objStorMutex->unlock();

  if (err != fds::ERR_OK) {
    FDS_PLOG(objStorMgr->GetLog()) << "XID:" << trans_id << "  Failed to get key " << obj_id << " with status " << err;
    get_obj_req->data_obj.assign("");
    return err;
  } else {
    FDS_PLOG(objStorMgr->GetLog()) << "XID:" << trans_id << "Successfully got value ";
    get_obj_req->data_obj.assign(obj.data);
  }

  return err;
}

void 
ObjectStorMgr::GetObject(const FDSP_MsgHdrTypePtr& fdsp_msg,  
                         const FDSP_GetObjTypePtr& get_obj_req) 
{

    // Verify the integrity of the FDSP msg using chksums
    // 
    // stor_mgr_verify_msg(fdsp_msg);
    //
  Error err(ERR_OK);
  ObjectID oid(get_obj_req->data_obj_id.hash_high,
               get_obj_req->data_obj_id.hash_low);

  FDS_PLOG(objStorMgr->GetLog()) << "GetObject  XID:" << fdsp_msg->req_cookie << " Obj ID :" << oid << "glob_vol_id:" << fdsp_msg->glob_volume_id << "Num Objs:" << fdsp_msg->num_objects;
   
  err = getObjectInternal(get_obj_req,
                          fdsp_msg->glob_volume_id,
                          fdsp_msg->req_cookie,
                          fdsp_msg->num_objects);
  if (err != ERR_OK) {
    fdsp_msg->result = FDSP_ERR_FAILED;
    fdsp_msg->err_code = err.getIceErr();
  } else {
    fdsp_msg->result = FDSP_ERR_OK;
  }
}

inline void ObjectStorMgr::swapMgrId(const FDSP_MsgHdrTypePtr& fdsp_msg) {
 FDSP_MgrIdType temp_id;
 long tmp_addr;
 fds_uint32_t tmp_port;

 temp_id = fdsp_msg->dst_id;
 fdsp_msg->dst_id = fdsp_msg->src_id;
 fdsp_msg->src_id = temp_id;

 tmp_addr = fdsp_msg->dst_ip_hi_addr;
 fdsp_msg->dst_ip_hi_addr = fdsp_msg->src_ip_hi_addr;
 fdsp_msg->src_ip_hi_addr = tmp_addr;

 tmp_addr = fdsp_msg->dst_ip_lo_addr;
 fdsp_msg->dst_ip_lo_addr = fdsp_msg->src_ip_lo_addr;
 fdsp_msg->src_ip_lo_addr = tmp_addr;

 tmp_port = fdsp_msg->dst_port;
 fdsp_msg->dst_port = fdsp_msg->src_port;
 fdsp_msg->src_port = tmp_port;
}


/*------------------------------------------------------------------------------------- 
 * Storage Mgr main  processor : Listen on the socket and spawn or assign thread from a pool
 -------------------------------------------------------------------------------------*/
int
ObjectStorMgr::run(int argc, char* argv[])
{

  bool         unit_test;
  std::string endPointStr;
  fds::DmDiskInfo     *info;
  fds::DmDiskQuery     in;
  fds::DmDiskQueryOut  out;
  
  unit_test = false;
  std::string  omIpStr;
  fds_uint32_t omConfigPort;

  omConfigPort = 0;

  for (int i = 1; i < argc; i++) {
    std::string arg(argv[i]);
    if (arg == "--unit_test") {
      unit_test = true;
    } else if (strncmp(argv[i], "--cp_port=", 10) == 0) {
      cp_port_num = strtoul(argv[i] + 10, NULL, 0);
    } else if (strncmp(argv[i], "--port=", 7) == 0) {
      port_num = strtoul(argv[i] + 7, NULL, 0);
    } else if (strncmp(argv[i], "--om_ip=", 8) == 0) {
      omIpStr = argv[i] + 8;
    } else if (strncmp(argv[i], "--om_port=", 10) == 0) {
      omConfigPort = strtoul(argv[i] + 10, NULL, 0);
    } else if (strncmp(argv[i], "--prefix=", 9) == 0) {
      stor_prefix = argv[i] + 9;
    } else {
       FDS_PLOG(objStorMgr->GetLog()) << "Invalid argument " << argv[i];
      return -1;
    }
  }    

 // Create leveldb
  std::string filename= stor_prefix + "SNodeObjRepository";
  objStorDB  = new ObjectDB(filename);
  filename= stor_prefix + "SNodeObjIndex";
  objIndexDB  = new ObjectDB(filename); 

  Ice::PropertiesPtr props = communicator()->getProperties();

  if (cp_port_num == 0) {
    cp_port_num = props->getPropertyAsInt("ObjectStorMgrSvr.ControlPort");
  }

  if (port_num == 0) {
    /*
     * Pull the port from the config file if it wasn't
     * specified on the command line.
     */
    port_num = props->getPropertyAsInt("ObjectStorMgrSvr.PortNumber");
  }

  if (unit_test) {
    objStorMgr->unitTest();
    return 0;
  }

  FDS_PLOG(objStorMgr->GetLog()) << "Stor Mgr port_number :" << port_num;
  
  
  /*
   * Set basic thread properties.
   */
  props->setProperty("ObjectStorMgrSvr.ThreadPool.Client.Size", "200");
  props->setProperty("ObjectStorMgrSvr.ThreadPool.Client.SizeMax", "400");
  props->setProperty("ObjectStorMgrSvr.ThreadPool.Client.SizeWarn", "300");

  props->setProperty("ObjectStorMgrSvr.ThreadPool.Server.Size", "200");
  props->setProperty("ObjectStorMgrSvr.ThreadPool.Server.SizeMax", "400");
  props->setProperty("ObjectStorMgrSvr.ThreadPool.Server.SizeWarn", "300");

  std::ostringstream tcpProxyStr;
  tcpProxyStr << "tcp -p " << port_num;
  
  Ice::ObjectAdapterPtr adapter = communicator()->createObjectAdapterWithEndpoints("ObjectStorMgrSvr", tcpProxyStr.str());
  fdspDataPathServer = new ObjectStorMgrI(communicator());
  adapter->add(fdspDataPathServer, communicator()->stringToIdentity("ObjectStorMgrSvr"));

  callbackOnInterrupt();
  
  adapter->activate();

  volTbl = new StorMgrVolumeTable(this);

  struct ifaddrs *ifAddrStruct = NULL;
  struct ifaddrs *ifa          = NULL;
  void   *tmpAddrPtr           = NULL;

  /*
   * Get the local IP of the host.
   * This is needed by the OM.
   */
  getifaddrs(&ifAddrStruct);
  for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr->sa_family == AF_INET) { // IPv4
      if (strncmp(ifa->ifa_name, "lo", 2) != 0) {
          tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
          char addrBuf[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, tmpAddrPtr, addrBuf, INET_ADDRSTRLEN);
          myIp = std::string(addrBuf);

      }
    }
  }
  assert(myIp.empty() == false);
  FDS_PLOG(objStorMgr->GetLog()) << "Stor Mgr IP:" << myIp;

  /*
   * Query  Disk Manager  for disk parameter details 
   */
    fds::DmQuery        &query = fds::DmQuery::dm_query();
    in.dmq_mask = fds::dmq_disk_info;
    query.dm_disk_query(in, &out);
    /* we should be bundling multiple disk  parameters  into one message to OM TBD */ 
    dInfo = new  FDSP_AnnounceDiskCapability(); 
    while (1) {
        info = out.query_pop();
        if (info != nullptr) {
  	    FDS_PLOG(objStorMgr->GetLog()) << "Max blks capacity: " << info->di_max_blks_cap
            << "Disk type........: " << info->di_disk_type
            << "Max iops.........: " << info->di_max_iops
            << "Min iops.........: " << info->di_min_iops
            << "Max latency (us).: " << info->di_max_latency
            << "Min latency (us).: " << info->di_min_latency;

            if ( info->di_disk_type == FDS_DISK_SATA) {
            	dInfo->disk_iops_max =  info->di_max_iops; /*  max avarage IOPS */
            	dInfo->disk_iops_min =  info->di_min_iops; /* min avarage IOPS */
            	dInfo->disk_capacity = info->di_max_blks_cap;  /* size in blocks */
            	dInfo->disk_latency_max = info->di_max_latency; /* in us second */
            	dInfo->disk_latency_min = info->di_min_latency; /* in us second */
  	    } else if (info->di_disk_type == FDS_DISK_SSD) {
            	dInfo->ssd_iops_max =  info->di_max_iops; /*  max avarage IOPS */
            	dInfo->ssd_iops_min =  info->di_min_iops; /* min avarage IOPS */
            	dInfo->ssd_capacity = info->di_max_blks_cap;  /* size in blocks */
            	dInfo->ssd_latency_max = info->di_max_latency; /* in us second */
            	dInfo->ssd_latency_min = info->di_min_latency; /* in us second */
	    } else 
  	       FDS_PLOG(objStorMgr->GetLog()) << "Unknown Disk Type " << info->di_disk_type;
			
            delete info;
            continue;
        }
        break;
    }

  /*
   * Register this node with OM.
   */
  omClient = new OMgrClient(FDSP_STOR_MGR,
                            omIpStr,
                            omConfigPort,
                            myIp,
                            port_num,
                            stor_prefix + "localhost-sm",
                            sm_log);
  omClient->initialize();
  omClient->registerEventHandlerForNodeEvents((node_event_handler_t)nodeEventOmHandler);
  omClient->registerEventHandlerForVolEvents((volume_event_handler_t)volEventOmHandler);
  omClient->startAcceptingControlMessages(cp_port_num);
  omClient->registerNodeWithOM(dInfo);

  communicator()->waitForShutdown();

  if (ifAddrStruct != NULL) {
    freeifaddrs(ifAddrStruct);
  }

  return EXIT_SUCCESS;
}

fds_log* ObjectStorMgr::GetLog() {
  return sm_log;
}


void
ObjectStorMgr::interruptCallback(int)
{
    communicator()->shutdown();
}
}  // namespace fds

int main(int argc, char *argv[])
{
  objStorMgr = new ObjectStorMgr();

  objStorMgr->main(argc, argv, "stor_mgr.conf");

  delete objStorMgr;
}

