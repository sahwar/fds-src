/*
 * Copyright 2015 Formation Data Systems, Inc.
 */

#include "./dm_mocks.h"
#include "./dm_gtest.h"
#include "./dm_utils.h"

#include <testlib/SvcMsgFactory.h>
#include <vector>
#include <string>
#include <thread>
#include <google/profiler.h>

fds::DMTester* dmTester = NULL;
fds::concurrency::TaskStatus taskCount(0);

struct SeqIdTest : ::testing::Test {
    virtual void SetUp() override {
        static int i = 0;
        dmTester->addVolume(i);
        dmTester->TESTVOLID = dmTester->volumes[i]->volUUID;

        ++i;
    }

    virtual void TearDown() override {
    }
};

void startTxn(fds_volid_t volId, std::string blobName, int txnNum = 1, int blobMode = 0) {
    DMCallback cb;
    DEFINE_SHARED_PTR(AsyncHdr, asyncHdr);

    // start tx
    DEFINE_SHARED_PTR(StartBlobTxMsg, startBlbTx);

    startBlbTx->volume_id = volId.get();
    startBlbTx->blob_name = blobName;
    startBlbTx->txId = txnNum;
    startBlbTx->blob_mode = blobMode;
    startBlbTx->dmt_version = 1;
    auto dmBlobTxReq = new DmIoStartBlobTx(volId,
                                           startBlbTx->blob_name,
                                           startBlbTx->blob_version,
                                           startBlbTx->blob_mode,
                                           startBlbTx->dmt_version);
    dmBlobTxReq->ioBlobTxDesc = BlobTxId::ptr(new BlobTxId(startBlbTx->txId));
    dmBlobTxReq->cb = BIND_OBJ_CALLBACK(cb, DMCallback::handler, asyncHdr);
    TIMEDBLOCK("start") {
        dataMgr->handlers[FDS_START_BLOB_TX]->handleQueueItem(dmBlobTxReq);
        cb.wait();
    }
    EXPECT_EQ(ERR_OK, cb.e);
}

void commitTxn(fds_volid_t volId, std::string blobName, int txnNum = 1) {
    DMCallback cb;
    static sequence_id_t seq_id = 0;
    DEFINE_SHARED_PTR(AsyncHdr, asyncHdr);
    DEFINE_SHARED_PTR(CommitBlobTxMsg, commitBlbTx);
    commitBlbTx->volume_id = dmTester->TESTVOLID.get();
    commitBlbTx->blob_name = blobName;
    commitBlbTx->txId = txnNum;

    auto dmBlobTxReq1 = new DmIoCommitBlobTx(dmTester->TESTVOLID,
                                             commitBlbTx->blob_name,
                                             commitBlbTx->blob_version,
                                             commitBlbTx->dmt_version,
                                             ++seq_id);
    dmBlobTxReq1->ioBlobTxDesc = BlobTxId::ptr(new BlobTxId(commitBlbTx->txId));
    dmBlobTxReq1->cb =
            BIND_OBJ_CALLBACK(cb, DMCallback::handler, asyncHdr);

    TIMEDBLOCK("commit") {
        dataMgr->handlers[FDS_COMMIT_BLOB_TX]->handleQueueItem(dmBlobTxReq1);
        cb.wait();
    }
    EXPECT_EQ(ERR_OK, cb.e);
}

void putBlob(){
    uint64_t txnId;
    std::string blobName;
    DEFINE_SHARED_PTR(AsyncHdr, asyncHdr);

    for (uint i = 0; i < NUM_BLOBS; i++) {
        // update
        DEFINE_SHARED_PTR(UpdateCatalogMsg, updcatMsg);
        updcatMsg->volume_id = dmTester->TESTVOLID.get();

        DMCallback cb;
        txnId = dmTester->getNextTxnId();
        blobName = dmTester->getBlobName(i);
        updcatMsg->blob_name = blobName;
        updcatMsg->txId = txnId;

        fds::UpdateBlobInfoNoData(updcatMsg, MAX_OBJECT_SIZE, BLOB_SIZE);

        startTxn(dmTester->TESTVOLID, blobName, txnId);
        // FIXME(DAC): Memory leak.
        auto dmUpdCatReq = new DmIoUpdateCat(updcatMsg);
        dmUpdCatReq->cb = BIND_OBJ_CALLBACK(cb, DMCallback::handler, asyncHdr);
        TIMEDBLOCK("process") {
            dataMgr->handlers[FDS_CAT_UPD]->handleQueueItem(dmUpdCatReq);
            cb.wait();
        }
        EXPECT_EQ(ERR_OK, cb.e);
        commitTxn(dmTester->TESTVOLID, blobName, txnId);
    }
}

