/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#include <stor_hvisor_ice/VolumeCatalogCache.h>
#include <stor_hvisor_ice/StorHvisorNet.h>

#include <iostream>  // NOLINT(*)
#include <vector>
#include <string>
#include <list>

#include "util/Log.h"
#include "util/concurrency/Thread.h"
/*
 * Piggyback on the global storHvisor that's been
 * declared elsewhere.
 */
extern StorHvCtrl *storHvisor;

namespace fds {

class VccUnitTest {
 private:
  std::list<std::string>  unit_tests;

  fds_log *vcc_log;

  /*
   * Unit test funtions
   */
  fds_int32_t basic_update() {
    Error err(ERR_OK);
    
    VolumeCatalogCache vcc(storHvisor,
                           vcc_log);
    fds_uint64_t vol_uuid;
    
    vol_uuid = 987654321;
    
    err = vcc.RegVolume(vol_uuid);
    if (!err.ok()) {
      std::cout << "Failed to register volume cache "
                << vol_uuid << std::endl;
      return -1;
    }
    
    for (fds_uint32_t i = 0; i < 2; i++) {
      fds_uint64_t block_id = 1 + i;
      ObjectID oid(block_id, (block_id * i));
      err = vcc.Update(vol_uuid, block_id, oid);
      if (!err.ok() && err != ERR_PENDING_RESP) {
        std::cout << "Failed to update volume cache "
                  << vol_uuid << std::endl;
        return -1;
      }
    }

    return 0;
  }

  fds_int32_t basic_query() {
    Error err(ERR_OK);
    
    VolumeCatalogCache vcc(storHvisor,
                           vcc_log);
    fds_uint64_t vol_uuid;
    
    vol_uuid = 987654321;
    
    err = vcc.RegVolume(vol_uuid);
    if (!err.ok()) {
      std::cout << "Failed to register volume cache "
                << vol_uuid << std::endl;
      return -1;
    }
    
    for (fds_uint32_t i = 0; i < 2; i++) {
      fds_uint64_t block_id = 1 + i;
      ObjectID oid;
      err = vcc.Query(vol_uuid, block_id, 0, &oid);
      if (!err.ok() && err != ERR_PENDING_RESP) {
        std::cout << "Failed to query volume cache "
                  << vol_uuid << std::endl;
        return -1;
      }
    }

    return 0;
  }
  
  /*
   * Thread helper functions
   */
  int shared_update(VolumeCatalogCache *vcc,
                    fds_uint64_t vol_uuid) {
    Error err(ERR_OK);
    fds_uint32_t num_updates = 2000;

    for (fds_uint32_t i = 0; i < num_updates; i++) {
      fds_uint64_t block_id = 1 + i;
      ObjectID oid(block_id, (block_id * i));
      err = vcc->Update(vol_uuid, block_id, oid);
      if (!err.ok() && err != ERR_PENDING_RESP) {
        FDS_PLOG(vcc_log) << "Failed to update volume cache "
                          << vol_uuid << std::endl;
        return -1;
      }
    }

    return 0;
  }

  int shared_clear(VolumeCatalogCache *vcc,
                   fds_uint64_t vol_uuid) {
    vcc->Clear(vol_uuid);

    return 0;
  }

  static void update_mt_wrapper(VccUnitTest *ut,
                                fds_uint32_t id,
                                VolumeCatalogCache *vcc,
                                fds_uint64_t vol_uuid) {
    int res = ut->shared_update(vcc, vol_uuid);
    if (res == 0) {
      FDS_PLOG(ut->GetLog()) << "A update thread " << id
                             << " completed successfully.";
    } else {
      FDS_PLOG(ut->GetLog()) << "A update thread " << id
                             << " completed with error.";
    }
  }

  static void clear_mt_wrapper(VccUnitTest *ut,
                               fds_uint32_t id,
                               VolumeCatalogCache *vcc,
                               fds_uint64_t vol_uuid) {
    /*
     * Wait for a sec to clear the cache so that some
     * updates can be put into it.
     */
    boost::xtime xt;
    boost::xtime_get(&xt, boost::TIME_UTC_);
    xt.sec += 1;
    boost::thread::sleep(xt);

    int res = ut->shared_clear(vcc, vol_uuid);
    if (res == 0) {
      FDS_PLOG(ut->GetLog()) << "A clear thread " << id
                             << " completed successfully.";
    } else {
      FDS_PLOG(ut->GetLog()) << "A clear thread " << id
                             << " completed with error.";
    }
  }

  boost::thread* start_update_thread(fds_uint32_t id,
                                     VolumeCatalogCache *vcc,
                                     fds_uint64_t vol_uuid) {
    boost::thread *_t;
    _t = new boost::thread(boost::bind(&update_mt_wrapper,
                                       this,
                                       id,
                                       vcc,
                                       vol_uuid));
    
    return _t;
  }

  boost::thread* start_clear_thread(fds_uint32_t id,
                                    VolumeCatalogCache *vcc,
                                    fds_uint64_t vol_uuid) {
    boost::thread *_t;
    _t = new boost::thread(boost::bind(&clear_mt_wrapper,
                                       this,
                                       id,
                                       vcc,
                                       vol_uuid));
    
    return _t;
  }

