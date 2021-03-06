/**
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <unistd.h>

#include <vector>
#include <chrono>
#include <thread>
#include <string>

#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>

#include <fds_assert.h>
#include <fds_process.h>
#include <ObjectId.h>
#include <FdsRandom.h>
#include <StatsCollector.h>
#include <ObjMeta.h>
#include <object-store/SmDiskMap.h>
#include <object-store/ObjectMetaDb.h>
#include <object-store/ObjectMetadataStore.h>
#include <object-store/ObjectMetaCache.h>

#include <sm_ut_utils.h>

static fds::SmDiskMap::ptr smDiskMap;

namespace fds {

/**
 * Unit test for both ObjectMetaDb and ObjectMetadataStore (the type of
 * test is set in sm_ut.conf config file)
 * This unit test is intended to be high-load test which could also be used
 * for performance testing of ObjectMetaDB and ObjectMetadataStore
 * Unit test runs meta_ut.num_threads number of threads and does
 * meta_ut.num_ops operations of type meta_ut.op_type (get/put/delete)
 * on ObjectMetaDb or ObjectMetadataStore;
 */
class MetaStoreUTProc : public FdsProcess {
  public:
    MetaStoreUTProc(int argc, char * argv[], const std::string & config,
                    const std::string & basePath, Module * vec[]);
    virtual ~MetaStoreUTProc();

    virtual int run() override;

    virtual void task(int id);

  private:
    typedef enum {
        UT_OP_GET,
        UT_OP_PUT,
        UT_OP_RMW,
        UT_OP_DELETE
    } MetaOpType;

    typedef enum {
        UT_SMOKE,
        UT_LOAD
    } TestType;

    /* helper methods */
    MetaOpType getOpTypeFromName(std::string& name);
    TestType getTestTypeFromName(std::string& name);
    fds_uint32_t getObjSize(fds_uint32_t rnum);
    void setMetaLoc(fds_uint32_t rnum,
                    obj_phy_loc_t* loc);
    ObjMetaData::ptr allocRandomObjMeta(fds_uint32_t rnum,
                                        const ObjectID& objId);
    fds_bool_t isValidObjMeta(ObjMetaData::const_ptr meta,
                              fds_uint32_t rnum);
    Error openMetadataDB();
    Error populateStore();

    /* tests */
    int runSmokeTest();

    // wrappers for get/put/delete
    Error get(uint64_t volId,
              const ObjectID& objId,
              ObjMetaData::const_ptr &objMeta);
    Error put(uint64_t volId,
              const ObjectID& objId,
              ObjMetaData::const_ptr objMeta);
    Error remove(uint64_t volId,
                 const ObjectID& objId);

  private:
    /**
     * Params from from config file
     */
    TestType test_type_;
    fds_uint32_t num_threads_;
    fds_uint32_t dataset_size_;
    fds_uint32_t num_ops_;
    MetaOpType op_type_;

    std::vector<std::thread*> threads_;
    std::atomic<fds_uint32_t> op_count;
    std::atomic<fds_bool_t> test_pass_;

    /* dataset we will use for testing
     * If operations are gets, we will first
     * populate DB with generated dataset, and then
     * do gets on DB; same for deletes
     */
    std::vector<ObjectID> dataset_;
    /* dataset object id -> random number
    * random number is used to recreate metadata entry, so we
    * can check correctness*/
    std::unordered_map<ObjectID, fds_uint32_t, ObjectHash> dataset_map_;

    // one of these will be created
    // based on what we are testing
    ObjectMetadataDb* db_;
    ObjectMetadataStore* store_;
    ObjectMetaCache* cache_;
};

MetaStoreUTProc::TestType
MetaStoreUTProc::getTestTypeFromName(std::string& name) {
    if (0 == name.compare(0, 5, "smoke")) return UT_SMOKE;
    if (0 == name.compare(0, 4, "load")) return UT_LOAD;
    return UT_SMOKE;
}

MetaStoreUTProc::MetaOpType
MetaStoreUTProc::getOpTypeFromName(std::string& name) {
    if (0 == name.compare(0, 3, "get")) return UT_OP_GET;
    if (0 == name.compare(0, 3, "put")) return UT_OP_PUT;
    if (0 == name.compare(0, 3, "rmw")) return UT_OP_RMW;
    if (0 == name.compare(0, 6, "delete")) return UT_OP_DELETE;
    fds_verify(false);
    return UT_OP_GET;
}

