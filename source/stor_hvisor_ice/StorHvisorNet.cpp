#include <Ice/Ice.h>
#include <FDSP.h>
#include <ObjectFactory.h>
#include <Ice/ObjectFactory.h>
#include <Ice/BasicStream.h>
#include <Ice/Object.h>
#include <IceUtil/Iterator.h>
#include "StorHvisorNet.h"
//#include "hvisor_lib.h"
#include "StorHvisorCPP.h"

StorHvCtrl *storHvisor;
using namespace std;
using namespace FDS_ProtocolInterface;
using namespace Ice;

struct fbd_device *fbd_dev;
extern vvc_vhdl_t vvc_vol_create(volid_t vol_id, const char *db_name, int max_blocks);

/*
 * Globals being used for the unit test.
 * TODO: This should be cleaned up into
 * unit test specific class. Keeping them
 * global is kinda hackey.
 */
fds_notification notifier;
fds_mutex map_mtx("map mutex");
std::map<fds_uint32_t, std::string> written_data;
std::map<fds_uint32_t, fds_bool_t> verified_data;

static void sh_test_w_callback(void *arg1,
                               void *arg2,
                               fbd_request_t *w_req,
                               int res) {
  std::cerr << "SH test received write callback" << std::endl;

  map_mtx.lock();
  /*
   * Copy the write buffer for verification later.
   */
  written_data[w_req->op] = std::string(w_req->buf,
                                        w_req->sec *
                                        HVISOR_SECTOR_SIZE);
  verified_data[w_req->op] = false;
  map_mtx.unlock();

  notifier.notify();
  
  delete w_req->buf;
  delete w_req;
}

static void sh_test_r_callback(void *arg1,
                               void *arg2,
                               fbd_request_t *r_req,
                               int res) {
  std::cerr << "SH test received read callback" << std::endl;

  /*
   * Verify the read's contents.
   */
  
  map_mtx.lock();
  if (written_data[r_req->op].compare(0,
                                      r_req->len,
                                      r_req->buf)) {
    verified_data[r_req->op] = true;
  } else {
    verified_data[r_req->op] = false;
  }
  map_mtx.unlock();

  notifier.notify();
  
  /*
   * We're not freeing the buffer here. We'll
   * free it once a read verifies its contents.
   */
  delete r_req->buf;
  delete r_req;
}

