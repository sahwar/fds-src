/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_INCLUDE_TESTFIXTURES_H_
#define SOURCE_INCLUDE_TESTFIXTURES_H_

#include <memory>
#include <string>
#include <testlib/Deployer.h>
#include <platform/platform-lib.h>
#include <gtest/gtest.h>

namespace fds {

/**
* @brief Test platform.  It bundles platform deamon and disables the run() method.
* Using this class provides a platform deamon that can be used for testing.
*/
struct TestPlatform : FdsService 
{
    TestPlatform(int argc, char *argv[], const std::string &log, Module **vec);
    virtual int run() override;
};

/**
* @brief Provides Test fixture for single Node base testing.  As part of fixture setup
* following is provided.
* 1. Single node domain environemt
* 2. Test platformd setup
* 3. One S3 volume
* All the above objects that provide above functionality are provided as static members.
*
* Usage:
* 1. Derive a fixture from this class.  You will have access test platform daemon and single node
* fds environemnt will be setup for you.
* 2. In your TEST_F() tests, you can use static members such as platform, volume id, etc.
*/
struct SingleNodeTest : ::testing::Test
{
    SingleNodeTest();
    static void init(int argc, char *argv[], const std::string volName);

    static void SetUpTestCase();

    static void TearDownTestCase();

  protected:
    static int argc_;
    static char **argv_;
    static std::unique_ptr<TestPlatform> platform_;
    static std::string volName_;
    static uint64_t volId_;
    static std::unique_ptr<Deployer> deployer_;
};
}  // namespace fds

#endif  // SOURCE_INCLUDE_TESTFIXTURES_H_