static void doPutBlobOnce(boost::shared_ptr<DMCallback> & cb, DmIoUpdateCatOnce * dmUpdCatReq) {
    TIMEDBLOCK("process") {
        dataMgr->handlers[FDS_CAT_UPD_ONCE]->handleQueueItem(dmUpdCatReq);
        cb->wait();
    }
    EXPECT_EQ(ERR_OK, cb->e);
    taskCount.done();
}

void putBlobOnce(){
    uint64_t txnId;
    std::string blobName;
    DEFINE_SHARED_PTR(AsyncHdr, asyncHdr);
    sequence_id_t seq_id = 0;

    taskCount.reset(NUM_BLOBS);

    for (uint i = 0; i < NUM_BLOBS; i++) {
        // update
        DEFINE_SHARED_PTR(UpdateCatalogOnceMsg, updcatMsg);
        updcatMsg->volume_id = dmTester->TESTVOLID.get();
        updcatMsg->dmt_version = 1;

        boost::shared_ptr<DMCallback> cb(new DMCallback());

        txnId = dmTester->getNextTxnId();
        blobName = dmTester->getBlobName(i);
        updcatMsg->blob_name = blobName;
        updcatMsg->txId = txnId;
        updcatMsg->sequence_id = ++seq_id;

        fds::UpdateBlobInfoNoData(updcatMsg, MAX_OBJECT_SIZE, BLOB_SIZE);

        auto dmCommitBlobOnceReq = new DmIoCommitBlobOnce(dmTester->TESTVOLID,
                                                          updcatMsg->blob_name,
                                                          updcatMsg->blob_version,
                                                          updcatMsg->dmt_version,
                                                          seq_id);
        dmCommitBlobOnceReq->ioBlobTxDesc = BlobTxId::ptr(new BlobTxId(updcatMsg->txId));
        dmCommitBlobOnceReq->cb =
            BIND_OBJ_CALLBACK(*cb.get(), DMCallback::handler, asyncHdr);

        auto dmUpdCatReq = new DmIoUpdateCatOnce(updcatMsg, dmCommitBlobOnceReq);
        dmCommitBlobOnceReq->parent = dmUpdCatReq;

        g_fdsprocess->proc_thrpool()->schedule(&doPutBlobOnce, cb, dmUpdCatReq);
    }
    taskCount.await();
}

TEST_F(SeqIdTest, SequenceIncrementPutOnce){
    sequence_id_t seq_id;

    MAX_OBJECT_SIZE = 2 * 1024 * 1024;    // 2MB
    BLOB_SIZE = 4 * 1024 * 1024;   // 4 MB
    NUM_BLOBS = 1024;

    TIMEDBLOCK("Scan For Max SeqId - Empty") {
        dataMgr->timeVolCat_->queryIface()->getVolumeSequenceId(dmTester->TESTVOLID, seq_id);
        EXPECT_EQ(0, seq_id);
    }

    putBlobOnce();

    TIMEDBLOCK("Scan For Max SeqId - Confirmation") {
        dataMgr->timeVolCat_->queryIface()->getVolumeSequenceId(dmTester->TESTVOLID, seq_id);
        EXPECT_EQ(NUM_BLOBS, seq_id);
    }

    printStats();
}

TEST_F(SeqIdTest, SequenceIncrementCommitTx){
    sequence_id_t seq_id;

    MAX_OBJECT_SIZE = 2 * 1024 * 1024;    // 2MB
    BLOB_SIZE = 4 * 1024 * 1024;   // 4 MB
    NUM_BLOBS = 1024;

    TIMEDBLOCK("Scan For Max SeqId - Empty") {
        dataMgr->timeVolCat_->queryIface()->getVolumeSequenceId(dmTester->TESTVOLID, seq_id);
        EXPECT_EQ(0, seq_id);
    }

    putBlob();

    TIMEDBLOCK("Scan For Max SeqId - Confirmation") {
        dataMgr->timeVolCat_->queryIface()->getVolumeSequenceId(dmTester->TESTVOLID, seq_id);
        EXPECT_EQ(NUM_BLOBS, seq_id);
    }

    printStats();
}

int main(int argc, char** argv) {
    // The following line must be executed to initialize Google Mock
    // (and Google Test) before running the tests.
    ::testing::InitGoogleMock(&argc, argv);

    // process command line options
    po::options_description desc("\nDM test Command line options");
    desc.add_options()
            ("help,h", "help/ usage message");  // NOLINT

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).allow_unregistered().run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    NUM_VOLUMES = ::testing::UnitTest::GetInstance()->total_test_count();

    dmTester = new fds::DMTester(argc, argv);
    dmTester->start();
    // setup default volumes
    generateVolumes(dmTester->volumes);
    dmTester->TESTBLOB = "testblob";

    int retCode = RUN_ALL_TESTS();

    dmTester->stop();
    dmTester->clean();

    return retCode;
}