int unitTest() {
  fbd_request_t *w_req;
  fbd_request_t *r_req;
  fds_uint32_t req_size;
  char *w_buf;
  char *r_buf;
  fds_uint32_t w_count;
  fds_int32_t result = 0;

  req_size = 4096;
  w_count  = 100;

  for (fds_uint32_t i = 0; i < w_count; i++) {
    /*
     * Note the buf and request are freed by
     * the callback handler.
     */
    w_req = new fbd_request_t;
    w_buf = new char[req_size]();

    /*
     * Select a byte string to fill the
     * buffer with.
     * TODO: Let's get more random data
     * than this.
     */
    if (i % 3 == 0) {
      memset(w_buf, 0xbeef, req_size);
    } else if (i % 2 == 0) {
      memset(w_buf, 0xdead, req_size);
    } else {
      memset(w_buf, 0xfeed, req_size);
    }
    
    /*
     * TODO: We're currently overloading the
     * op field to denote which I/O request
     * this is. The field isn't used, so we can
     * later remove it and use a better way to
     * identify the I/O.
     */
    w_req->op   = i;
    w_req->buf  = w_buf;
    /*
     * Set the offset to be based
     * on the iteration.
     */
    w_req->sec  = i;
    w_req->secs = req_size / HVISOR_SECTOR_SIZE;
    
    StorHvisorProcIoWr(NULL,
                       w_req,
                       sh_test_w_callback,
                       NULL,
                       NULL);

    /*
     * Wait until we've finished this write
     */
    while (1) {
      notifier.wait_for_notification();
      map_mtx.lock();
      if (written_data.size() == i + 1) {
        map_mtx.unlock();
        break;
      }
      map_mtx.unlock();
    }
    std::cerr << "Finished write " << i << std::endl;
  }

  std::cerr << "Finished all " << w_count << " writes" << std::endl;

  for (fds_uint32_t i = 0; i < w_count; i++) {
    /*
     * Note the buf and request are freed by
     * the callback handler.
     */
    r_req = new fbd_request_t;
    r_buf = new char[req_size]();

    /*
     * TODO: We're currently overloading the
     * op field to denote which I/O request
     * this is. The field isn't used, so we can
     * later remove it and use a better way to
     * identify the I/O.
     */
    r_req->op   = i;
    r_req->buf  = r_buf;
    /*
     * Set the offset to be based
     * on the iteration.
     */
    r_req->sec  = i;
    /*
     * TODO: Do we need both secs and len? What's
     * the difference?
     */
    r_req->secs = req_size / HVISOR_SECTOR_SIZE;
    r_req->len  = req_size;
    
    StorHvisorProcIoRd(NULL,
                       r_req,
                       sh_test_r_callback,
                       NULL,
                       NULL);

    /*
     * Wait until we've finished this read
     */
    while (1) {
      notifier.wait_for_notification();
      map_mtx.lock();
      if (verified_data[i] == true) {
        std::cerr << "Read " << i << " verified" << std::endl;
      } else {
        std::cerr << "Read " << i << " FAILED verification" << std::endl;
        result = -1;
      }
      map_mtx.unlock();
      break;
    }

    if (result != 0) {
      break;
    }

    std::cerr << "Finished read " << i << std::endl;
  }
  std::cerr << "Finished all " << w_count << " reads" << std::endl;  
  
  return result;
}


void CreateStorHvisor(int argc, char *argv[])
{
  CreateSHMode(argc, argv, false, 0, 0);
}

void CreateSHMode(int argc,
                  char *argv[],
                  fds_bool_t test_mode,
                  fds_uint32_t sm_port,
                  fds_uint32_t dm_port)
{
  if (test_mode == true) {
    storHvisor = new StorHvCtrl(argc, argv, StorHvCtrl::TEST_BOTH, sm_port, dm_port);
  } else {
    storHvisor = new StorHvCtrl(argc, argv, StorHvCtrl::NORMAL);
  }
  std::cout << "Created storHvisor " << (void *)storHvisor
            << " with journal table " << (void *)storHvisor->journalTbl
            << std::endl;
}