MetaStoreUTProc::MetaStoreUTProc(int argc, char * argv[], const std::string & config,
                                 const std::string & basePath, Module * vec[])
        : FdsProcess(argc, argv, config, basePath, vec),
          db_(NULL), store_(NULL), cache_(NULL) {
    FdsConfigAccessor conf(get_conf_helper());
    std::string teststr = conf.get<std::string>("test_type");
    std::string whatstr = conf.get<std::string>("test_what");
    std::string opstr = conf.get<std::string>("op_type");

    test_type_ = getTestTypeFromName(teststr);
    op_type_ = getOpTypeFromName(opstr);
    num_threads_ = conf.get<fds_uint32_t>("num_threads");
    num_ops_ = conf.get<fds_uint32_t>("num_ops");
    dataset_size_ = conf.get<fds_uint32_t>("dataset_size");

    if (test_type_ == UT_SMOKE) {
        std::cout << "Will run smoke test..." << std::endl;
        LOGNOTIFY << "Will run smoke test...";
    } else {
        std::cout << "Will execute " << num_ops_ << " operations of type "
                  << op_type_ << " while running " << num_threads_ << " threads"
                  << ", dataset size " << dataset_size_ << " objects" << std::endl;
        LOGNOTIFY << "Will execute " << num_ops_ << " operations of type "
                  << op_type_ << " while running " << num_threads_ << " threads"
                  << ", dataset size " << dataset_size_ << " objects";
    }

    if (0 == whatstr.compare(0, 7, "meta_db")) {
        db_ = new ObjectMetadataDb();
        db_->setNumBitsPerToken(16);
        std::cout << "Will test ObjectMetadataDb" << std::endl;
        LOGNOTIFY << "Will test ObjectMetadataDb";
    } else if (0 == whatstr.compare(0, 10, "meta_store")) {
        store_ = new ObjectMetadataStore("Object Metadata Store UT");
        store_->mod_init(NULL);
        store_->mod_startup();
        store_->setNumBitsPerToken(16);
        std::cout << "Will test ObjectMetadataStore" << std::endl;
        LOGNOTIFY << "Will test ObjectMetadataStore";
    } else if (0 == whatstr.compare(0, 10, "meta_cache")) {
        cache_ = new ObjectMetaCache("Object Metadata Cache UT");
        cache_->mod_init(NULL);
        cache_->mod_startup();
        std::cout << "Will test ObjectMetaCache" << std::endl;
        LOGNOTIFY << "Will test ObjectMetaCache";
    } else {
        fds_panic("unknown module to test!");
    }

    // generate dataset of unique objIds
    fds_uint64_t seed = RandNumGenerator::getRandSeed();
    RandNumGenerator rgen(seed);
    fds_uint32_t rnum = (fds_uint32_t)rgen.genNum();
    for (fds_uint32_t i = 0; i < dataset_size_; ++i) {
        std::string obj_data = std::to_string(rnum);
        ObjectID oid = ObjIdGen::genObjectId(obj_data.c_str(), obj_data.size());
        // we want every object ID in the dataset to be unique
        while (dataset_map_.count(oid) > 0) {
            rnum = (fds_uint32_t)rgen.genNum();
            obj_data = std::to_string(rnum);
            oid = ObjIdGen::genObjectId(obj_data.c_str(), obj_data.size());
        }
        dataset_.push_back(oid);
        dataset_map_[oid] = rnum;
        LOGDEBUG << "Dataset: " << oid << " rnum " << rnum << std::endl;
        rnum = (fds_uint32_t)rgen.genNum();
    }

    // initialize dynamicc counters
    op_count = ATOMIC_VAR_INIT(0);
    test_pass_ = ATOMIC_VAR_INIT(true);
    StatsCollector::singleton()->enableQosStats("ObjMetaStoreUt");
}

MetaStoreUTProc::~MetaStoreUTProc() {
    if (db_) {
        delete db_;
        db_ = NULL;
    }
    if (store_) {
        delete store_;
        store_ = NULL;
    }
    if (cache_) {
        delete cache_;
        cache_ = NULL;
    }
}


