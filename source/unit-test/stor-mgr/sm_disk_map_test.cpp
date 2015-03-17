/**
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <unistd.h>

#include <vector>
#include <chrono>
#include <thread>
#include <string>
#include <boost/shared_ptr.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <util/Log.h>
#include <fds_assert.h>
#include <fds_process.h>
#include <ObjectId.h>
#include <FdsRandom.h>
#include <object-store/SmDiskMap.h>

#include <sm_ut_utils.h>

/**
 * Unit test for SmDiskMap class
 */

namespace fds {

class SmDiskMapUtProc : public FdsProcess {
  public:
    SmDiskMapUtProc(int argc, char * argv[], const std::string & config,
                    const std::string & basePath, Module * vec[]) {
        init(argc, argv, config, basePath, "smdiskmap_ut.log", vec);
    }

    virtual int run() override {
        return 0;
    }
};

SmDiskMap::ptr
loadDiskMap(fds_uint32_t sm_count) {
    Error err(ERR_OK);
    SmDiskMap::ptr smDiskMap(new SmDiskMap("Test SM Disk Map"));
    smDiskMap->mod_init(NULL);

    fds_uint32_t cols = (sm_count < 4) ? sm_count : 4;
    DLT* dlt = new DLT(10, cols, 1, true);
    SmUtUtils::populateDlt(dlt, sm_count);
    GLOGDEBUG << "Using DLT: " << *dlt;
    smDiskMap->handleNewDlt(dlt);

    // we don't need dlt anymore
    delete dlt;

    return smDiskMap;
}

boost::log::formatting_ostream& operator<< (boost::log::formatting_ostream& out,
                                            const SmTokenSet& toks) {
    out << "tokens {";
    if (toks.size() == 0) out << "none";
    for (SmTokenSet::const_iterator cit = toks.cbegin();
         cit != toks.cend();
         ++cit) {
        if (cit != toks.cbegin()) {
            out << ", ";
        }
        out << *cit;
    }
    out << "}\n";
    return out;
}

void
printSmTokens(const SmDiskMap::const_ptr& smDiskMap) {
    SmTokenSet smToks = smDiskMap->getSmTokens();
    diskio::DataTier tier = diskio::diskTier;
    // if there are no HDDs, tokens will be on SSDs only
    DiskIdSet hddIds = smDiskMap->getDiskIds(diskio::diskTier);    
    if (hddIds.size() == 0) {
        tier = diskio::flashTier;
    }
    // in other cases, print HDD location
    for (SmTokenSet::const_iterator cit = smToks.cbegin();
         cit != smToks.cend();
         ++cit) {
        GLOGDEBUG << "Token " << *cit << " disk path: "
                  << smDiskMap->getDiskPath(*cit, tier);
    }
}

TEST(SmDiskMap, basic) {
    Error err(ERR_OK);
    fds_uint32_t sm_count = 1;

    // start clean
    const FdsRootDir *dir = g_fdsprocess->proc_fdsroot();
    const std::string devPath = dir->dir_dev();
    SmUtUtils::cleanAllInDir(devPath);

    // create our own disk map
    SmUtUtils::setupDiskMap(dir, 10, 2);

    SmDiskMap::ptr smDiskMap = loadDiskMap(sm_count);
    printSmTokens(smDiskMap);

    // print disk ids
    DiskIdSet diskIds = smDiskMap->getDiskIds(diskio::diskTier);
    GLOGDEBUG << "HDD tier disk IDs " << diskIds;
    for (DiskIdSet::const_iterator cit = diskIds.cbegin();
         cit != diskIds.cend();
         ++cit) {
        SmTokenSet toks = smDiskMap->getSmTokens(*cit);
        GLOGDEBUG << "Disk " << *cit << " " << toks;
        // get disk path
        GLOGDEBUG << "Disk " << *cit << " path "
                  << smDiskMap->getDiskPath(*cit);
    }

    diskIds = smDiskMap->getDiskIds(diskio::flashTier);
    GLOGDEBUG << "Flash tier disk Ids " << diskIds;
    for (DiskIdSet::const_iterator cit = diskIds.cbegin();
         cit != diskIds.cend();
         ++cit) {
        SmTokenSet toks = smDiskMap->getSmTokens(*cit);
        GLOGDEBUG << "Disk " << *cit << " " << toks;
        // we should be able to get disk path for each token
        for (SmTokenSet::const_iterator ctok = toks.cbegin();
             ctok != toks.cend();
             ++ctok) {
            GLOGDEBUG << "Disk " << *cit << " token " << *ctok << " "
                      << smDiskMap->getDiskPath(*ctok, diskio::flashTier);
        }
    }

    // cleanup
    SmUtUtils::cleanAllInDir(devPath);
}

TEST(SmDiskMap, all_hdds) {
    Error err(ERR_OK);
    fds_uint32_t sm_count = 1;
    fds_uint32_t hdd_count = 10;

    // start clean
    const FdsRootDir *dir = g_fdsprocess->proc_fdsroot();
    const std::string devPath = dir->dir_dev();
    SmUtUtils::cleanAllInDir(devPath);

    // create our own disk map
    SmUtUtils::setupDiskMap(dir, hdd_count, 0);

    SmDiskMap::ptr smDiskMap = loadDiskMap(sm_count);
    printSmTokens(smDiskMap);

    // we should get empty disk set for SSDs
    DiskIdSet ssdIds = smDiskMap->getDiskIds(diskio::flashTier);
    EXPECT_EQ(0, ssdIds.size());

    // we should get 'hdd_count' number of disk IDs for HDDs
    DiskIdSet hddIds = smDiskMap->getDiskIds(diskio::diskTier);
    EXPECT_EQ(hdd_count, hddIds.size());

    // cleanup
    SmUtUtils::cleanAllInDir(devPath);
}

TEST(SmDiskMap, all_flash) {
    Error err(ERR_OK);
    fds_uint32_t sm_count = 1;
    fds_uint32_t ssd_count = 1;

    for (ssd_count = 1; ssd_count <= 16; ssd_count += 7) {
        // start clean
        const FdsRootDir *dir = g_fdsprocess->proc_fdsroot();
        const std::string devPath = dir->dir_dev();
        SmUtUtils::cleanAllInDir(devPath);

        // create our own disk map
        SmUtUtils::setupDiskMap(dir, 0, ssd_count);

        SmDiskMap::ptr smDiskMap = loadDiskMap(sm_count);
        printSmTokens(smDiskMap);

        // we should get ssd_count number of disk IDs for SSDs
        DiskIdSet ssdIds = smDiskMap->getDiskIds(diskio::flashTier);
        EXPECT_EQ(ssd_count, ssdIds.size());

        // we should get empty disk set for HDDs
        DiskIdSet hddIds = smDiskMap->getDiskIds(diskio::diskTier);
        EXPECT_EQ(0, hddIds.size());

        // cleanup
        SmUtUtils::cleanAllInDir(devPath);
    }
}

TEST(SmDiskMap, up_down) {
    Error err(ERR_OK);
    const FdsRootDir *dir = g_fdsprocess->proc_fdsroot();
    const std::string devPath = dir->dir_dev();
    SmUtUtils::cleanAllInDir(devPath);

    // bring up with different number of SMs
    // each time from clean state
    for (fds_uint32_t i = 1; i < 300; i+=24) {
        // create our own disk map
        SmUtUtils::setupDiskMap(dir, 10, 2);
        SmDiskMap::ptr smDiskMap = loadDiskMap(i);

        // cleanup
        SmUtUtils::cleanAllInDir(devPath);
    }
}

}  // namespace fds

int main(int argc, char * argv[]) {
    fds::SmDiskMapUtProc diskMapProc(argc, argv, "platform.conf",
                                     "fds.sm.", NULL);
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}

