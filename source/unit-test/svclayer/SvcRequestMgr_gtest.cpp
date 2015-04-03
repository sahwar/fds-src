/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#define GTEST_USE_OWN_TR1_TUPLE 0

#include <unistd.h>
#include <string>
#include <iostream>
#include <stdexcept>
#include <boost/make_shared.hpp>
#include <fds_module_provider.h>
#include <net/net_utils.h>
#include <net/SvcRequestPool.h>
#include <net/SvcMgr.h>
#include <fdsp_utils.h>
#include <fiu-control.h>
#include <testlib/SvcMsgFactory.h>
#include <testlib/TestUtils.h>
#include <testlib/TestFixtures.h>
#include <util/fiu_util.h>
#include <SvcMgrModuleProvider.hpp>
#include <FakeSvcDomain.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <testlib/TestFixtures.h>

using ::testing::AtLeast;
using ::testing::Return;
using namespace fds;  // NOLINT

struct SvcRequestMgrTest : BaseTestFixture {
    static std::string confFile;

    static void SetUpTestCase() {
        confFile = getArg<std::string>("fds-root") + std::string("/etc/platform.conf");
    }

};
std::string SvcRequestMgrTest::confFile;

/**
* @brief Tests svc map update in the domain.
*/
TEST_F(SvcRequestMgrTest, epsvcrequest)
{
    int cnt = 2;
    FakeSyncSvcDomain domain(cnt, confFile);

    /* dest service is up...request should succeed */
    SvcRequestCbTask<EPSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter1;
    domain.sendGetStatusEpSvcRequest(0, 1, svcStatusWaiter1);
    svcStatusWaiter1.await();
    ASSERT_EQ(svcStatusWaiter1.error, ERR_OK) << "Error: " << svcStatusWaiter1.error;
    ASSERT_EQ(svcStatusWaiter1.response->status, fpi::SVC_STATUS_ACTIVE)
        << "Status: " << svcStatusWaiter1.response->status;

    /* dest service is down...request should fail */
    domain.kill(1);
    SvcRequestCbTask<EPSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter2;
    domain.sendGetStatusEpSvcRequest(0, 1, svcStatusWaiter2);
    svcStatusWaiter2.await();
    ASSERT_TRUE(svcStatusWaiter2.error == ERR_SVC_REQUEST_INVOCATION ||
                svcStatusWaiter2.error == ERR_SVC_REQUEST_TIMEOUT)
        << "Error: " << svcStatusWaiter2.error;

    /* dest service is up again...request should succeed */
    domain.spawn(1);
    SvcRequestCbTask<EPSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter3;
    domain.sendGetStatusEpSvcRequest(0, 1, svcStatusWaiter3);
    svcStatusWaiter3.await();
    ASSERT_EQ(svcStatusWaiter3.error, ERR_OK)
        << "Error: " << svcStatusWaiter3.error;
    ASSERT_EQ(svcStatusWaiter3.response->status, fpi::SVC_STATUS_ACTIVE)
        << "Status: " << svcStatusWaiter3.response->status;
}

/**
* @brief Test sending endpoint request agains invalid endpoint
*
*/
TEST_F(SvcRequestMgrTest, epsvcrequest_invalidep)
{
    int cnt = 2;
    FakeSyncSvcDomain domain(cnt, confFile);

    auto svcMgr1 = domain[0]->getSvcMgr();

    SvcRequestCbTask<EPSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter;
    auto svcStatusMsg = boost::make_shared<fpi::GetSvcStatusMsg>();
    auto asyncReq = svcMgr1->getSvcRequestMgr()->newEPSvcRequest(FakeSvcDomain::INVALID_SVCUUID);
    asyncReq->setPayload(FDSP_MSG_TYPEID(fpi::GetSvcStatusMsg), svcStatusMsg);
    asyncReq->setTimeoutMs(1000);
    asyncReq->onResponseCb(svcStatusWaiter.cb);
    asyncReq->invoke();

    svcStatusWaiter.await();
    ASSERT_EQ(svcStatusWaiter.error, ERR_SVC_REQUEST_INVOCATION) << "Error: " << svcStatusWaiter.error;
}