Error MetaStoreUTProc::get(uint64_t volId,
                           const ObjectID& objId,
                           ObjMetaData::const_ptr &objMeta) {
    fds_volid_t fdsVolId(volId);
    Error err(ERR_OK);
    fds_uint64_t start_nano = util::getTimeStampNanos();
    if (db_) {
        fds_verify(!store_ && !cache_);
        objMeta = db_->get(fdsVolId, objId, err);
    } else if (store_) {
        fds_verify(!db_ && !cache_);
        objMeta = store_->getObjectMetadata(fdsVolId, objId, err);
    } else if (cache_) {
        fds_verify(!db_ && !store_);
        objMeta = cache_->getObjectMetadata(fdsVolId, objId, err);
    } else {
        fds_panic("no known modules are initialized for get operation!");
    }
    if (!err.ok()) return err;

    // record stat
    fds_uint64_t lat_nano = util::getTimeStampNanos() - start_nano;
    StatsCollector::singleton()->recordEvent(fdsVolId,
                                             util::getTimeStampNanos(),
                                             STAT_AM_GET_OBJ,
                                             static_cast<double>(lat_nano) / 1000.0);
    return ERR_OK;
}

Error MetaStoreUTProc::put(uint64_t volId,
                           const ObjectID& objId,
                           ObjMetaData::const_ptr objMeta) {
    fds_volid_t fdsVolId(volId);
    Error err(ERR_OK);
    fds_uint64_t start_nano = util::getTimeStampNanos();
    if (db_) {
        fds_verify(!store_ && !cache_);
        err = db_->put(fdsVolId, objId, objMeta);
    } else if (store_) {
        fds_verify(!db_ && !cache_);
        err = store_->putObjectMetadata(fdsVolId, objId, objMeta);
    } else if (cache_) {
        fds_verify(!db_ && !store_);
        cache_->putObjectMetadata(fdsVolId, objId, objMeta);
    } else {
        fds_panic("no known modules are initialized for put operation!");
    }
    if (!err.ok()) return err;

    // record stat
    fds_uint64_t lat_nano = util::getTimeStampNanos() - start_nano;
    StatsCollector::singleton()->recordEvent(fdsVolId,
                                             util::getTimeStampNanos(),
                                             STAT_AM_PUT_OBJ,
                                             static_cast<double>(lat_nano) / 1000.0);

    return ERR_OK;
}

Error MetaStoreUTProc::remove(uint64_t volId,
                              const ObjectID& objId) {
    fds_volid_t fdsVolId(volId);
    if (db_) {
        fds_verify(!store_ && !cache_);
        return db_->remove(fdsVolId, objId);
    } else if (store_) {
        fds_verify(!db_ && !cache_);
        return store_->removeObjectMetadata(fdsVolId, objId);
    } else if (cache_) {
        fds_verify(!db_ && !store_);
        cache_->removeObjectMetadata(fdsVolId, objId);
        return ERR_OK;
    } else {
        fds_panic("no known modules are initialized for put operation!");
    }
    return ERR_OK;
}

Error MetaStoreUTProc::openMetadataDB() {
    if (db_) {
        fds_verify(!store_ && !cache_);
        return db_->openMetadataDb(smDiskMap);
    } else if (store_) {
        fds_verify(!db_ && !cache_);
        return store_->openMetadataStore(smDiskMap);
    } else if (cache_) {
        fds_verify(!db_ && !store_);
        return ERR_OK;
    } else {
        fds_panic("no known modules are initialized for put operation!");
    }
    return ERR_OK;
}

Error MetaStoreUTProc::populateStore() {
    Error err(ERR_OK);
    // put all metadata of all objects in dataset to the
    // metadata store
    for (fds_uint32_t i = 0; i < dataset_.size(); ++i) {
        ObjectID oid = dataset_[i];
        fds_uint32_t rnum = dataset_map_[oid];
        ObjMetaData::ptr meta = allocRandomObjMeta(rnum, oid);
        err = put(1, oid, meta);
        if (!err.ok()) return err;
    }
    return err;
}

void MetaStoreUTProc::setMetaLoc(fds_uint32_t rnum,
                                 obj_phy_loc_t* loc) {
    loc->obj_stor_loc_id = 1;
    loc->obj_file_id = rnum % 10;
    loc->obj_stor_offset = 100;
    if ((rnum % 2) == 1) {
        loc->obj_tier = diskio::DataTier::diskTier;
    } else {
        loc->obj_tier = diskio::DataTier::flashTier;
    }
}

