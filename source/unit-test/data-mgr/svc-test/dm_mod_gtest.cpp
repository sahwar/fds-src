/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <string>
#include <dm_mod_gtest.h>
#include <net/SvcRequest.h>
#include <net/net-service-tmpl.hpp>
#include <google/profiler.h>
#include "fdsp/dm_service_types.h"
static fds_uint32_t MAX_OBJECT_SIZE = 1024 * 1024 * 2;    // 2MB
static fds_uint32_t NUM_OBJTS = 1;    // 2MB
// static fds_uint64_t BLOB_SIZE = static_cast<fds_uint64_t>(10) * 1024 * 1024 * 1024;   // 1GB
static fds_uint64_t BLOB_SIZE = 1 * 1024 * 1024 * 1024;   // 1GB
static fds_uint32_t NUM_VOLUMES = 1;
static fds_uint32_t NUM_BLOBS = 1;
static bool  profile = false;

boost::shared_ptr<LatencyCounter> startTxCounter(new LatencyCounter("startBLobTx", 0, 0));
boost::shared_ptr<LatencyCounter> updateTxCounter(new LatencyCounter("updateBlobTx", 0, 0));
boost::shared_ptr<LatencyCounter> commitTxCounter(new LatencyCounter("commitBlobTx", 0, 0));
boost::shared_ptr<LatencyCounter> getBlobMetaCounter(new LatencyCounter("getBlobMeta", 0, 0));
boost::shared_ptr<LatencyCounter> qryCatCounter(new LatencyCounter("getBlobMeta", 0, 0));
boost::shared_ptr<LatencyCounter> setBlobMetaCounter(new LatencyCounter("getBlobMeta", 0, 0));
boost::shared_ptr<LatencyCounter> delCatObjCounter(new LatencyCounter("getBlobMeta", 0, 0));
boost::shared_ptr<LatencyCounter> getBucketCounter(new LatencyCounter("getBlobMeta", 0, 0));
boost::shared_ptr<LatencyCounter> updateCatOnceCounter(new LatencyCounter("getBlobMeta", 0, 0));
boost::shared_ptr<LatencyCounter> dmStatsCounter(new LatencyCounter("getBlobMeta", 0, 0));
boost::shared_ptr<LatencyCounter> delBlobCounter(new LatencyCounter("getBlobMeta", 0, 0));
boost::shared_ptr<LatencyCounter> txCounter(new LatencyCounter("tx", 0, 0));

static fds_uint64_t txStartTs = 0;