  /*
   * Basic multi-threaded test.
   */
  fds_int32_t basic_mt() {
    Error err(ERR_OK);

    std::vector<boost::thread*> threads;
    fds_uint32_t num_threads = 20;

    /*
     * Make a shared cache that all access
     * the same volume.
     */
    VolumeCatalogCache vcc(storHvisor,
                           vcc_log);

    fds_uint64_t vol_uuid = 987654321;

    for (fds_uint32_t i = 0; i < num_threads; i++) {
      threads.push_back(start_update_thread(i, &vcc, vol_uuid));
      threads.push_back(start_clear_thread(i, &vcc, vol_uuid));
    }

    for (fds_uint32_t i = 0; i < num_threads; i++) {
      threads[i]->join();
    }

    return 0;
  }

 public:
  VccUnitTest() {
    vcc_log = new fds_log("vcc_test", "logs");

    unit_tests.push_back("basic_update");
    unit_tests.push_back("basic_query");
    unit_tests.push_back("basic_mt");
    
    /*
     * Create the SH control. Pass some empty cmdline args.
     */
    int argc = 0;
    char* argv[argc];
    storHvisor = new StorHvCtrl(argc, argv, StorHvCtrl::DATA_MGR_TEST);
  }

  ~VccUnitTest() {
    delete vcc_log;    
    delete storHvisor;
  }

  fds_int32_t Run(const std::string& testname) {
    int result = 0;
    std::cout << "Running unit test \"" << testname << "\"" << std::endl;

    if (testname == "basic_update") {
      result = basic_update();
    } else if (testname == "basic_query") {
      result = basic_query();
    } else if (testname == "basic_mt") {
      result = basic_mt();
    } else {
      std::cout << "Unknown unit test " << testname << std::endl;
    }

    if (result == 0) {
      std::cout << "Unit test \"" << testname << "\" PASSED"  << std::endl;
    } else {
      std::cout << "Unit test \"" << testname << "\" FAILED" << std::endl;
    }
    std::cout << std::endl;

    return result;
  }

  void Run() {
    fds_int32_t result = 0;
    for (std::list<std::string>::iterator it = unit_tests.begin();
         it != unit_tests.end();
         ++it) {
      result = Run(*it);
      if (result != 0) {
        std::cout << "Unit test FAILED" << std::endl;
        break;
      }
    }

    if (result == 0) {
      std::cout << "Unit test PASSED" << std::endl;
    }
  }

  fds_log* GetLog() {
    return vcc_log;
  }
};

/*
 * Ice communication classes
 */
class ShClientCb : public FDS_ProtocolInterface::FDSP_DataPathResp {
 public:
  void GetObjectResp(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr&,
                     const FDS_ProtocolInterface::FDSP_GetObjTypePtr&,
                     const Ice::Current&) {
  }

  void PutObjectResp(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr&,
                     const FDS_ProtocolInterface::FDSP_PutObjTypePtr&,
                     const Ice::Current&) {
  }

  void UpdateCatalogObjectResp(const FDS_ProtocolInterface::FDSP_MsgHdrTypePtr&
                               fdsp_msg,
                               const
                               FDS_ProtocolInterface::FDSP_UpdateCatalogTypePtr&
                               cat_obj_req,
                               const Ice::Current &) {
    std::cout << "Got a update catalog response!" << std::endl;
  }

  void QueryCatalogObjectResp(const
                              FDS_ProtocolInterface::FDSP_MsgHdrTypePtr&
                              fdsp_msg,
                               const
                              FDS_ProtocolInterface::FDSP_QueryCatalogTypePtr&
                              cat_obj_req,
                              const Ice::Current &) {
    ObjectID oid(cat_obj_req->data_obj_id.hash_high,
                 cat_obj_req->data_obj_id.hash_high);
    std::cout << "Got a response query catalog for object " << oid << std::endl;
  }

  void OffsetWriteObjectResp(const
                             FDS_ProtocolInterface::FDSP_MsgHdrTypePtr&
                             fdsp_msg,
                             const
                             FDS_ProtocolInterface::FDSP_OffsetWriteObjTypePtr&
                             offset_write_obj_req,
                             const Ice::Current &) {
  }
  void RedirReadObjectResp(const
                           FDS_ProtocolInterface::FDSP_MsgHdrTypePtr&
                           fdsp_msg,
                           const
                           FDS_ProtocolInterface::FDSP_RedirReadObjTypePtr&
                           redir_write_obj_req,
                           const Ice::Current &) {
  }
};

class ShClient : public Ice::Application {
 public:
  ShClient() {
  }
  ~ShClient() {
  }

  /*
   * Ice will execute the application via run()
   */
  int run(int argc, char* argv[]) {
    /*
     * Process the cmdline args.
     */
    std::string testname;
    for (int i = 1; i < argc; i++) {
      if (strncmp(argv[i], "--testname=", 11) == 0) {
        testname = argv[i] + 11;
      } else {
        std::cout << "Invalid argument " << argv[i] << std::endl;
        return -1;
      }
    }

    /*
     * Setup the basic unit test.
     */
    VccUnitTest unittest;

    if (testname.empty()) {
      unittest.Run();
    } else {
      unittest.Run(testname);
    }

    return 0;
  }

 private:
};

}  // namespace fds

int main(int argc, char* argv[]) {
  fds::ShClient sh_client;

  return sh_client.main(argc, argv, "fds.conf");
}