fds_uint32_t MetaStoreUTProc::getObjSize(fds_uint32_t rnum) {
    return 4096 + 4096 * (rnum % 256);
}

ObjMetaData::ptr
MetaStoreUTProc::allocRandomObjMeta(fds_uint32_t rnum,
                                    const ObjectID& objId) {
    ObjMetaData::ptr meta(new ObjMetaData());
    obj_phy_loc_t loc;
    setMetaLoc(rnum, &loc);

    meta->initialize(objId,
                     getObjSize(rnum));
    if ((rnum % 3) == 0) {
        meta->updateAssocEntry(objId, fds_volid_t(5));
        meta->updateAssocEntry(objId, fds_volid_t(123));
        meta->updateAssocEntry(objId, fds_volid_t(1293));
    } else if ((rnum %3) == 1) {
        meta->updateAssocEntry(objId, fds_volid_t(51));
        meta->updateAssocEntry(objId, fds_volid_t(323));
    } else {
        meta->updateAssocEntry(objId, fds_volid_t(34));
    }
    meta->updatePhysLocation(&loc);

    return meta;
}

fds_bool_t
MetaStoreUTProc::isValidObjMeta(ObjMetaData::const_ptr meta,
                                fds_uint32_t rnum) {
    obj_phy_loc_t loc;
    setMetaLoc(rnum, &loc);

    const obj_phy_loc_t* meta_loc = meta->getObjPhyLoc((diskio::DataTier)loc.obj_tier);
    if (meta_loc->obj_stor_loc_id != loc.obj_stor_loc_id) return false;
    if (meta_loc->obj_file_id != loc.obj_file_id) return false;
    if (meta_loc->obj_stor_offset != loc.obj_stor_offset) return false;
    if (meta_loc->obj_tier != loc.obj_tier) return false;
    if (meta->getObjSize() != getObjSize(rnum)) return false;

    if ((rnum % 3) == 0) {
        if (!meta->isVolumeAssociated(fds_volid_t(5))) return false;
        if (!meta->isVolumeAssociated(fds_volid_t(123))) return false;
        if (!meta->isVolumeAssociated(fds_volid_t(1293))) return false;
    } else if ((rnum %3) == 1) {
        if (!meta->isVolumeAssociated(fds_volid_t(51))) return false;
        if (!meta->isVolumeAssociated(fds_volid_t(323))) return false;
    } else {
        if (!meta->isVolumeAssociated(fds_volid_t(34))) return false;
    }
    return true;
}

int MetaStoreUTProc::runSmokeTest() {
    Error err(ERR_OK);
    int ret = 0;

    // simple put to db, get, verify sequence
    for (fds_uint32_t i = 0; i < dataset_.size(); ++i) {
        ObjectID oid = dataset_[i];
        fds_uint32_t rnum = dataset_map_[oid];
        ObjMetaData::ptr meta = allocRandomObjMeta(rnum, oid);
        LOGDEBUG << "Generated meta " << *meta;
        err = put(1, oid, meta);
        if (!err.ok()) return -1;

        ObjMetaData::const_ptr get_meta;
        err = get(1, oid, get_meta);
        if (!err.ok()) return -1;

        LOGDEBUG << "Retrieved meta " << *get_meta;

        if (!isValidObjMeta(get_meta, rnum)) {
            return -1;
        }
    }

    // if we are testing metadata db, get snapshot and read objects
    if (db_) {
        fds_token_id smTokId = 1;
        ObjMetaData omd;
        std::shared_ptr<leveldb::DB> ldb;
        leveldb::ReadOptions options;
        db_->snapshot(smTokId, ldb, options);
        leveldb::Iterator* it = ldb->NewIterator(options);

        GLOGDEBUG << "Reading metadata db for SM token " << smTokId;
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            ObjectID id(it->key().ToString());
            omd.deserializeFrom(it->value());
            GLOGDEBUG << "Snap: " << id << " " << omd;
        }

        delete it;
        ldb->ReleaseSnapshot(options.snapshot);
    }

    // delete first object
    ObjectID del_oid = dataset_[0];
    err = remove(1, del_oid);
    if (!err.ok()) return -1;

    // try to access removed object
    ObjMetaData::const_ptr del_meta;
    err = get(1, del_oid, del_meta);
    LOGDEBUG << "Result getting removed object " << err;
    fds_verify(!err.ok());

    return ret;
}