StorHvCtrl::StorHvCtrl(int argc,
                       char *argv[],
                       sh_comm_modes _mode,
                       fds_uint32_t sm_port_num,
                       fds_uint32_t dm_port_num)
    : mode(_mode) {
  
  Ice::InitializationData initData;
  initData.properties = Ice::createProperties();
  initData.properties->load("fds.conf");
  _communicator = Ice::initialize(argc, argv, initData);
  Ice::PropertiesPtr props = _communicator->getProperties();

  rpcSwitchTbl = new FDS_RPC_EndPointTbl(_communicator);
  journalTbl = new StorHvJournal(FDS_READ_WRITE_LOG_ENTRIES);
  volCatalogCache = new VolumeCatalogCache(this);
  
  /*
   * Set basic thread properties.
   */
  props->setProperty("StorHvisorClient.ThreadPool.Size", "10");
  props->setProperty("StorHvisorClient.ThreadPool.SizeMax", "20");
  props->setProperty("StorHvisorClient.ThreadPool.SizeWarn", "18");
  
  /*
   * Parse options out of config file
   */

  /*
   * Setup RPC endpoints based on comm mode.
   */
  std::string dataMgrIPAddress;
  int dataMgrPortNum;
  std::string storMgrIPAddress;
  int storMgrPortNum;
  if ((mode == DATA_MGR_TEST) ||
      (mode == TEST_BOTH) ||
      (mode == NORMAL)) {
    /*
     * If a port_num to use is set use it,
     * otherwise pull from config file.
     */
    if (dm_port_num != 0) {
      dataMgrPortNum = dm_port_num;
    } else {
      dataMgrPortNum = props->getPropertyAsInt("DataMgr.PortNumber");
    }
    dataMgrIPAddress = props->getProperty("DataMgr.IPAddress");
    rpcSwitchTbl->Add_RPC_EndPoint(dataMgrIPAddress, dataMgrPortNum, FDSP_DATA_MGR);
  }
  if ((mode == STOR_MGR_TEST) ||
      (mode == TEST_BOTH) ||
      (mode == NORMAL)) {
    if (sm_port_num != 0) {
      storMgrPortNum = sm_port_num;
    } else {
      storMgrPortNum  = props->getPropertyAsInt("ObjectStorMgrSvr.PortNumber");
    }
    storMgrIPAddress  = props->getProperty("ObjectStorMgrSvr.IPAddress");
    rpcSwitchTbl->Add_RPC_EndPoint(storMgrIPAddress, storMgrPortNum, FDSP_STOR_MGR);
  }
  
  if ((mode == DATA_MGR_TEST) ||
      (mode == TEST_BOTH)) {
    /*
     * TODO: Currently we always add the DM IP in the DM and BOTH test modes.
     */
    fds_uint32_t ip_num = FDS_RPC_EndPoint::ipString2Addr(dataMgrIPAddress);
    dataPlacementTbl  = new StorHvDataPlacement(StorHvDataPlacement::DP_NO_OM_MODE,
                                                ip_num);
  } else if (mode == STOR_MGR_TEST) {
    fds_uint32_t ip_num = FDS_RPC_EndPoint::ipString2Addr(storMgrIPAddress);
    dataPlacementTbl  = new StorHvDataPlacement(StorHvDataPlacement::DP_NO_OM_MODE,
                                                ip_num);
  } else {
cout <<" Entring Normal Data placement mode" << endl;
    dataPlacementTbl  = new StorHvDataPlacement(StorHvDataPlacement::DP_NORMAL_MODE);
  }
}

/*
 * Constructor uses comm with DM and SM if no mode provided.
 */
StorHvCtrl::StorHvCtrl(int argc, char *argv[])
    : StorHvCtrl(argc, argv, NORMAL, 0, 0) {
}

StorHvCtrl::StorHvCtrl(int argc,
                       char *argv[],
                       sh_comm_modes _mode)
    : StorHvCtrl(argc, argv, _mode, 0, 0) {
}

StorHvCtrl::~StorHvCtrl()
{
}

BEGIN_C_DECLS
void *hvisor_lib_init(void)
{
  // int err = -ENOMEM;
//        int rc;

        fbd_dev = (fbd_device *)malloc(sizeof(*fbd_dev));
        if (!fbd_dev)
          return (void *)-ENOMEM;
        memset(fbd_dev, 0, sizeof(*fbd_dev));

        fbd_dev->blocksize = 4096; /* default value  */
        fbd_dev->bytesize = 0;
        /* we will have to  generate the dev id for multiple device support */
        fbd_dev->dev_id = 0;

        fbd_dev->vol_id = 1; /*  this should be comming from OM */
#if 0
        if((fbd_dev->vhdl = vvc_vol_create(fbd_dev->vol_id, NULL,8192)) == 0 )
        {
                printf(" Error: creating the  vvc  volume \n");
                goto out;
        }


        rc = pthread_create(&fbd_dev->rx_thread, NULL, fds_rx_io_proc, fbd_dev);
        if (rc) {
          printf(" Error: creating the   RX IO thread : %d \n", rc);
                goto out;
        }
#endif

        return (fbd_dev);
        // return (void *)err;
}
END_C_DECLS