/**
* @brief basic  DM tests- startBlob, UpdateBlob and Commit  
*
*/
TEST_F(DMApi, putBlobOnceTest)
{
    std::string blobPrefix("testBlobOnce");
    fpi::SvcUuid svcUuid;
    svcUuid = TestUtils::getAnyNonResidentDmSvcuuid(gModuleProvider->get_plf_manager());
    ASSERT_NE(svcUuid.svc_uuid, 0);

    if (profile)
        ProfilerStart("/tmp/dm.prof");
    for (fds_uint64_t numBlobs = 0; numBlobs < NUM_BLOBS; numBlobs++) {
        std::string blobName = blobPrefix + std::to_string(numBlobs);
        SvcRequestCbTask<EPSvcRequest, fpi::UpdateCatalogOnceMsg> putBlobOnceWaiter;
        auto putBlobOnce = SvcMsgFactory::newUpdateCatalogOnceMsg(volId_, blobName);
        auto asyncPutBlobTxReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
        fds::UpdateBlobInfoNoData(putBlobOnce, MAX_OBJECT_SIZE, BLOB_SIZE);
        putBlobOnce->txId = 1;
        putBlobOnce->dmt_version = 1;
        putBlobOnce->blob_mode = 0;
        asyncPutBlobTxReq->setPayload(FDSP_MSG_TYPEID(fpi::UpdateCatalogOnceMsg), putBlobOnce);
        asyncPutBlobTxReq->onResponseCb(putBlobOnceWaiter.cb);
        {
          fds_uint64_t startTs = util::getTimeStampNanos();
          txStartTs = startTs;
          asyncPutBlobTxReq->invoke();
          putBlobOnceWaiter.await();
          fds_uint64_t endTs = util::getTimeStampNanos();
          updateCatOnceCounter->update(endTs - startTs);
       }
       ASSERT_EQ(putBlobOnceWaiter.error, ERR_OK) << "Error: " << putBlobOnceWaiter.error;
       std::cout << "\033[33m[putBlobOnceTest latency]\033[39m "
         << std::fixed << std::setprecision(3)
         << (updateCatOnceCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
         << updateCatOnceCounter->count() << std::endl;
    }
    if (profile)
        ProfilerStop();
}

TEST_F(DMApi, putBlobTest)
{
    std::string blobName("testBlob");

    fpi::SvcUuid svcUuid;
    svcUuid = TestUtils::getAnyNonResidentDmSvcuuid(gModuleProvider->get_plf_manager());
    ASSERT_NE(svcUuid.svc_uuid, 0);

    /* Prepare start tx */
    SvcRequestCbTask<EPSvcRequest, fpi::StartBlobTxRspMsg> waiter;
    auto startBlobTx = SvcMsgFactory::newStartBlobTxMsg(volId_, blobName);
    auto asyncBlobTxReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    startBlobTx->txId = 1;
    startBlobTx->dmt_version = 1;
    startBlobTx->blob_mode = 0;
    asyncBlobTxReq->setPayload(FDSP_MSG_TYPEID(fpi::StartBlobTxMsg), startBlobTx);
    asyncBlobTxReq->onResponseCb(waiter.cb);
    startBlobTxIssued_++;

    /* Prepare update tx */
    SvcRequestCbTask<EPSvcRequest, fpi::UpdateCatalogRspMsg> updWaiter;
    auto updateCatMsg = SvcMsgFactory::newUpdateCatalogMsg(volId_, blobName);
    auto asyncUpdCatReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
         updateCatMsg->obj_list.clear();
    fds::UpdateBlobInfoNoData(updateCatMsg, MAX_OBJECT_SIZE, BLOB_SIZE);
    updateCatMsg->txId = 1;

    asyncUpdCatReq->setPayload(FDSP_MSG_TYPEID(fpi::UpdateCatalogMsg), updateCatMsg);
    asyncUpdCatReq->onResponseCb(updWaiter.cb);
    updateCatIssued_++;

    /* Prepare commit tx */
    SvcRequestCbTask<EPSvcRequest, fpi::CommitBlobTxRspMsg> commitWaiter;
    auto commitBlobMsg = SvcMsgFactory::newCommitBlobTxMsg(volId_, blobName);
    auto asyncCommitBlobReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    commitBlobMsg->txId  = 1;
    commitBlobMsg->dmt_version = 1;
    asyncCommitBlobReq->setPayload(FDSP_MSG_TYPEID(fpi::CommitBlobTxMsg), commitBlobMsg);
    asyncCommitBlobReq->onResponseCb(commitWaiter.cb);
    commitBlobTxIssued_++;

    /* Start tx */
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    txStartTs = startTs;
    asyncBlobTxReq->invoke();
    waiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    startTxCounter->update(endTs - startTs);
    }
    ASSERT_EQ(waiter.error, ERR_OK) << "Error: " << waiter.error;

    /* Update tx */
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    asyncUpdCatReq->invoke();
    updWaiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    updateTxCounter->update(endTs - startTs);
    }
    ASSERT_EQ(updWaiter.error, ERR_OK) << "Error: " << updWaiter.error;

    /* Commit tx */
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    asyncCommitBlobReq->invoke();
    commitWaiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    commitTxCounter->update(endTs - startTs);
    txCounter->update(endTs - txStartTs);
    }
    ASSERT_EQ(commitWaiter.error, ERR_OK) << "Error: " << commitWaiter.error;

    std::cout << "\033[33m[startTx latency]\033[39m " << std::fixed << std::setprecision(3)
            << (startTxCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << startTxCounter->count() << std::endl;
    std::cout << "\033[33m[updateBlobTx latency]\033[39m " << std::fixed << std::setprecision(3)
            << (updateTxCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << updateTxCounter->count() << std::endl;
    std::cout << "\033[33m[CommitBlobTx latency]\033[39m " << std::fixed << std::setprecision(3)
            << (commitTxCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << commitTxCounter->count() << std::endl;
    std::cout << "\033[33m[putBlobTest latency]\033[39m " << std::fixed << std::setprecision(3)
            << (txCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << txCounter->count() << std::endl;
}

TEST_F(DMApi, qryCatTest)
{
    std::string blobName("testBlob");
    fpi::SvcUuid svcUuid;
    uint64_t blobOffset = 0;
    svcUuid = TestUtils::getAnyNonResidentDmSvcuuid(gModuleProvider->get_plf_manager());
    ASSERT_NE(svcUuid.svc_uuid, 0);


    SvcRequestCbTask<EPSvcRequest, fpi::QueryCatalogRspMsg> qryCatWaiter;
    auto qryCat = SvcMsgFactory::newQueryCatalogMsg(volId_, blobName, blobOffset);
    auto asyncQryCatReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    asyncQryCatReq->setPayload(FDSP_MSG_TYPEID(fpi::QueryCatalogMsg), qryCat);
    asyncQryCatReq->onResponseCb(qryCatWaiter.cb);
    /*
    asyncQryCatReq->onResponseCb(std::bind(&DMApi::queryCatCb, this,
                                            std::placeholders::_1,
                                            std::placeholders::_2, std::placeholders::_3));
    */
    queryCatIssued_++;
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    asyncQryCatReq->invoke();
    qryCatWaiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    qryCatCounter->update(endTs - startTs);
    }
    ASSERT_EQ(qryCatWaiter.error, ERR_OK) << "Error: " << qryCatWaiter.error;
    std::cout << "\033[33m[qryCatTest latency]\033[39m " << std::fixed << std::setprecision(3)
            << (qryCatCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << qryCatCounter->count() << std::endl;
}

TEST_F(DMApi, setBlobMeta)
{
    std::string blobName("testBlob");
    fpi::SvcUuid svcUuid;
    uint64_t blobOffset = 0;
    svcUuid = TestUtils::getAnyNonResidentDmSvcuuid(gModuleProvider->get_plf_manager());
    ASSERT_NE(svcUuid.svc_uuid, 0);

    // start transaction
    SvcRequestCbTask<EPSvcRequest, fpi::StartBlobTxRspMsg> waiter;
    auto startBlobTx = SvcMsgFactory::newStartBlobTxMsg(volId_, blobName);
    auto asyncBlobTxReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    startBlobTx->txId = 2;
    startBlobTx->dmt_version = 1;
    startBlobTx->blob_mode = 0;
    asyncBlobTxReq->setPayload(FDSP_MSG_TYPEID(fpi::StartBlobTxMsg), startBlobTx);
    asyncBlobTxReq->onResponseCb(waiter.cb);
    startBlobTxIssued_++;
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    txStartTs = startTs;
    asyncBlobTxReq->invoke();
    waiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    startTxCounter->update(endTs - startTs);
    }
    ASSERT_EQ(waiter.error, ERR_OK) << "Error: " << waiter.error;


    SvcRequestCbTask<EPSvcRequest, fpi::SetBlobMetaDataRspMsg> setBlobWaiter;
    auto setBlobMeta = SvcMsgFactory::newSetBlobMetaDataMsg(volId_, blobName);
    auto setBlobMetaReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    FDS_ProtocolInterface::FDSP_MetaDataPair metaData;
    metaData.key = "blobType";
    metaData.value = "test Blob S3";
    setBlobMeta->metaDataList.push_back(metaData);
    setBlobMeta->txId  = 2;
    setBlobMetaReq->setPayload(FDSP_MSG_TYPEID(fpi::SetBlobMetaDataMsg), setBlobMeta);
    setBlobMetaReq->onResponseCb(setBlobWaiter.cb);
    setBlobMetaIssued_++;
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    setBlobMetaReq->invoke();
    setBlobWaiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    setBlobMetaCounter->update(endTs - startTs);
    }
    ASSERT_EQ(setBlobWaiter.error, ERR_OK) << "Error: " << setBlobWaiter.error;

    SvcRequestCbTask<EPSvcRequest, fpi::CommitBlobTxRspMsg> commitWaiter;
    auto commitBlobMsg = SvcMsgFactory::newCommitBlobTxMsg(volId_, blobName);
    auto asyncCommitBlobReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    commitBlobMsg->txId  = 2;
    commitBlobMsg->dmt_version = 1;
    asyncCommitBlobReq->setPayload(FDSP_MSG_TYPEID(fpi::CommitBlobTxMsg), commitBlobMsg);
    asyncCommitBlobReq->onResponseCb(commitWaiter.cb);
    commitBlobTxIssued_++;
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    asyncCommitBlobReq->invoke();
    commitWaiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    commitTxCounter->update(endTs - startTs);
    txCounter->update(endTs - txStartTs);
    }
    ASSERT_EQ(commitWaiter.error, ERR_OK) << "Error: " << commitWaiter.error;

    std::cout << "\033[33m[startBlobTx latency]\033[39m " << std::fixed << std::setprecision(3)
            << (startTxCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << startTxCounter->count() << std::endl;
    std::cout << "\033[33m[setBlobMeta latency]\033[39m " << std::fixed << std::setprecision(3)
            << (setBlobMetaCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << setBlobMetaCounter->count() << std::endl;
    std::cout << "\033[33m[commitBlobTx latency]\033[39m " << std::fixed << std::setprecision(3)
            << (commitTxCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << commitTxCounter->count() << std::endl;
    std::cout << "\033[33m[setBlobMeta latency]\033[39m " << std::fixed << std::setprecision(3)
            << (txCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << txCounter->count() << std::endl;
}


TEST_F(DMApi, getBlobMetaTest)
{
    std::string blobName("testBlob");
    fpi::SvcUuid svcUuid;
    svcUuid = TestUtils::getAnyNonResidentDmSvcuuid(gModuleProvider->get_plf_manager());
    ASSERT_NE(svcUuid.svc_uuid, 0);

    SvcRequestCbTask<EPSvcRequest, fpi::GetBlobMetaDataMsg> getMetaWaiter;
    auto getBlobMeta = SvcMsgFactory::newGetBlobMetaDataMsg(volId_, blobName);
    auto asyncGetBlobMetaReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    asyncGetBlobMetaReq->setPayload(FDSP_MSG_TYPEID(fpi::GetBlobMetaDataMsg), getBlobMeta);
    asyncGetBlobMetaReq->onResponseCb(getMetaWaiter.cb);
    getBlobMetaIssued_++;
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    asyncGetBlobMetaReq->invoke();
    getMetaWaiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    getBlobMetaCounter->update(endTs - startTs);
    }
    ASSERT_EQ(getMetaWaiter.error, ERR_OK) << "Error: " << getMetaWaiter.error;
    std::cout << "\033[33m[getBlobMetaTest latency]\033[39m " << std::fixed << std::setprecision(3)
            << (getBlobMetaCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << getBlobMetaCounter->count() << std::endl;
}

TEST_F(DMApi, getDmStats)
{
    std::string blobName("testBlob");
    fpi::SvcUuid svcUuid;
    uint64_t blobOffset = 0;
    svcUuid = TestUtils::getAnyNonResidentDmSvcuuid(gModuleProvider->get_plf_manager());
    ASSERT_NE(svcUuid.svc_uuid, 0);

    SvcRequestCbTask<EPSvcRequest, fpi::GetDmStatsMsg> getDmStatsWaiter;
    auto getDmStats = SvcMsgFactory::newGetDmStatsMsg(volId_);
    auto getDmStatsReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    getDmStatsReq->setPayload(FDSP_MSG_TYPEID(fpi::GetDmStatsMsg), getDmStats);
    getDmStatsReq->onResponseCb(getDmStatsWaiter.cb);
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    getDmStatsReq->invoke();
    getDmStatsWaiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    dmStatsCounter->update(endTs - startTs);
    }
    ASSERT_EQ(getDmStatsWaiter.error, ERR_OK) << "Error: " << getDmStatsWaiter.error;
    std::cout << "\033[33m[getDmStats latency]\033[39m " << std::fixed << std::setprecision(3)
            << (dmStatsCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << dmStatsCounter->count() << std::endl;
}

TEST_F(DMApi, getBucket)
{
    std::string blobName("testBlob");
    fpi::SvcUuid svcUuid;
    svcUuid = TestUtils::getAnyNonResidentDmSvcuuid(gModuleProvider->get_plf_manager());
    ASSERT_NE(svcUuid.svc_uuid, 0);

    SvcRequestCbTask<EPSvcRequest, fpi::GetBucketMsg> getBucketWaiter;
    auto getBucket = SvcMsgFactory::newGetBucketMsg(volId_, 0);
    auto getBucketReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    getBucketReq->setPayload(FDSP_MSG_TYPEID(fpi::GetBucketMsg), getBucket);
    getBucketReq->onResponseCb(getBucketWaiter.cb);
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    getBucketReq->invoke();
    getBucketWaiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    getBucketCounter->update(endTs - startTs);
    }
    ASSERT_EQ(getBucketWaiter.error, ERR_OK) << "Error: " << getBucketWaiter.error;
    std::cout << "\033[33m[getBucket latency]\033[39m " << std::fixed << std::setprecision(3)
            << (getBucketCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << getBucketCounter->count() << std::endl;
}

TEST_F(DMApi, deleteBlobTest)
{
    std::string blobName("testBlob");
    fpi::SvcUuid svcUuid;
    svcUuid = TestUtils::getAnyNonResidentDmSvcuuid(gModuleProvider->get_plf_manager());
    ASSERT_NE(svcUuid.svc_uuid, 0);

    // start transaction
    SvcRequestCbTask<EPSvcRequest, fpi::StartBlobTxRspMsg> waiter;
    auto startBlobTx = SvcMsgFactory::newStartBlobTxMsg(volId_, blobName);
    auto asyncBlobTxReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    startBlobTx->txId = 2;
    startBlobTx->dmt_version = 1;
    startBlobTx->blob_mode = 0;
    asyncBlobTxReq->setPayload(FDSP_MSG_TYPEID(fpi::StartBlobTxMsg), startBlobTx);
    asyncBlobTxReq->onResponseCb(waiter.cb);
    startBlobTxIssued_++;
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    txStartTs = startTs;
    asyncBlobTxReq->invoke();
    waiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    startTxCounter->update(endTs - startTs);
    }
    ASSERT_EQ(waiter.error, ERR_OK) << "Error: " << waiter.error;

    SvcRequestCbTask<EPSvcRequest, fpi::DeleteBlobMsg> delBlobWaiter;
    auto delBlob = SvcMsgFactory::newDeleteBlobMsg(volId_, blobName);
    auto delBlobReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    delBlob->txId  = 2;
    delBlobReq->setPayload(FDSP_MSG_TYPEID(fpi::DeleteBlobMsg), delBlob);
    delBlobReq->onResponseCb(delBlobWaiter.cb);
    delBlobIssued_++;
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    delBlobReq->invoke();
    delBlobWaiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    delBlobCounter->update(endTs - startTs);
    }
    ASSERT_EQ(delBlobWaiter.error, ERR_OK) << "Error: " << delBlobWaiter.error;
    SvcRequestCbTask<EPSvcRequest, fpi::CommitBlobTxRspMsg> commitWaiter;
    auto commitBlobMsg = SvcMsgFactory::newCommitBlobTxMsg(volId_, blobName);
    auto asyncCommitBlobReq = gSvcRequestPool->newEPSvcRequest(svcUuid);
    commitBlobMsg->txId  = 2;
    commitBlobMsg->dmt_version = 1;
    asyncCommitBlobReq->setPayload(FDSP_MSG_TYPEID(fpi::CommitBlobTxMsg), commitBlobMsg);
    asyncCommitBlobReq->onResponseCb(commitWaiter.cb);
    commitBlobTxIssued_++;
    {
    fds_uint64_t startTs = util::getTimeStampNanos();
    asyncCommitBlobReq->invoke();
    commitWaiter.await();
    fds_uint64_t endTs = util::getTimeStampNanos();
    commitTxCounter->update(endTs - startTs);
    txCounter->update(endTs - txStartTs);
    }
    ASSERT_EQ(commitWaiter.error, ERR_OK) << "Error: " << commitWaiter.error;

    std::cout << "\033[33m[startBlobTx latency]\033[39m " << std::fixed << std::setprecision(3)
            << (startTxCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << startTxCounter->count() << std::endl;
    std::cout << "\033[33m[setBlobMeta latency]\033[39m " << std::fixed << std::setprecision(3)
            << (setBlobMetaCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << setBlobMetaCounter->count() << std::endl;
    std::cout << "\033[33m[commitBlobTx latency]\033[39m " << std::fixed << std::setprecision(3)
            << (commitTxCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << commitTxCounter->count() << std::endl;
    std::cout << "\033[33m[deleteBlobTest latency]\033[39m " << std::fixed << std::setprecision(3)
            << (delBlobCounter->latency() / (1024 * 1024)) << "ms     \033[33m[count]\033[39m "
            << delBlobCounter->count() << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    // process command line options
    po::options_description desc("\nDM test Command line options");
    desc.add_options()
            ("help,h"       , "help/ usage message")  // NOLINT
            ("num-volumes,v", po::value<fds_uint32_t>(&NUM_VOLUMES)->default_value(NUM_VOLUMES)        , "number of volumes")  // NOLINT
            ("obj-size,o"   , po::value<fds_uint32_t>(&MAX_OBJECT_SIZE)->default_value(MAX_OBJECT_SIZE), "max object size in bytes")  // NOLINT
            ("blob-size,b"  , po::value<fds_uint64_t>(&BLOB_SIZE)->default_value(BLOB_SIZE)            , "blob size in bytes")  // NOLINT
            ("num-blobs,n"  , po::value<fds_uint32_t>(&NUM_BLOBS)->default_value(NUM_BLOBS)            , "number of blobs")  // NOLINT
            ("profile,p"    , po::value<bool>(&profile)->default_value(profile)                        , "enable profile ")  // NOLINT
            ("puts-only"    , "do put operations only")
            ("no-delete"    , "do put & get operations only");

    DMApi::init(argc, argv, desc, "vol1");
    return RUN_ALL_TESTS();
}