int MetaStoreUTProc::run() {
    Error err(ERR_OK);
    int ret = 0;
    std::cout << "Starting test...";

    const FdsRootDir *dir = g_fdsprocess->proc_fdsroot();
    SmUtUtils::cleanFdsDev(dir);
    fds_uint32_t sm_count = 1;
    fds_uint32_t cols = (sm_count < 4) ? sm_count : 4;
    DLT* dlt = new DLT(10, cols, 1, true);
    SmUtUtils::populateDlt(dlt, sm_count);
    GLOGDEBUG << "Using DLT: " << *dlt;
    smDiskMap->handleNewDlt(dlt);

    // open metadata DBs for SM tokens
    err = openMetadataDB();
    fds_verify(err.ok());

    if (test_type_ == UT_SMOKE) {
        ret = runSmokeTest();
        return ret;
    }

    // otherwise load test (for now...)
    fds_verify(test_type_== UT_LOAD);

    // if this is get or delete test, populate metastore
    // first with obj meta
    err = populateStore();
    if (!err.ok()) {
        std::cout << "Failed to populate meta store " << err << std::endl;
        LOGERROR << "Failed to populate meta store " << err;
        return -1;
    }

    for (unsigned i = 0; i < num_threads_; ++i) {
        std::thread* new_thread = new std::thread(&MetaStoreUTProc::task, this, i);
        threads_.push_back(new_thread);
    }

    // Wait for all threads
    for (unsigned x = 0; x < num_threads_; ++x) {
        threads_[x]->join();
    }

    for (unsigned x = 0; x < num_threads_; ++x) {
        std::thread* th = threads_[x];
        delete th;
        threads_[x] = NULL;
    }

    fds_bool_t test_passed = test_pass_.load();
    if (test_passed) {
        std::cout << "Test PASSED" << std::endl;
    } else {
        std::cout << "Test FAILED" << std::endl;
        ret = -1;
    }

    return -1;
}

void MetaStoreUTProc::task(int id) {
    // task is executed here
    Error err(ERR_OK);
    fds_uint64_t seed = RandNumGenerator::getRandSeed();
    RandNumGenerator rgen(seed);
    fds_uint32_t ops = atomic_fetch_add(&op_count, (fds_uint32_t)1);

    std::cout << "Starting thread " << id
              << " current number of ops finished " << ops << std::endl;

    while ((ops + 1) <= num_ops_)
    {
        // do op
        fds_uint32_t index = ops % dataset_.size();
        ObjectID oid = dataset_[index];
        fds_uint32_t rnum = dataset_map_[oid];
        switch (op_type_) {
            case UT_OP_PUT:
                {
                    ObjMetaData::ptr meta = allocRandomObjMeta(rnum, oid);
                    err = put(1, oid, meta);
                    break;
                }
            case UT_OP_GET:
                {
                    ObjMetaData::const_ptr get_meta;
                    err = get(1, oid, get_meta);
                    break;
                }
            case UT_OP_RMW:
                {
                    ObjMetaData::const_ptr get_meta;
                    err = get(1, oid, get_meta);
                    if (err.ok()) {
                        ++rnum;
                        dataset_map_[oid] = rnum;
                        ObjMetaData::ptr meta = allocRandomObjMeta(rnum, oid);
                        err = put(1, oid, meta);
                    }
                    break;
                }
            case UT_OP_DELETE:
                {
                    err = remove(1, oid);
                    break;
                }
            default:
                fds_verify(false);   // new type added?
        }
        if (!err.ok()) {
            LOGERROR << "Failed operation " << err;
            break;
        }

        ops = atomic_fetch_add(&op_count, (fds_uint32_t)1);
    }

    if (!err.ok()) {
        test_pass_.store(false);
    }
}

}  // namespace fds

int main(int argc, char * argv[]) {
    smDiskMap = fds::SmDiskMap::ptr(new fds::SmDiskMap("Test SM Disk Map"));
    fds::Module *smVec[] = {
        smDiskMap.get(),
        nullptr
    };

    fds::MetaStoreUTProc p(argc, argv, "sm_ut.conf", "fds.meta_ut.", smVec);
    std::cout << "unit test " << __FILE__ << " started." << std::endl;
    p.main();
    std::cout << "unit test " << __FILE__ << " finished." << std::endl;
    return 0;
}

