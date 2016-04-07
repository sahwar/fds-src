/*
 * Copyright 2015 Formation Data Systems, Inc.
 */
#include <sstream>
#include <lib/Catalog.h>
#include <DataMgr.h>
#include <refcount/objectrefscanner.h>
#include <counters.h>
#include <util/path.h>
#include <boost/filesystem.hpp>

namespace fds { namespace refcount {
namespace bfs = boost::filesystem;

BloomFilterStore::BloomFilterStore(const std::string &path, uint32_t cacheSize, uint32_t bloomfilterBits)
        : basePath(path),
          maxCacheSize(cacheSize),
          bloomfilterBits(bloomfilterBits),
          accessCnt(1)
{
    if (basePath[basePath.size()-1] != '/') {
        basePath += "/";
    }
    /* Remove any existing files from path...and recreate that directory */
    bfs::path p(basePath.c_str());
    bfs::remove_all(p);
    bfs::create_directory(p);
    fds_assert(bfs::exists(p));
}

BloomFilterStore::~BloomFilterStore() {
    // purge();
}

util::BloomFilterPtr BloomFilterStore::load(const std::string &key) {
    auto deserializer = serialize::getFileDeserializer(getFilePath(key));
    auto bloomfilter = util::BloomFilterPtr(new util::BloomFilter(bloomfilterBits));
    auto readSize = bloomfilter->read(deserializer);
    delete deserializer;
    return bloomfilter;
}

void BloomFilterStore::save(const std::string &key, util::BloomFilterPtr bloomfilter) {
    auto filename = getFilePath(key);
    bfs::remove(filename);
    auto serializer = serialize::getFileSerializer(filename);
    auto writeSize = bloomfilter->write(serializer);
    delete serializer;
}

void BloomFilterStore::addToCache(const std::string &key, util::BloomFilterPtr bloomfilter) {
    BFNode newNode;
    newNode.accessCnt = accessCnt;
    newNode.key = key;
    newNode.bloomfilter = bloomfilter;
    cache.push_front(newNode);
    accessCnt++;

    /* Evict an entry if needed */
    if (cache.size() >= maxCacheSize) {
        auto &back = cache.back();
        save(back.key, back.bloomfilter);
        cache.pop_back();
    }
    fds_assert(cache.size() < maxCacheSize);
}

util::BloomFilterPtr BloomFilterStore::getFromCache(const std::string &key) {
    auto itr = cache.begin();
    for (; itr != cache.end(); itr++) {
        if (itr->key == key) {
            break;
        }
    }
    if (itr == cache.end()) {
        return nullptr;
    }

    /* Update the access count and move the found entry to front of the cache */
    itr->accessCnt = accessCnt;
    accessCnt++;
    cache.splice(cache.begin(), cache, itr);
    return cache.begin()->bloomfilter;
}

util::BloomFilterPtr BloomFilterStore::get(const std::string &key, bool create) {
    util::BloomFilterPtr bloomfilter;
    /* Check in the index */
    auto itr = index.find(key);
    if (itr == index.end()) {
        if (!create) {
            return bloomfilter;
        }
        /* Create empty bloomfilter and add to index and cache */
        bloomfilter.reset(new util::BloomFilter(bloomfilterBits));
        addToCache(key, bloomfilter);
        index.insert(key);
        GLOGDEBUG << "created bloomfilter: " << key;
    } else {
        /* Check in cache */
        bloomfilter = getFromCache(key);
        if (!bloomfilter) {
            bloomfilter = load(key);
            addToCache(key, bloomfilter);
        }
    }
    return bloomfilter;
}

bool BloomFilterStore::exists(const std::string &key) const
{
    return index.find(key) != index.end();
}

void BloomFilterStore::sync(bool clearCache) {
    for (const auto &item : cache) {
        save(item.key, item.bloomfilter);
    }
    if (clearCache) {
        cache.clear();
    }
}

void BloomFilterStore::purge() {
    cache.clear();
    for (const auto &item : index) {
        auto ret = bfs::remove(bfs::path(item.c_str()));
        if (ret != 0) {
            GLOGWARN << "failed to remove bloomfilter:" << item << " ret:" << ret;
        }
    }
    index.clear();
}

ObjectRefScanMgr::ObjectRefScanMgr(CommonModuleProviderIf *moduleProvider, DataMgr* dm)
        : HasModuleProvider(moduleProvider),
          Module("RefcountScanner"),
          dataMgr(dm),
          state(STOPPED),
          qosHelper(*dm),
          scanCntr(0),
          objectsScannedCntr(0)
{
    LOGNORMAL << "instantiating";
}

void ObjectRefScanMgr::mod_startup() {
    auto config = MODULEPROVIDER()->get_conf_helper();

    maxEntriesToScan = config.get<int>("objectrefscan.entries_per_scan", 32768);

    LOGNORMAL << "refscan enabled:" << (dataMgr->features.isExpungeEnabled()?"true":"false")
              << " scancount:" << maxEntriesToScan;

}

void ObjectRefScanMgr::mod_shutdown() {
}

bool ObjectRefScanMgr::isRunning() {
    return (STOPPED != state.load());
}

void ObjectRefScanMgr::scanOnce() {
    auto expectedState = STOPPED;
    bool wasStopped = state.compare_exchange_strong(expectedState, INIT);
    if (!wasStopped) {
        GLOGWARN << "ignoring refscan request as scanner is already running";
        return;
    }

    auto req = new DmFunctor(FdsDmSysTaskId, std::bind(&ObjectRefScanMgr::scanStep, this));
    qosHelper.addToQueue(req);
}

void ObjectRefScanMgr::setScanDoneCb(const ObjectRefScanMgr::ScanDoneCb &cb) {
    fds_assert(!scandoneCb);
    scandoneCb = cb;
}

void ObjectRefScanMgr::dumpStats() const {
    GLOGNOTIFY << "refscan runcount:" << scanCntr
               << " total.volumes:" << scanList.size()
               << " scanned.volumes:" << scanSuccessVols.size()
               << " scanned.objects:" << objectsScannedCntr
               << " bloomfilters.count:" << bfStore->getIndexSize();
}

void ObjectRefScanMgr::scanStep() {
    if (state == INIT) {
        prescanInit();
    }

    if (currentItr != scanList.end()) {
        /* Scan step */
        Error e = currentItr->scanStep();
        if (e != ERR_OK) {
            /* Report the error and move on */
            GLOGWARN<< "failed to scan: " << currentItr->logString() << " " << e;
            currentItr->finishScan(e);
            currentItr++;
        } else {
            if (currentItr->isComplete()) {
                currentItr->finishScan(ERR_OK);
                currentItr++;
            }
        }
    }

    if (currentItr != scanList.end()) {
        /* Schedule next scan step */
        auto req = new DmFunctor(FdsDmSysTaskId, std::bind(&ObjectRefScanMgr::scanStep, this));
        qosHelper.addToQueue(req);
    } else {
        /* Scan finished */
        bfStore->sync(true /* clear cache */);
        // stop from refcountmanager
        // state = STOPPED
        dumpStats();
        if (scandoneCb) {
            scandoneCb(this);
        }
    }
}

bool ObjectRefScanMgr::setStateStopped() {
    auto expectedState = RUNNING;
    bool wasStopped = state.compare_exchange_strong(expectedState, STOPPED);
    if (!wasStopped) {
        GLOGWARN << "could not set state to STOPPED "
                 << "current state: " << state;
        return false;
    }
    dataMgr->counters->refscanRunning.set(0);
    return true;
}

util::BloomFilterPtr ObjectRefScanMgr::getTokenBloomFilter(const fds_token_id &tokenId)
{
    return bfStore->get(tokenBloomFilterKey(tokenId), false);
}

std::string ObjectRefScanMgr::getTokenBloomfilterPath(const fds_token_id &tokenId)
{
    auto key = tokenBloomFilterKey(tokenId);
    if (bfStore->exists(key)) {
        return bfStore->getFilePath(key);
    } else {
        return "";
    }
}

void ObjectRefScanMgr::prescanInit()
{
    dataMgr->counters->refscanLastRun.set(util::getTimeStampSeconds());
    dataMgr->counters->refscanRunning.set(1);
    dataMgr->counters->refscanNumVolumes.set(0);
    dataMgr->counters->refscanNumTokenFiles.set(0);
    dataMgr->counters->refscanRunCount.incr();

    /* Get the current dlt */
    currentDlt = MODULEPROVIDER()->getSvcMgr()->getCurrentDLT();

    /* Clear out datastructures from any previous run */
    scanList.clear();
    scanSuccessVols.clear();

    /* Build a list of volumes to scan */
    std::vector<fds_volid_t> vols;
    dataMgr->getActiveVolumes(vols);
    for (const auto &v : vols) {
        if (dataMgr->amIPrimary(v)) {
            scanList.push_back(VolumeRefScannerContext(this, v));
        }
    }
    currentItr = scanList.begin();

    /* Init bloomfilter store */
    auto dmUserRepo = MODULEPROVIDER()->proc_fdsroot()->dir_user_repo_dm();
    auto config = MODULEPROVIDER()->get_conf_helper();
    auto bfSize = util::getBytesFromHumanSize(
        config.get<std::string>("objectrefscan.bf_size", "1M"));

    bfStore.reset(new BloomFilterStore(util::strformat("%s/bloomfilters/", dmUserRepo.c_str()), 5, bfSize));

    /* Scan cycle counters */
    scanCntr++;
    objectsScannedCntr = 0;

    state = RUNNING;
}

VolumeRefScannerContext::VolumeRefScannerContext(ObjectRefScanMgr* m, fds_volid_t vId)
        : ObjectRefScannerIf(m)
{
    volId = vId;
    std::stringstream ss;
    ss << "VolumeRefScannerContext:" << vId;
    logStr = ss.str();

    scanners.push_back(ObjectRefScannerPtr(new VolumeObjectRefScanner(m, vId)));
    // adding snapshots
    std::vector<fds_volid_t> vecSnapIds;
    Error err = m->getDataMgr()->timelineMgr->getSnapshotsForVolume(vId, vecSnapIds);
    for (const auto& snapId : vecSnapIds) {
        err = m->getDataMgr()->timelineMgr->loadSnapshot(vId, snapId);
        if (err.ok()) {
            // double check if the snapshot was loaded.
            if (nullptr != m->getDataMgr()->getVolumeMeta(snapId)) {
                scanners.push_back(ObjectRefScannerPtr(new VolumeObjectRefScanner(m, snapId)));
            } else {
                LOGERROR << "unable to load snap:" << snapId;
            }
        } else {
            LOGWARN << "unable to load snapshot for refscan vol:" << vId << " snap:"<< snapId;
        }
    }
    itr = scanners.begin();
    state = SCANNING;
}

Error VolumeRefScannerContext::scanStep() {
    Error e = ERR_OK;
    if (itr != scanners.end()) {
        auto scanner = *itr;
        e = scanner->scanStep();
        if (e != ERR_OK) {
            GLOGWARN<< "failed to scan: " << scanner->logString() << " " << e;
            scanner->finishScan(e);
            itr++;
        } else {
            if (scanner->isComplete()) {
                scanner->finishScan(ERR_OK);
                itr++;
            }
        }
    }
    return e;
}

Error VolumeRefScannerContext::finishScan(const Error &e) {
    state = COMPLETE;
    completionError = e;
    std::string errString;
    if (!e.ok()) {
        std::ostringstream oss;
        oss<< " completion error:" << e;
        errString = oss.str();
    }

    GLOGNOTIFY << "finished scan vol:" << volId
               << " snapshots:" << (scanners.size() - 1)
               << " total.objects.scanned:" << objRefMgr->objectsScannedCntr
               << " " << errString;


    if (e != ERR_OK) {
        /* Scan completed with an error.  Nothing more to do */
        return ERR_OK;
    }

    /* For each token update the aggregate bloom filter */
    auto bfStore = objRefMgr->getBloomFiltersStore();
    auto tokenCnt = objRefMgr->getCurrentDlt()->getNumTokens();
    for (uint32_t token = 0; token < tokenCnt; token++) {
        auto bloomfilter = bfStore->get(ObjectRefScanMgr::volTokBloomFilterKey(volId, token), false);
        if (!bloomfilter) continue;
        auto tokenbloomfilter = bfStore->get(ObjectRefScanMgr::tokenBloomFilterKey(token));
        tokenbloomfilter->merge(*bloomfilter);
    }
    objRefMgr->getScanSuccessVols().push_back(volId);
    objRefMgr->dataMgr->counters->refscanNumVolumes.incr(1);
    return ERR_OK;
}

bool VolumeRefScannerContext::isComplete() const {
    return (state == COMPLETE || itr == scanners.end());
}

std::string VolumeRefScannerContext::logString() const {
    return logStr;
}

VolumeObjectRefScanner::VolumeObjectRefScanner(ObjectRefScanMgr* m, fds_volid_t vId)
        : ObjectRefScannerIf(m),
          volId(vId)
{
    std::stringstream ss;
    ss << "VolumeObjectRefScanner:" << volId;
    logStr = ss.str();
}

Error VolumeObjectRefScanner::init() {
    /* Create snapshot */
    auto volcatIf = objRefMgr->getDataMgr()->timeVolCat_->queryIface();
    auto err = volcatIf->getVolumeSnapshot(volId, snap);
    if (err != ERR_OK) {
        return err;
    }
    // TODO(Rao): We should lock the volume as well to protect against deletions
    return err;
}

Error VolumeObjectRefScanner::scanStep() {
    if (state == INIT) {
        auto error = init();
        if (error != ERR_OK) {
            GLOGWARN << "failed to initialize " << logString();
            return error;
        }
        state = SCANNING;
    }

    auto currentDlt = objRefMgr->getCurrentDlt();
    std::map<fds_token_id, std::list<ObjectID>> tokenObjects;
    std::list<ObjectID> objects;
    auto volcatIf = objRefMgr->getDataMgr()->timeVolCat_->queryIface();
    volcatIf->getObjectIds(volId, objRefMgr->getMaxEntriesToScan(), snap, itr, objects);

    /* Bucketize the objects into tokens */
    for (const auto &oid : objects) {
        tokenObjects[currentDlt->getToken(oid)].push_back(oid);
    }
    objects.clear();

    /* Update token bloomfilter one at a time.  We do this because bloomfilters can be
     * quite large.  Processing one bloomfilter at a timer allows us to efficient with
     * memory usage
     */
    auto bfStore = objRefMgr->getBloomFiltersStore();
    auto bfVolId = volId;
    auto volDesc = objRefMgr->getDataMgr()->getVolumeDesc(volId);
    if (volDesc->isSnapshot()) {
        bfVolId = volDesc->srcVolumeId;
    }
    for (const auto &kv : tokenObjects) {
        auto bloomfilter = bfStore->get(ObjectRefScanMgr::volTokBloomFilterKey(bfVolId, kv.first));
        auto &objects = kv.second;
        for (const auto &oid : objects) {
            // LOGNORMAL << "scanned obj:" << oid;
            bloomfilter->add(oid);
            objRefMgr->objectsScannedCntr++;
            objRefMgr->dataMgr->counters->refscanNumObjects.set(objRefMgr->objectsScannedCntr);
        }
    }

    return ERR_OK;
}

Error VolumeObjectRefScanner::finishScan(const Error &e) {
    state = COMPLETE;
    completionError = e;
    LOGDEBUG << "finishing scan:" << volId;
    auto volcatIf = objRefMgr->getDataMgr()->timeVolCat_->queryIface();
    if (e != ERR_VOL_NOT_FOUND) {
        itr = nullptr;
        if (ERR_OK != volcatIf->freeVolumeSnapshot(volId, snap)) {
            GLOGWARN << "failed to release snapshot for vol: " << volId;
        }
    }
    return e;
}
}  // namespace refcount
}  // namespace fds