/* Tests basic failover style request */
TEST_F(SvcRequestMgrTest, failoversvcrequest)
{
    int cnt = 3;
    FakeSyncSvcDomain domain(cnt, confFile);

    /* all endpoints are up...request should succeed */
    SvcRequestCbTask<FailoverSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter;
    domain.sendGetStatusFailoverSvcRequest(0, {1,2}, svcStatusWaiter);

    svcStatusWaiter.await();
    ASSERT_EQ(svcStatusWaiter.error, ERR_OK) << "Error: " << svcStatusWaiter.error;
    ASSERT_EQ(svcStatusWaiter.response->status, fpi::SVC_STATUS_ACTIVE)
        << "Status: " << svcStatusWaiter.response->status;

    /* one endpoints is down...request should succeed */
    domain.kill(1);
    SvcRequestCbTask<FailoverSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter1;
    domain.sendGetStatusFailoverSvcRequest(0, {1,2}, svcStatusWaiter1);
    svcStatusWaiter1.await();
    ASSERT_EQ(svcStatusWaiter1.error, ERR_OK) << "Error: " << svcStatusWaiter1.error;
    ASSERT_EQ(svcStatusWaiter1.response->status, fpi::SVC_STATUS_ACTIVE)
        << "Status: " << svcStatusWaiter1.response->status;

    /* all endpoints are down. Request should fail */
    domain.kill(2);
    SvcRequestCbTask<FailoverSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter2;
    domain.sendGetStatusFailoverSvcRequest(0, {1,2}, svcStatusWaiter2);
    svcStatusWaiter2.await();
    ASSERT_TRUE(svcStatusWaiter2.error == ERR_SVC_REQUEST_INVOCATION ||
                svcStatusWaiter2.error == ERR_SVC_REQUEST_TIMEOUT)
        << "Error: " << svcStatusWaiter2.error;

    /* One service is up. Request should succeed */
    domain.spawn(2);
    SvcRequestCbTask<FailoverSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter3;
    domain.sendGetStatusFailoverSvcRequest(0, {1,2}, svcStatusWaiter3);
    svcStatusWaiter3.await();
    ASSERT_EQ(svcStatusWaiter3.error, ERR_OK) << "Error: " << svcStatusWaiter3.error;
    ASSERT_EQ(svcStatusWaiter3.response->status, fpi::SVC_STATUS_ACTIVE)
        << "Status: " << svcStatusWaiter3.response->status;
}

/* Tests basic quorum reqesut */
TEST_F(SvcRequestMgrTest, quorumsvcrequest)
{
    int cnt = 3;
    FakeSyncSvcDomain domain(cnt, confFile);

    SvcRequestCbTask<QuorumSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter;
    domain.sendGetStatusQuorumSvcRequest(0, {1,2}, svcStatusWaiter);

    svcStatusWaiter.await();
    ASSERT_EQ(svcStatusWaiter.error, ERR_OK) << "Error: " << svcStatusWaiter.error;
    ASSERT_EQ(svcStatusWaiter.response->status, fpi::SVC_STATUS_ACTIVE)
        << "Status: " << svcStatusWaiter.response->status;

    /* Kill both services.  Quorum request should fail */
    domain.kill(1);
    domain.kill(2);
    SvcRequestCbTask<QuorumSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter1;
    domain.sendGetStatusQuorumSvcRequest(0, {1,2}, svcStatusWaiter1);
    svcStatusWaiter1.await();
    ASSERT_TRUE(svcStatusWaiter1.error == ERR_SVC_REQUEST_INVOCATION ||
                svcStatusWaiter1.error == ERR_SVC_REQUEST_TIMEOUT)
        << "Error: " << svcStatusWaiter1.error;

    /* Bring one service up.  Quorum request should fail */ 
    domain.spawn(1);
    SvcRequestCbTask<QuorumSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter2;
    domain.sendGetStatusQuorumSvcRequest(0, {1,2}, svcStatusWaiter2);
    svcStatusWaiter2.await();
    ASSERT_TRUE(svcStatusWaiter2.error == ERR_SVC_REQUEST_INVOCATION ||
                svcStatusWaiter2.error == ERR_SVC_REQUEST_TIMEOUT)
        << "Error: " << svcStatusWaiter2.error;

    /* Bring both services up.  Quorum request should succeed */ 
    domain.spawn(2);
    SvcRequestCbTask<QuorumSvcRequest, fpi::GetSvcStatusRespMsg> svcStatusWaiter3;
    domain.sendGetStatusQuorumSvcRequest(0, {1,2}, svcStatusWaiter3);
    svcStatusWaiter3.await();
    ASSERT_EQ(svcStatusWaiter3.error, ERR_OK) << "Error: " << svcStatusWaiter3.error;
    ASSERT_EQ(svcStatusWaiter3.response->status, fpi::SVC_STATUS_ACTIVE)
        << "Status: " << svcStatusWaiter3.response->status;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    po::options_description opts("Allowed options");
    opts.add_options()
        ("help", "produce help message")
        ("fds-root", po::value<std::string>()->default_value("/fds"), "root");
    SvcRequestMgrTest::init(argc, argv, opts);
    return RUN_ALL_TESTS();
}
